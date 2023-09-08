# r68k

Run native rosco_m68k binaries on your modern computer.

> **Note**: This is barely even a toy at this point, and may not go any further.
> It's fun though 😅

## Build it

You'll need the rosco_m68k toolchain installed, as well as a relatively-recent
`cmake` and build tools for your platform.

You'll also need to have the rosco_m68k codebase checked out somewhere, 
with the standard libs build and installed.

```shell
cd firmware
make all
cd ../helloworld
ROSCO_M68K_DIR=/path/to/your/rosco_m68k make all
cd ../musashi
make
cd ..
cmake .
make all
```

## Run it

```shell
./r68k helloworld/helloworld.bin
```

## That's it

Fin.
