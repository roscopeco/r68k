# r68k

Run native rosco_m68k binaries on your modern computer.

> **Note**: This is not a complete "emulation" or translation layer
> for a full rosco_m68k system. It implements enough of the system
> to run many general purpose programs, and is great for testing /
> iterating on a thing without having to upload to a real machine. 

> **Also note**: This code is horrific by literally any standard 
> you might care to mention. It grew out of a hack, and happened to 
> become useful. 
>
> We only use it for dev and in our build for running tests, so sorry,
> not sorry.

## Build it

You'll need the rosco_m68k toolchain (or other m68k toolchain of
your choosing) installed, as well as build tools for your platform.

```
make clean all
```

The m68k toolchain (`vasmm68k_mot`, `m68k-elf-*`) is only needed for
the firmware ROM: once `firmware/rosco_m68k.rom` exists, `make` only
rebuilds it when its sources change.

To run the tests (no m68k toolchain needed):

```
make check
```

The tests also boot the real rosco_m68k firmware if its ROMs are around,
see `R68K_REAL_ROMS` in `tests/run.sh`.

## Run it

```shell
./r68k [-d] [-r <rom>] [-w] <rosco_m68k binary file>
```

The options can also come after the file; `--` ends them.

The program is loaded at `0x40000` and run by r68k's own small
firmware, which hands console and SD card I/O to r68k.

`-d` adds a model of the XR68C681 DUART (rosco_m68k Classic v2) at
`0xF00000`, with its timer and interrupts. Channel A is stdin/stdout,
channel B transmit goes to stderr. With `-d`, an access where nothing is
mapped is a bus error, as on the real hardware. This is enough to boot
the real rosco_m68k firmware:

```shell
./r68k -d -r rosco_m68k.rom whatever.bin
```

The ROM is read-only, writes to it are ignored, like the board's flash
with its write enable off. `-w` makes it writable, as plain memory: the
flash chip's program and erase commands are not emulated.

Like a real 68010, a word or long access at an odd address, or a jump
to one, is an address error (vector 3, format 8 frame), with or
without `-d`.

r68k exits with the program's exit code. Other exit codes:

| Code | Why |
|------|-----|
| 1    | a bad option, the ROM or program could not be loaded, or a bad `R68K_RAM` |
| 132  | an illegal instruction, with no handler installed (128 + SIGILL) |
| 128 + SIGBUS | `-d`: a bus error, with no handler installed |
| 128 + SIGBUS | an address error (word or long access at an odd address, or an odd PC), with no handler installed |
| 0    | end of input while the program waits for a character |

### Settings

| Environment          | What |
|----------------------|------|
| `R68K_RAM=5M`        | RAM size, a multiple of 512K up to 14M (`K` and `M` suffixes). Default 1M, the on-board RAM. |
| `R68K_ROM=<file>`    | ROM image, up to 1MB, when there is no `-r`. Default `firmware/rosco_m68k.rom` next to `r68k`. |

The SD card is the file `rosco_sd.bin` in the current directory, if it
exists.

### Debugging

| | What |
|-|------|
| `kill -USR1 <pid>`         | Dumps the registers, the next few instructions and the PC trace (below) to stderr, and the RAM to `r68k-ram.bin`. |
| `R68K_TRACE=1`             | Keeps the last 4096 PCs for that dump. It stops at the first odd PC, a PC outside RAM and ROM, or two zero opcodes in a row, so the trace shows how it got there. |
| `R68K_DUMPAT=<hex pc>`     | The same dump, the first time the PC gets there. |
| `R68K_WATCH=lo-hi[,plo-phi]` | Reports the first 8 writes into `[lo, hi)` (hex), with the PC and code of the first writer. Writes by code in `[plo, phi)` are left out. |
| `R68K_PROF=<file>`        | Cycle profiler: the program writes the byte 1 to `0xFFFFF0` to start and 2 to stop. Appends the cycles and instruction counts per address in RAM to the file. Without it, `0xFFFFF0` is not special. |

### Embedding

`machine/Duart.cpp` has a second console path for programs that embed
r68k with their own `main()`, a web page (`__EMSCRIPTEN__`) or a hosted
console program (`-DR68K_HOSTED`, which provides
`r68k_host_output()`): channel A output is collected for `r68k_flush()`,
keys come in through `r68k_key()`, and missed timer ticks are made up.
See `src68k/web` and `src68k/host` in bbc-basic-68k.

## That's it

Fin.
