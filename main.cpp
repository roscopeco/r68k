#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <stdint.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <iomanip>
#include <vector>

#include "musashi/m68k.h"
#include "musashi/m68kcpu.h"
#include "AddressDecoder.h"

using namespace std;

// The ROM window, 0xE00000-0xEFFFFF: 1MB, like the v2.x boards' flash
#define ROM_SIZE   0x100000

#define DUART_IRQ	4
#define DUART_VEC	0x45

#define UPTICKS    0x40C
#define ECHO_ON    0x410
#define PROMPT_ON  0x411
#define LF_DISPLAY 0x412

struct termios originalTermios;
static bool termSaved;

static void restore_term() {
    if (termSaved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &originalTermios);
    }
}

static void restore_term_and_die(int sig) {
    restore_term();
    signal(sig, SIG_DFL);
    raise(sig);
}

// Raw-ish input; the terminal is put back on any exit, also on ^C.
// stdin stays blocking: read_char() waits in read(), the other input
// checks use select().
void init_term() {
    struct termios newTermios;

    if (tcgetattr(STDIN_FILENO, &originalTermios) != 0) {
        return;     // not a terminal
    }
    termSaved = true;

    newTermios = originalTermios;
    newTermios.c_lflag &= ~(ICANON | ECHO);
    newTermios.c_iflag &= ~(ICRNL | INLCR);

    tcsetattr(STDIN_FILENO, TCSANOW, &newTermios);

    atexit(restore_term);
    signal(SIGINT, restore_term_and_die);
    signal(SIGTERM, restore_term_and_die);
    signal(SIGHUP, restore_term_and_die);
}

bool check_char() {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    return select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) > 0;
}

char read_char() {
    char c = 0;
    while (c == 0) {
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n == 0 || (n < 0 && errno != EINTR)) {
            // End of input (a pipe, a file, /dev/null): nothing more will come
            fprintf(stderr, "\n[r68k] end of input, exiting\n");
            exit(0);
        }
    }
    return c;
}

// easy68k helper functions

#define BUF_LEN 78
#define BUF_MAX BUF_LEN - 2
static uint8_t buf[BUF_LEN];

static uint8_t digit(unsigned char digit) {
    if (digit < 10) {
        return (char)(digit + '0');
    } else {
        return (char)(digit - 10 + 'A');
    }
}

static char* print_unsigned(uint32_t num, uint8_t base) {
    if (base < 2 || base > 36) {
        buf[0] = 0;
        return (char *)buf;
    }

    unsigned char bp = BUF_MAX;

    if (num == 0) {
        buf[bp--] = '0';
    } else {
        while (num > 0) {
            buf[bp--] = digit(num % base);
            num /= base;
        }
    } 

    return ((char*)&buf[bp+1]);
}

// Bus errors for unmapped addresses (-d), see memoryglue.cpp
extern "C" bool r68k_bus_errors;
extern "C" int r68k_no_bus_errors;

// r68k's own looks at memory (firmware traps, debug aids) never take a
// bus error
struct NoBusErrors {
    NoBusErrors() { r68k_no_bus_errors++; }
    ~NoBusErrors() { r68k_no_bus_errors--; }
};

