//
// Created by ross.bamford on 01/04/2019.
//

#include <iostream>
#include "AddressDecoder.h"
#include "../musashi/m68kcpu.h"

#ifdef __cplusplus
extern "C" {
#endif

extern rosco::m68k::emu::AddressDecoder *__mem;

/* Is the CPU writing a bus/address error stack frame? (A bus error now
 * is a double fault.) Here, as m68kcpu.h cannot go with memoryglue.cpp. */
bool r68k_stacking_bus_error() {
    return CPU_RUN_MODE == RUN_MODE_BERR_AERR_RESET_WSF;
}

void resetMachineHandler() {
    __mem->reset();
}

void instructionHook() {
	m68ki_cpu_core ctx;
	m68k_get_context(&ctx);

	std::cout << "Execute instruction @ 0x" << std::hex << ctx.pc << " (Instruction word is 0x" << __mem->read16(ctx.pc) << ")" << std::endl;
}

#ifdef __cplusplus
}
#endif
