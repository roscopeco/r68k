//
// Created by ross.bamford on 01/04/2019.
//

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include "AddressDecoder.h"
#include "../musashi/m68k.h"

#ifdef __cplusplus
extern "C" {
#endif

extern rosco::m68k::emu::AddressDecoder *__mem;
bool r68k_stacking_bus_error();     /* cpuglue.cpp */

/* Bus errors (-d): when on, a CPU access where nothing is mapped takes
 * the bus error exception, as the real hardware's /BERR does. r68k's own
 * peeks at memory (firmware traps, debug aids) raise r68k_no_bus_errors
 * around them, they must never fault. */
bool r68k_bus_errors = false;
int r68k_no_bus_errors = 0;

static void check_bus(unsigned int address, unsigned int size) {
    if (r68k_bus_errors && r68k_no_bus_errors == 0 && !__mem->isMapped(address, size)) {
        if (r68k_stacking_bus_error()) {
            /* Bus error while stacking a bus error: the CPU halts.
             * (Musashi's own double fault path reads memory, and would
             * come back here.) */
            fprintf(stderr, "\n[r68k] double bus fault at %08X, CPU halted\n", address);
            m68k_pulse_halt();
            return;
        }

        /* Vector 2 still the default from the ROM's vector table: that
         * handler halts for ever, report and stop here instead, with the
         * exit status of a SIGBUS. (Probes install their own handler.) */
        unsigned int vbr = m68k_get_reg(NULL, M68K_REG_VBR);
        if (__mem->read32(vbr + 2 * 4) == __mem->read32(0xE00000 + 2 * 4)) {
            fprintf(stderr, "\n[r68k] bus error at %08X, PC %08X, no handler: exiting\n",
                    address, m68k_get_reg(NULL, M68K_REG_PPC));
            exit(128 + SIGBUS);
        }

        m68k_pulse_bus_error();     /* does not return */
    }
}

/* Read from anywhere */
unsigned int  m68k_read_memory_8(unsigned int address) {
    check_bus(address, 1);
    return __mem->read8(address);
}

unsigned int  m68k_read_memory_16(unsigned int address) {
    check_bus(address, 2);
    return __mem->read16(address);
}

unsigned int  m68k_read_memory_32(unsigned int address) {
    check_bus(address, 4);
    return __mem->read32(address);
}

/* Read data immediately following the PC */
unsigned int  m68k_read_immediate_16(unsigned int address) {
    check_bus(address, 2);
    return __mem->read16(address);
}

unsigned int  m68k_read_immediate_32(unsigned int address) {
    check_bus(address, 4);
    return __mem->read32(address);
}

/* Read data relative to the PC */
unsigned int  m68k_read_pcrelative_8(unsigned int address) {
    check_bus(address, 1);
    return __mem->read8(address);
}

unsigned int  m68k_read_pcrelative_16(unsigned int address) {
    check_bus(address, 2);
    return __mem->read16(address);
}

unsigned int  m68k_read_pcrelative_32(unsigned int address) {
    check_bus(address, 4);
    return __mem->read32(address);
}

/* Memory access for the disassembler: never a bus error */
unsigned int m68k_read_disassembler_8  (unsigned int address) {
    return __mem->read8(address);
}

unsigned int m68k_read_disassembler_16 (unsigned int address) {
    return __mem->read16(address);
}

unsigned int m68k_read_disassembler_32 (unsigned int address) {
    return __mem->read32(address);
}

/* Debugging aid: R68K_WATCH=lo-hi[,plo-phi] (hex) reports the first few
 * writes into [lo, hi), with the PC of the instruction that wrote, except
 * writes done by code in [plo, phi) (e.g. the loader's copier). */
static void watch(unsigned int address, unsigned int size, unsigned int value) {
    static int init, count;
    static unsigned long lo, hi, plo, phi;
    if (!init) {
        const char *w = getenv("R68K_WATCH");
        init = 1;
        int n = w ? sscanf(w, "%lx-%lx,%lx-%lx", &lo, &hi, &plo, &phi) : 0;
        if (n < 2)
            lo = hi = 0;
        if (n == 3) {
            fprintf(stderr, "[r68k] R68K_WATCH: the excluded range needs plo-phi, ignored\n");
            plo = phi = 0;
        }
    }
    if (address + size > lo && address < hi && count < 8) {
        unsigned int pc = m68k_get_reg(NULL, M68K_REG_PPC);
        if (pc >= plo && pc < phi) return;
        count++;
        fprintf(stderr, "\n[r68k] write %u bytes %08X to %08X at PC %08X SR %04X\n",
                size, value, address, pc, m68k_get_reg(NULL, M68K_REG_SR));
        if (count == 1) {	/* the code around the first writer */
            /* Up to 0x40 bytes before it: the earliest start from which
             * the disassembly gets to pc itself, not across it */
            char buf[100];
            unsigned int start = pc, a, n;
            for (unsigned int s = pc < 0x40 ? 0 : pc - 0x40; s < pc; s += 2) {
                for (a = s; a < pc; a += m68k_disassemble(buf, a, M68K_CPU_TYPE_68010))
                    ;
                if (a == pc) {
                    start = s;
                    break;
                }
            }
            for (a = start; a < pc + 0x10; a += n) {
                n = m68k_disassemble(buf, a, M68K_CPU_TYPE_68010);
                fprintf(stderr, "[r68k]   %08X  %s\n", a, buf);
            }
        }
    }
}

/* Write to anywhere */
void r68k_prof_ctl(unsigned int v);   /* main.cpp */
extern bool r68k_prof_enabled;

void m68k_write_memory_8(unsigned int address, unsigned int value) {
    if (r68k_prof_enabled && address == 0xFFFFF0) {     /* profiling control, R68K_PROF */
        r68k_prof_ctl(value & 0xFF);
        return;
    }
    check_bus(address, 1);
    watch(address, 1, value);
    __mem->write8(address, static_cast<uint8_t>(value & 0xFF));

}

void m68k_write_memory_16(unsigned int address, unsigned int value) {
    check_bus(address, 2);
    watch(address, 2, value);
    __mem->write16(address, static_cast<uint16_t>(value & 0xFFFF));

}

void m68k_write_memory_32(unsigned int address, unsigned int value) {
    check_bus(address, 4);
    watch(address, 4, value);
    __mem->write32(address, value);
}

#ifdef __cplusplus
}
#endif