extern "C" {
    rosco::m68k::emu::AddressDecoder* __mem;
    rosco::m68k::emu::Duart* __duart = nullptr;
    std::fstream ifs("rosco_sd.bin", std::ios::binary | std::ios::ate | std::ios::in | std::ios::out);

    int illegal_instruction_handler(int opcode) {
        NoBusErrors noBusErrors;
        m68ki_cpu_core ctx;
        m68k_get_context(&ctx);

        uint32_t d7 = m68k_get_reg(&ctx, M68K_REG_D7);
        uint32_t d6 = m68k_get_reg(&ctx, M68K_REG_D6);
        uint32_t d0 = m68k_get_reg(&ctx, M68K_REG_D0);
        uint32_t d1 = m68k_get_reg(&ctx, M68K_REG_D1);
        uint32_t d2 = m68k_get_reg(&ctx, M68K_REG_D2);
        uint32_t a0 = m68k_get_reg(&ctx, M68K_REG_A0);
        uint32_t a1 = m68k_get_reg(&ctx, M68K_REG_A1);
        uint32_t a2 = m68k_get_reg(&ctx, M68K_REG_A2);
        uint32_t a7 = m68k_get_reg(&ctx, M68K_REG_A7);

        // Firmware traps are an ILLEGAL (0x4AFC) with the magic in D7/D6
        if (opcode == 0x4AFC && (d7 & 0xFFFFFF00) == 0xF0F0F000 && d6 == 0xAA55AA55) {
            // It's a trap!

            // below leaves Easy68k ops from E0 and up, others from 0 ..
            uint8_t op = d7 & 0x000000FF;
            if (op >= 0xF0) {
                op &= 0x0F;
            } 

            uint8_t c;
            bool r;
            int ptr;
            int chars_left = 0;
            int chars_read = 0;
            int num = 0;
            
            switch (op) {				
                case 0:
                    // Print
                    do {
                         c = m68k_read_memory_8(a0++);
                         if (c) {
                            cout << c;
                         }
                    } while (c != 0);

                    break;
                case 1:
                    // println
                    do {
                         c = m68k_read_memory_8(a0++);
                         if (c) {
                            cout << c;
                         }
                    } while (c != 0);

                    cout << endl;

                    break;
                case 2:
                    // printchar
                    c = (d0 & 0xFF);
                    if (c) {
                        cout << c;
                    }

                    break;
                case 3:
                    // prog_exit
                    m68k_pulse_halt();
                    exit(m68k_read_memory_32(a7 + 4));       // assuming called from cstdlib - C will have stacked an exit code
                    break;
                case 4:
                    // check_char
                    r = check_char();
                    m68k_set_reg(M68K_REG_D0, r ? 1 : 0);

                    break;
                case 5:
                    // read_char
                    cout << flush;
                    c = read_char();
                    cout << flush;
                    m68k_set_reg(M68K_REG_D0, c);

                    break;
                case 6:
                    // sd_init
                    if (!ifs) {
                        m68k_set_reg(M68K_REG_D0, 1);
                    } else {
                        m68k_write_memory_8(a1+0, 1);		// Initialized
                        m68k_write_memory_8(a1+1, 2);		// SDHC
                        m68k_write_memory_8(a1+2, 0);		// No current block
                        m68k_write_memory_32(a1+3, 0);		// Ignored (current block num)
                        m68k_write_memory_16(a1+7, 0);		// Ignored (current block offset)
                        m68k_write_memory_8(a1+9, 0);		// No partial reads (_could_ support, just don't yet)
                        m68k_set_reg(M68K_REG_D0, 0);		// Success
                    }
                    break;
                case 7:
                    // sd_read
                    if (ifs && m68k_read_memory_8(a1) > 0) {
                        std::vector<char> buf(512);

                        ifs.clear();
                        ifs.seekg(std::streamoff(d1) * 512, std::ios::beg);
                        ifs.read(&buf.front(), 512);

                        if (ifs.gcount() == 512) {
                            for (auto data : buf) {
                                m68k_write_memory_8(a2++, data);
                            }

                            m68k_set_reg(M68K_REG_D0, 1);		    // succeed
                        } else {
                            cout << "!!! Bad Read" << endl;
                            m68k_set_reg(M68K_REG_D0, 0);		// fail
                        }
                    } else {						
                        cout << "!!! Not init" << endl;
                        m68k_set_reg(M68K_REG_D0, 0);		// fail
                    }

                    break;
                case 8:
                    // sd_write
                    if (a2 < 0xe00000 && ifs && m68k_read_memory_8(a1) > 0) {
                        std::vector<char> buf(512);

                        for (int i = 0; i < 512; i++) {
                            buf[i] = m68k_read_memory_8(a2++);
                        }

                        ifs.clear();
                        ifs.seekp(std::streamoff(d1) * 512, std::ios::beg);
                        ifs.write(&buf.front(), 512);
                        ifs.flush();

                        if (ifs.good()) {
                            m68k_set_reg(M68K_REG_D0, 1);		    // succeed
                        } else {
                            cout << "!!! Bad Write" << endl;
                            m68k_set_reg(M68K_REG_D0, 0);		// fail
                        }
                    } else {						
                        cout << "!!! Not init or out of bounds" << endl;
                        m68k_set_reg(M68K_REG_D0, 0);		// fail
                    }

                    break;
                // Start of Easy68k traps
                case 0xD0:
                case 0xD1:
                    // PRINT_LN_LEN / PRINT_LEN
                    do {
                         c = m68k_read_memory_8(a1++);
                         if (c) {
                            cout << c;
                         }
                    } while ((c != 0) && ((--d1 & 0xFF) > 0));

                    if (op == 0xD0) {
                        cout << endl;
                    }

                    break;
                case 0xD2:
                    // READSTR
                    if (m68k_read_memory_8(PROMPT_ON) == 1) {
                        cout << "Input$> ";
                    } 

                    chars_read = 0;
                    ptr = a1;  // save start of input buffer
                    
                    while (chars_read++ < 80) {
                        cout << flush;
                        c = read_char();
                        cout << flush;

                        if (c == 0x0D) {
                            break;
                        }

                        if (m68k_read_memory_8(ECHO_ON) == 1) {
                            cout << c;
                        }

                        m68k_write_memory_8(a1++, c);
                    }

                    m68k_write_memory_8(a1++, 0);

                    if (m68k_read_memory_8(LF_DISPLAY) == 1) {
                        cout << endl;
                    }

                    m68k_set_reg(M68K_REG_D1, (chars_read - 1)); 
                    m68k_set_reg(M68K_REG_A1, ptr); 

                    break;
                case 0xD3:
                    // DISPLAYNUM_SIGNED
                    cout << (int)d1;

                    break;
                case 0xD4:
                    // READNUM
                    if (m68k_read_memory_8(PROMPT_ON) == 1) {
                        cout << "Input#> ";
                    }

                    chars_left = 10;

                    while (--chars_left) {
                        cout << flush;
                        c = read_char();
                        cout << flush;

                        if (c == 0x0D) {
                            break;
                        }
                    
                        if ((c >= '0') && (c <= '9')) {
                            num = (num * 10) + (c - '0');
                            if (m68k_read_memory_8(ECHO_ON) == 1) {
                                cout << c;
                            }
                        } 
                    }
                    if (m68k_read_memory_8(LF_DISPLAY) == 1) {
                        cout << endl;
                    }
                    m68k_set_reg(M68K_REG_D1, num);

                    break;
                case 0xD5:
                    // READCHAR
                    cout << flush;
                    c = read_char();
                    cout << flush;
                    m68k_set_reg(M68K_REG_D1, c);

                    break;
                case 0xD6:
                    // SENDCHAR
                    cout << (char) (d1 & 0xFF);
                    
                    break;
                case 0xD7:
                    // CHECKINPUT
                    r = check_char();
                    m68k_set_reg(M68K_REG_D1, r ? 1 : 0);

                    break;
                case 0xD8:
                    // GETUPTICKS
                    m68k_set_reg(M68K_REG_D1, m68k_read_memory_32(UPTICKS));
                    
                    break;
                case 0xD9:
                    // TERMINATE
                    m68k_pulse_halt();
                    exit(0);

                    break;
                // case 0xDA:
                    // Note implemented
                    
                case 0xDB:
                    // MOVEXY
                    if ((d1 & 0xFFFF) == 0xFF00) {
                        // clear screen
                        cout << endl << "easy68k CLRSCR 0xDB not implemneted" << endl;
                    } else {
                        // Move X, Y
                        cout << endl << "easy68k MOVE X,Y 0xDB " << ((d1 & 0xFF00) >> 8) << "," << (d1 & 0xFF) << " not implemented" << endl;
                    }

                    break;
                case 0xDC:
                    // SETECHO
                    if (d1 == 0) {
                        m68k_write_memory_8(ECHO_ON, 0);
                    }
                    if (d1 == 1) {
                        m68k_write_memory_8(ECHO_ON, 1);
                    }

                    break;
                case 0xDD:
                case 0xDE:
                    // PRINTLN_SZ / PRINT_SZ
                    do {
                         c = m68k_read_memory_8(a1++);
                         if (c) {
                            cout << c;
                         }
                    } while (c != 0);

                    if (op == 0xDD) {
                        cout << endl;
                    }

                    break;
                case 0xDF:
                    // PRINT_UNSIGNED
                    cout << print_unsigned(d1, d2);

                    break;
                case 0xE0:
                    // SETDISPLAY
                    if (d1 == 0) {  
                        m68k_write_memory_8(PROMPT_ON, 0);                        
                    }
                    if (d1 == 1) {  
                        m68k_write_memory_8(PROMPT_ON, 1);                        
                    }
                    if (d1 == 2) {  
                        m68k_write_memory_8(LF_DISPLAY, 0);                        
                    }
                    if (d1 == 3) {  
                        m68k_write_memory_8(LF_DISPLAY, 1);                        
                    }

                    break;
                case 0xE1:
                    // PRINTSZ_NUM
                    do {
                         c = m68k_read_memory_8(a1++);
                         if (c) {
                            cout << c;
                         }
                    } while (c != 0);

                    cout << (int)d1;

                    break;
                case 0xE2:
                    // PRINTSZ_READ_NUM
                    do {
                         c = m68k_read_memory_8(a1++);
                         if (c) {
                            cout << c;
                         }
                    } while (c != 0);
                    
                    chars_left = 10;
                    
                    while (--chars_left) {
                        cout << flush;
                        c = read_char();
                        cout << flush;

                        if (c == 0x0D) {
                            break;
                        }
                    
                        if ((c >= '0') && (c <= '9')) {
                            num = (num * 10) + (c - '0');
                            if (m68k_read_memory_8(ECHO_ON) == 1) {
                                cout << c;
                            }
                        } 
                    }
                    if (m68k_read_memory_8(LF_DISPLAY) == 1) {
                        cout << endl;
                    }
                    m68k_set_reg(M68K_REG_D1, num);

                    break;
                // case 0xE3:
                    // Not implemented

                case 0xE4:
                    // PRINTNUM_SIGNED_WIDTH
                    cout << setw(d2) << (int)d1;
                    break;
                
                default:
                    cerr << "<UNKNOWN OP " << hex << (int)op << "; D7=0x" << d7 << "; D6=0x" << d6 << ": IGNORED>" << dec << endl;
            }

            return 1;
        }

        // Not a firmware trap: a real illegal instruction, so take the
        // exception (vector 4) as the CPU would.
        // The firmware's CPU detection does this on purpose, with its own
        // handler. Only when vector 4 is still the default from the ROM's
        // vector table, which halts for ever, stop here instead with the
        // exit status of a SIGILL (128 + 4).
        uint32_t handler = m68k_read_memory_32(m68k_get_reg(&ctx, M68K_REG_VBR) + 4 * 4);

        if (handler == m68k_read_memory_32(0xE00000 + 4 * 4)) {
            fprintf(stderr, "\n[r68k] illegal instruction %04X at %08X, no handler: exiting\n",
                    opcode & 0xFFFF, m68k_get_reg(&ctx, M68K_REG_PPC));
            m68k_pulse_halt();
            exit(128 + 4);
        }

        return 0;
    }

    int interrupt_ack_handler(unsigned int irq) {
        switch (irq) {
        case DUART_IRQ:
            if (__duart != nullptr) {
                // Emulated XR68C681: level sensitive, vector from IVR
                return __duart->vector();
            }
            // DUART timer tick - vector to 0x45
            m68k_set_irq(0);
            return DUART_VEC;
        default:
            cerr << "WARN: Unexpected IRQ " << irq << "; Autovectoring, but machine will probably lock up!" << endl;
            return M68K_INT_ACK_AUTOVECTOR;
        }
    }
}

