#include <iostream>
#include <filesystem>
#include <stdint.h>
#include <unistd.h>
#include <thread>
#include <atomic>

#include "musashi/m68k.h"
#include "musashi/m68kcpu.h"
#include "AddressDecoder.h"

using namespace std;

#define DUART_IRQ	4
#define DUART_VEC	0x45

extern "C" {
	rosco::m68k::emu::AddressDecoder* __mem;

	int illegal_instruction_handler(int opcode) {
		m68ki_cpu_core ctx;
		m68k_get_context(&ctx);

		uint32_t d7 = m68k_get_reg(&ctx, M68K_REG_D7);
		uint32_t d6 = m68k_get_reg(&ctx, M68K_REG_D6);
		uint32_t a0 = m68k_get_reg(&ctx, M68K_REG_A0);

		if (opcode == 0x4afc && (d7 & 0xFFFFFFF0) == 0xF0F0F0F0 && d6 == 0xAA55AA55) {
			// It's a trap!

			uint8_t op = d7 & 0x0000000F;
			uint8_t c;

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
					c = m68k_read_memory_8(a0++);
					if (c) {
					cout << c;
					}

					break;
				case 3:
					// prog_exit
					m68k_pulse_halt();

					break;					
				default:
					cerr << "<UNKNOWN TRAP D7=0x" << hex << d7 << "; D6=0x" << d6 << ": IGNORED>" << endl;
			}
		}

		return 1;
	}

	int interrupt_ack_handler(unsigned int irq) {
		switch (irq) {
		case DUART_IRQ:
			// DUART timer tick - vector to 0x45
			m68k_set_irq(0);
			return DUART_VEC;
		default:
			cerr << "WARN: Unexpected IRQ " << irq << "; Autovectoring, but machine will probably lock up!" << endl;
			return M68K_INT_ACK_AUTOVECTOR;
		}
	}
}

std::atomic_bool is_done;

void timer_interrupt() {
	int i = 100;

	while (!is_done) {
		if (i++ == 100) {
			m68k_set_irq(DUART_IRQ);
			i = 0;
		}

		usleep(100);
	}
}

int main(int argc, char** argv) {
	if (argc != 2) {
		cout << "Usage: xl86 <binary>" << endl;
		return 1;
	} else {
		std::filesystem::path path = std::filesystem::path(argv[0]).parent_path();
		path += "/firmware/rosco_m68k.rom";

		__mem = new rosco::m68k::emu::AddressDecoder(0x40000, 0x100000, path.string().c_str());
		__mem->LoadMemoryFile(0x40000, argv[1]);

		m68k_set_cpu_type(M68K_CPU_TYPE_68010);
		m68k_init();
		m68k_pulse_reset();

		m68ki_cpu_core ctx;
		m68k_get_context(&ctx);

#ifdef	DEBUG
		cout << "Address mask: 0x" << hex << ctx.address_mask << endl;
		cout << "Cpu type    : 0x" << ctx.cpu_type << endl << endl;

		uint32_t memWord = m68k_read_memory_32(0x40000);
		uint32_t memWord2 = m68k_read_memory_32(0xfc0004);
		uint32_t memWord3 = m68k_read_memory_32(0);

		uint32_t biosMagic = m68k_read_memory_32(0x400);
		uint32_t oshiCode = m68k_read_memory_32(0x404);

		cout << "PC          : 0x" << m68k_get_reg(&ctx, M68K_REG_PC) << endl;
		cout << "BIOS Magic  : 0x" << biosMagic << endl;
		cout << "OSHI Code   : 0x" << oshiCode << endl;
		cout << "mem[0]      : 0x" << memWord3 << endl;
		cout << "mem[0x40000]: 0x" << memWord << endl;
		cout << "mem[fc0004] : 0x" << memWord2 << endl;
		cout << "SR          : 0x" << m68k_get_reg(&ctx, M68K_REG_SR) << endl;
		cout << "D0          : 0x" << m68k_get_reg(&ctx, M68K_REG_D0) << endl;
		cout << "D7          : 0x" << m68k_get_reg(&ctx, M68K_REG_D7) << endl << endl;
#		endif

		std::thread timer_thread(timer_interrupt);
		int cycles = 0;
		while (!ctx.stopped) {
			cycles += m68k_execute(100000);
			m68k_get_context(&ctx);
		}

		is_done = true;
		timer_thread.join();

#ifdef	DEBUG
		cout << endl;
		cout << "Executed    : " << dec << cycles << " cycle(s)" << hex << endl << endl;

		memWord = m68k_read_memory_32(0xfc0000);
		memWord2 = m68k_read_memory_32(0xfc0004);
		memWord3 = m68k_read_memory_32(0);

		biosMagic = m68k_read_memory_32(0x400);
		oshiCode = m68k_read_memory_32(0x404);

		cout << "PC          : 0x" << m68k_get_reg(&ctx, M68K_REG_PC) << endl;
		cout << "BIOS Magic  : 0x" << biosMagic << endl;
		cout << "OSHI Code   : 0x" << oshiCode << endl;
		cout << "mem[0]      : 0x" << memWord3 << endl;
		cout << "mem[fc0000] : 0x" << memWord << endl;
		cout << "mem[fc0004] : 0x" << memWord2 << endl;
		cout << "SR          : 0x" << m68k_get_reg(&ctx, M68K_REG_SR) << endl;
		cout << "D0          : 0x" << m68k_get_reg(&ctx, M68K_REG_D0) << endl;
		cout << "D7          : 0x" << m68k_get_reg(&ctx, M68K_REG_D7) << endl;
#		endif

		delete(__mem);
		return 0;
	}
}
