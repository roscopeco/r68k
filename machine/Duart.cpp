//
// Minimal XR68C681 DUART model for r68k, see Duart.h.
//

#include <iostream>
#include "Duart.h"
// The web page (Emscripten) and the hosted builds (R68K_HOSTED: the
// Windows program of src68k/host) give channel A to their own console.
#if defined(__EMSCRIPTEN__) || defined(R68K_HOSTED)
#define R68K_PAGE_IO 1
#include <string>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#elif defined(R68K_PAGE_IO)
#define EMSCRIPTEN_KEEPALIVE
extern "C" void r68k_host_output(const char *data, size_t n);
#else
#include <sys/select.h>
#include <unistd.h>
#endif

#ifdef R68K_PAGE_IO
// In a web page (src68k/web in bbc-basic-68k, with webmain.cpp in place of
// main()), or a hosted build (src68k/host, hostmain.cpp): channel A is the
// page's terminal, or the program's console.
// * Keys come in through r68k_key() (r68k_pending() of them are waiting,
//   r68k_drop() drops them) and go to the receiver at most one per KEY_TIME
//   of real time.  Minix takes input from the line's buffer (256 bytes) on
//   clock ticks, and the clock runs in real time, however fast the CPU
//   runs: pacing by CPU cycles would overrun the buffer with pasted text.
// * Output is collected until r68k_flush() hands it to
//   Module.r68kOutput(bytes) (hosted: r68k_host_output()).
// * Timer interrupts missed while the page was busy (the main loop runs in
//   frames) are made up, up to MAX_TICKS, so the Minix clock keeps time.
// * r68k_duart_idle: nothing pending (no key, no interrupt), and
//   r68k_duart_wait: milliseconds to the next timer interrupt, for the main
//   loop's idle test.
static constexpr int MAX_TICKS = 10;
static constexpr std::chrono::microseconds KEY_TIME(1000);
static std::deque<std::uint8_t> webKeys;
static std::string webOut;
static int missedTicks;
static std::chrono::steady_clock::time_point nextKey;

extern "C" {
    bool r68k_duart_idle;
    double r68k_duart_wait;

    EMSCRIPTEN_KEEPALIVE void r68k_key(int c) { webKeys.push_back((std::uint8_t)c); }
    EMSCRIPTEN_KEEPALIVE int r68k_pending() { return (int)webKeys.size(); }
    EMSCRIPTEN_KEEPALIVE void r68k_drop() { webKeys.clear(); }

    void r68k_flush() {
        if (webOut.empty()) return;
#ifdef __EMSCRIPTEN__
        EM_ASM({ if (Module.r68kOutput) Module.r68kOutput(HEAPU8.slice($0, $0 + $1)); },
               webOut.data(), webOut.size());
#else
        r68k_host_output(webOut.data(), webOut.size());
#endif
        webOut.clear();
    }
}
#endif

namespace rosco {
    namespace m68k {
        namespace emu {
            // Register numbers (address = BASE + 2*n + 1)
            enum {
                MRA = 0, SRA_CSRA = 1, MISR_CRA = 2, RHRA_THRA = 3,
                IPCR_ACR = 4, ISR_IMR = 5, CTU_CTUR = 6, CTL_CTLR = 7,
                MRB = 8, SRB_CSRB = 9, CRB = 10, RHRB_THRB = 11,
                IVR = 12, IP_OPCR = 13, STARTCC_OPSET = 14, STOPCC_OPCLR = 15
            };

            // Status register bits
            static constexpr std::uint8_t SR_RXRDY = 0x01, SR_TXRDY = 0x04, SR_TXEMT = 0x08;
            // Interrupt status bits
            static constexpr std::uint8_t ISR_TXRDYA = 0x01, ISR_RXRDYA = 0x02,
                ISR_CTR = 0x08, ISR_TXRDYB = 0x10;

            static constexpr long XTAL = 3686400;

            Duart::Duart(std::function<void(bool)> setIrq) : setIrq(std::move(setIrq)) {
            }

            std::uint8_t Duart::isr() const {
                std::uint8_t v = ISR_TXRDYA | ISR_TXRDYB;   // transmitters never busy
                if (!rxA.empty()) v |= ISR_RXRDYA;
                if (counterReady) v |= ISR_CTR;
                return v;
            }

            void Duart::update() {
                setIrq((isr() & imr) != 0);
            }

            std::chrono::nanoseconds Duart::period() const {
                // Timer mode, X1/16 (ACR 0x70) toggles every preset counts:
                // one interrupt per 2 * preset * 16 / XTAL seconds.
                long divider = (acr & 0x10) ? 16 : 1;
                long long ns = 2LL * (preset ? preset : 0x10000) * divider * 1000000000LL / XTAL;
                return std::chrono::nanoseconds(ns);
            }