// Debugging aid: "kill -USR1 <pid>" prints where the CPU is, and the
// last program counters (the jumps between them are what matters).
static volatile sig_atomic_t dump_requested = 0;
static unsigned int pc_ring[4096];
static unsigned int pc_pos;

static bool pc_frozen;
// RAM size: 1MB (the on-board RAM) unless R68K_RAM gives another, e.g.
// R68K_RAM=5M (on-board plus the 4MB expansion) or R68K_RAM=2048K
static unsigned int ram_size = 0x100000;

static unsigned int get_ram_size() {
    const char *e = getenv("R68K_RAM");
    if (e == NULL || *e == 0) return 0x100000;
    char *end;
    unsigned long n = strtoul(e, &end, 0);
    if (*end == 'M' || *end == 'm') n <<= 20, end++;
    else if (*end == 'K' || *end == 'k') n <<= 10, end++;
    if (end == e || *end != 0 || n < 0x80000 || n > 0xE00000 || (n & 0x7FFFF) != 0) {
        fprintf(stderr, "r68k: R68K_RAM must be a multiple of 512K, 512K-14M\n");
        exit(1);
    }
    return n;
}

static void dump_cpu();

// A word or long access at an odd address (or an odd PC): the 68010
// takes an address error. With vector 3 still the ROM's default, which
// halts for ever, report and stop instead, with the exit status of a
// SIGBUS. Called by Musashi before it takes the exception.
extern "C" void r68k_address_error(unsigned int address, int write, unsigned int pc) {
    NoBusErrors noBusErrors;
    uint32_t handler = m68k_read_memory_32(m68k_get_reg(NULL, M68K_REG_VBR) + 3 * 4);

    if (handler == m68k_read_memory_32(0xE00000 + 3 * 4)) {
        fprintf(stderr, "\n[r68k] address error: %s at %08X, PC %08X, no handler: exiting\n",
                write ? "write" : "read", address, pc);
        exit(128 + SIGBUS);
    }
}

