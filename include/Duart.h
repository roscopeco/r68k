//
// Minimal XR68C681 DUART model for r68k (rosco_m68k Classic v2 mainboard).
//
// Registers live on odd addresses from 0xF00001, register n at +2n.
// Channel A is connected to stdin/stdout, channel B transmit goes to stderr.
// The counter/timer runs in real time in timer mode; the interrupt output is
// level sensitive: (ISR & IMR) != 0 asserts IPL 4, the vector comes from IVR.
//

#ifndef ROSCOM68K_EMU_DUART_H
#define ROSCOM68K_EMU_DUART_H

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>

namespace rosco {
    namespace m68k {
        namespace emu {
            class Duart {
            public:
                static constexpr std::uint32_t BASE = 0x00F00000;
                static constexpr std::uint32_t END  = 0x00F00020;
                static constexpr unsigned int IRQ_LEVEL = 4;

                explicit Duart(std::function<void(bool)> setIrq);

                static bool contains(std::uint32_t address) {
                    return address >= BASE && address < END;
                }

                std::uint8_t read(std::uint32_t address);
                void write(std::uint32_t address, std::uint8_t data);

                // Called regularly from the main loop: timer and input.
                void poll();

                std::uint8_t vector() const { return ivr; }

            private:
                std::function<void(bool)> setIrq;
                std::deque<std::uint8_t> rxA;
                std::uint8_t mrA[2] = {0, 0}, mrB[2] = {0, 0};
                int mrPtrA = 0, mrPtrB = 0;
                std::uint8_t acr = 0, imr = 0, ivr = 0x0F;
                std::uint16_t preset = 0xFFFF;
                bool counterRunning = false, counterReady = false;
                std::chrono::steady_clock::time_point nextExpiry;

                std::uint8_t isr() const;
                void update();
                std::chrono::nanoseconds period() const;
            };
        }
    }
}

#endif //ROSCOM68K_EMU_DUART_H