            std::uint8_t Duart::read(std::uint32_t address) {
                if ((address & 1) == 0) return 0xFF;        // even bytes not decoded
                std::uint8_t v = 0;

                switch ((address - BASE) >> 1) {
                case MRA:       v = mrA[mrPtrA]; mrPtrA = 1; break;
                case SRA_CSRA:  v = SR_TXRDY | SR_TXEMT | (rxA.empty() ? 0 : SR_RXRDY); break;
                case MISR_CRA:  v = isr() & imr; break;
                case RHRA_THRA:
                    if (!rxA.empty()) {
                        v = rxA.front();
                        rxA.pop_front();
                    }
                    break;
                case IPCR_ACR:  v = 0; break;
                case ISR_IMR:   v = isr(); break;
                case CTU_CTUR:  v = preset >> 8; break;
                case CTL_CTLR:  v = preset & 0xFF; break;
                case MRB:       v = mrB[mrPtrB]; mrPtrB = 1; break;
                case SRB_CSRB:  v = SR_TXRDY | SR_TXEMT; break;
                case IVR:       v = ivr; break;
                case IP_OPCR:   v = 0; break;               // CTS inputs asserted (low)
                case STARTCC_OPSET:
                    counterRunning = true;
                    counterReady = false;
                    nextExpiry = std::chrono::steady_clock::now() + period();
                    break;
                case STOPCC_OPCLR:
                    // Timer mode: only clears the ready bit, counter mode: stop
                    counterReady = false;
                    if ((acr & 0x40) == 0) counterRunning = false;
                    break;
                default:        break;
                }
                update();
                return v;
            }

            void Duart::write(std::uint32_t address, std::uint8_t data) {
                if ((address & 1) == 0) return;

                switch ((address - BASE) >> 1) {
                case MRA:       mrA[mrPtrA] = data; mrPtrA = 1; break;
                case MISR_CRA:  if ((data & 0x70) == 0x10) mrPtrA = 0; break;
#ifdef R68K_PAGE_IO
                case RHRA_THRA: webOut.push_back((char)data); break;
#else
                case RHRA_THRA: std::cout.put((char)data); std::cout.flush(); break;
#endif
                case IPCR_ACR:  acr = data; break;
                case ISR_IMR:   imr = data; break;
                case CTU_CTUR:  preset = (preset & 0x00FF) | (data << 8); break;
                case CTL_CTLR:  preset = (preset & 0xFF00) | data; break;
                case MRB:       mrB[mrPtrB] = data; mrPtrB = 1; break;
                case CRB:       if ((data & 0x70) == 0x10) mrPtrB = 0; break;
                case RHRB_THRB: std::cerr.put((char)data); std::cerr.flush(); break;
                case IVR:       ivr = data; break;
                default:        break;                      // CSR, OPCR, OP set/reset
                }
                update();
            }

            std::uint8_t Duart::rxTake() {
                std::uint8_t c = 0;
                if (!rxA.empty()) {
                    c = rxA.front();
                    rxA.pop_front();
                }
                update();
                return c;
            }

            void Duart::poll() {
#ifdef R68K_PAGE_IO
                if (counterRunning) {
                    auto now = std::chrono::steady_clock::now();
                    while (now >= nextExpiry && missedTicks < MAX_TICKS) {
                        missedTicks++;
                        nextExpiry += period();
                    }
                    if (now >= nextExpiry) nextExpiry = now + period();
                    if (!counterReady && missedTicks > 0) {
                        counterReady = true;
                        missedTicks--;
                    }
                }
                if (rxA.empty() && !webKeys.empty()
                        && std::chrono::steady_clock::now() >= nextKey) {
                    nextKey = std::chrono::steady_clock::now() + KEY_TIME;
                    rxA.push_back(webKeys.front());
                    webKeys.pop_front();
                }
                r68k_duart_idle = !counterReady && missedTicks == 0
                    && rxA.empty() && webKeys.empty();
                r68k_duart_wait = !counterRunning ? 10 :
                    std::chrono::duration<double, std::milli>(
                        nextExpiry - std::chrono::steady_clock::now()).count();
                update();
#else
                if (counterRunning) {
                    auto now = std::chrono::steady_clock::now();
                    if (now >= nextExpiry) {
                        counterReady = true;
                        nextExpiry += period();
                        if (nextExpiry < now) nextExpiry = now + period();
                    }
                }

                if (rxA.empty()) {
                    fd_set fds;
                    struct timeval tv = {0, 0};
                    FD_ZERO(&fds);
                    FD_SET(STDIN_FILENO, &fds);
                    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
                        char c;
                        if (::read(STDIN_FILENO, &c, 1) == 1) rxA.push_back((std::uint8_t)c);
                    }
                }
                update();
#endif
            }
        }
    }
}