// Whether Musashi calls r68k_trace_pc at all (see m68kconf.h)
extern "C" {
    int r68k_trace_on = 0;
}

// ---- profiling: only with R68K_PROF=<file> set ----
// The program writes 1 to the byte at 0xFFFFF0 to start, 2 to stop: the
// cycles spent at each instruction address (in RAM) and the totals are
// appended to the file $R68K_PROF. Without R68K_PROF that address is not
// special (memoryglue.cpp).
extern "C" {
    bool r68k_prof_enabled = false;
}
static unsigned long long prof_acc;             // cycles of finished slices
static bool prof_on;
static unsigned long long prof_t0, prof_last, prof_ninsn;
static unsigned int prof_lastpc;
static std::vector<unsigned long long> prof_cyc, prof_cnt;
static int prof_run;

static unsigned long long prof_now() {
    return prof_acc + (unsigned) m68k_cycles_run();
}

extern "C" void r68k_prof_ctl(unsigned int v) {
    if (v == 1) {
        prof_cyc.assign(ram_size / 2, 0);
        prof_cnt.assign(ram_size / 2, 0);
        prof_ninsn = 0;
        prof_t0 = prof_last = prof_now();
        prof_lastpc = 0xFFFFFFFF;
        prof_on = true;
    } else if (v == 2 && prof_on) {
        unsigned long long t = prof_now();
        if (prof_lastpc < ram_size)
            prof_cyc[prof_lastpc / 2] += t - prof_last;
        prof_on = false;
        const char *f = getenv("R68K_PROF");
        FILE *o = fopen(f != NULL && *f != 0 ? f : "r68k.prof", "a");
        if (!o) return;
        fprintf(o, "# run %d cycles %llu instructions %llu\n", ++prof_run,
                t - prof_t0, prof_ninsn);
        for (unsigned i = 0; i < prof_cyc.size(); i++)
            if (prof_cnt[i])
                fprintf(o, "%06X %llu %llu\n", i * 2, prof_cyc[i], prof_cnt[i]);
        fclose(o);
    }
}

