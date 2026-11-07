# Make r68k 
# (c) 2023 Ross Bamford & Contribs

CLEAN_FILES=r68k *.o *.d rosco_m68k_glue/*.o rosco_m68k_glue/*.d machine/*.o machine/*.d
R68K_OBJS=machine/AddressDecoder.o machine/Duart.o machine/Memory.o rosco_m68k_glue/cpuglue.o rosco_m68k_glue/memoryglue.o main.o
MUSASHI_OBJS=musashi/m68kcpu.o musashi/m68kdasm.o musashi/m68kops.o musashi/softfloat/softfloat.o
ROM_BINARY=firmware/rosco_m68k.rom
CXXFLAGS=-O2 -Wall -Wextra -Wpedantic -Iinclude
DEPFLAGS=-MMD -MP

.PHONY: clean all musashi check

all: r68k $(ROM_BINARY)

check: r68k $(ROM_BINARY)
	tests/run.sh ./r68k

clean:
	rm -rf $(CLEAN_FILES)
	$(MAKE) -C musashi clean
	$(MAKE) -C firmware clean

r68k: $(MUSASHI_OBJS) $(R68K_OBJS)
	$(CXX) -o $@ $^

# musashi/Makefile knows its own dependencies (m68kconf.h, ...): always
# ask it, the timestamps then decide whether r68k needs relinking.
$(MUSASHI_OBJS): musashi ;

musashi:
	$(MAKE) -C musashi

# Only when the sources changed, so a prebuilt ROM is fine without the
# m68k toolchain.
$(ROM_BINARY): $(wildcard firmware/*.asm firmware/*.ld firmware/debug_stub/*.asm)
	$(MAKE) -C firmware rosco_m68k.rom

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -std=c++20 -c -o $@ $<

-include $(R68K_OBJS:.o=.d)