extern "C" void r68k_trace_pc(unsigned int pc) {
    if (prof_on) {
        unsigned long long t = prof_now();
        if (prof_lastpc < ram_size)
            prof_cyc[prof_lastpc / 2] += t - prof_last;
        prof_last = t;
        prof_lastpc = pc;
        prof_ninsn++;
        if (pc < ram_size)
            prof_cnt[pc / 2]++;
    }
    // Only with R68K_TRACE set: keep a PC trace (shown by the SIGUSR1
    // dump), stop it when the PC runs wild.  R68K_DUMPAT=<hex pc>: dump
    // CPU and RAM the first time pc is reached.
    NoBusErrors noBusErrors;
    static int dumpat_init, tracing;
    static unsigned long dumpat;
    if (!dumpat_init) {
        const char *e = getenv("R68K_DUMPAT");
        dumpat_init = 1;
        dumpat = e ? strtoul(e, NULL, 16) : 0;
        tracing = getenv("R68K_TRACE") != NULL;
        if (!tracing) pc_frozen = true;
    }
    if (dumpat && pc == dumpat) {
        dumpat = 0;
        fprintf(stderr, "\n[r68k] reached %08X\n", pc);
        dump_cpu();
    }
    if (pc_frozen) return;
    pc_ring[pc_pos++ & 4095] = pc;
    // stop at the first PC outside RAM and ROM, or odd, or the second
    // zero opcode in a row (running through empty memory): keep the way there
    static unsigned int zeros;
    zeros = (pc & 1) == 0 && m68k_read_memory_16(pc) == 0 ? zeros + 1 : 0;
    if ((pc & 1) || zeros >= 2 ||
        (pc >= ram_size && (pc < 0xE00000 || pc >= 0xF00000))) {
        pc_frozen = true;
        fprintf(stderr, "\n[r68k] PC went wild: %08X (trace frozen)\n", pc);
    }
}

static void dump_cpu() {
    NoBusErrors noBusErrors;
    char buf[100];
    unsigned int pc = m68k_get_reg(NULL, M68K_REG_PC);
    fprintf(stderr, "\n[r68k] PC=%08X SR=%04X SP=%08X USP=%08X SSP=%08X\n",
            pc, m68k_get_reg(NULL, M68K_REG_SR), m68k_get_reg(NULL, M68K_REG_SP),
            m68k_get_reg(NULL, M68K_REG_USP), m68k_get_reg(NULL, M68K_REG_ISP));
    for (int r = 0; r < 8; r++)
        fprintf(stderr, "[r68k] D%d=%08X A%d=%08X\n", r,
                m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + r)), r,
                m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + r)));
    for (int i = 0; i < 6; i++) {
        unsigned int len = m68k_disassemble(buf, pc, M68K_CPU_TYPE_68010);
        fprintf(stderr, "[r68k]   %08X  %s\n", pc, buf);
        pc += len;
    }
    // the RAM, for a closer look
    if (FILE *f = fopen("r68k-ram.bin", "wb")) {
        for (unsigned int a = 0; a < ram_size; a++) fputc(m68k_read_memory_8(a), f);
        fclose(f);
        fprintf(stderr, "[r68k] RAM written to r68k-ram.bin\n");
    }
    // non-sequential steps in the trace: where control flow went
    unsigned int prev = 0;
    for (unsigned int i = pc_pos - 4096; i != pc_pos; i++) {
        unsigned int p = pc_ring[i & 4095];
        if (p < prev || p > prev + 10) {
            m68k_disassemble(buf, prev, M68K_CPU_TYPE_68010);
            fprintf(stderr, "[r68k] trace %08X %-30s -> %08X\n", prev, buf, p);
        }
        prev = p;
    }
}

// The ROM: -r, else R68K_ROM if set (e.g. a real firmware, with -d),
// else the one next to the r68k executable, wherever it was started from
static std::filesystem::path rom_path(const char *argv0, const char *option) {
    if (option != NULL) {
        return option;
    }

    const char *rom = getenv("R68K_ROM");
    if (rom != NULL && *rom != 0) {
        return rom;
    }

    std::error_code ec;
    std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) {
        exe = argv0;
    }

    std::filesystem::path dir = exe.parent_path();
    if (dir.empty()) {
        dir = ".";
    }
    return dir / "firmware" / "rosco_m68k.rom";
}

static void check_dump() {
    if (dump_requested) {
        dump_requested = 0;
        dump_cpu();
    }
}

static void usage(std::ostream &out) {
    out << "Usage: r68k [-d] [-r <rom>] [-w] <binary>   (options anywhere, -- ends them)" << endl;
    out << "  -d        emulate the XR68C681 DUART registers and timer, and bus" << endl;
    out << "            errors for unmapped addresses (real firmware runs with this)" << endl;
    out << "  -r <rom>  ROM image, also -r<rom> (default R68K_ROM, else" << endl;
    out << "            firmware/rosco_m68k.rom next to r68k)" << endl;
    out << "  -w        ROM writable (the board's flash write enable), as plain" << endl;
    out << "            memory: no flash commands. Default: writes ignored" << endl;
    out << "  -h        this help" << endl;
    out << "  R68K_RAM=5M  RAM size (default 1M, the on-board RAM)" << endl;
}

int main(int argc, char** argv) {
    signal(SIGUSR1, [](int) { dump_requested = 1; });

    // By hand rather than getopt(): BSD/macOS getopt stops at the first
    // non-option, and the options may also come after the binary
    bool emulateDuart = false;
    const char *romOption = NULL;
    bool romWritable = false;
    const char *binary = NULL;
    bool options = true;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (!options || arg.size() < 2 || arg[0] != '-') {
            if (binary != NULL) {
                cerr << "r68k: one binary only, got " << binary << " and " << arg << endl;
                return 1;
            }
            binary = argv[i];
        } else if (arg == "--") {
            options = false;
        } else if (arg == "-d") {
            emulateDuart = true;
        } else if (arg.compare(0, 2, "-r") == 0) {
            if (arg.size() > 2) {
                romOption = argv[i] + 2;
            } else if (i + 1 < argc) {
                romOption = argv[++i];
            } else {
                cerr << "r68k: -r needs a ROM file" << endl;
                return 1;
            }
        } else if (arg == "-w") {
            romWritable = true;
        } else if (arg == "-h") {
            usage(cout);
            return 0;
        } else {
            cerr << "r68k: unknown option " << arg << endl;
            usage(cerr);
            return 1;
        }
    }

    if (binary == NULL) {
        usage(cout);
        return 1;
    } else {
        ram_size = get_ram_size();
        try {
            __mem = new rosco::m68k::emu::AddressDecoder(ROM_SIZE, ram_size, rom_path(argv[0], romOption).string().c_str());
            __mem->LoadMemoryFile(0x40000, binary);
            __mem->setRomWritable(romWritable);
        } catch (const std::exception &e) {
            cerr << "r68k: " << e.what() << endl;
            return 1;
        }

        init_term();

        if (emulateDuart) {
            __duart = new rosco::m68k::emu::Duart([](bool asserted) {
                m68k_set_irq(asserted ? DUART_IRQ : 0);
            });
            __mem->setDuart(__duart);
        }

        r68k_prof_enabled = getenv("R68K_PROF") != NULL;
        r68k_trace_on = getenv("R68K_TRACE") != NULL || getenv("R68K_DUMPAT") != NULL
                       || r68k_prof_enabled;

        m68k_set_cpu_type(M68K_CPU_TYPE_68010);
        m68k_init();
        m68k_pulse_reset();

        if (emulateDuart) {
            r68k_bus_errors = true;     // after the reset: it reads outside m68k_execute

            while (1) {
                prof_acc += m68k_execute(10000);
                if (CPU_STOPPED & STOP_LEVEL_HALT) {
                    fprintf(stderr, "\n[r68k] CPU halted, exiting\n");
                    exit(1);
                }
                __duart->poll();
                check_dump();
            }
        }

        // Legacy tick without -d: 100Hz on vector 0x45, the request is
        // dropped again in interrupt_ack_handler
        const auto tick = std::chrono::milliseconds(10);
        auto nextTick = std::chrono::steady_clock::now() + tick;
        while (1) {
            prof_acc += m68k_execute(100000);

            auto now = std::chrono::steady_clock::now();
            if (now >= nextTick) {
                m68k_set_irq(DUART_IRQ);
                nextTick += tick;
                if (nextTick < now) nextTick = now + tick;
            }

            check_dump();
        }
    }
}
