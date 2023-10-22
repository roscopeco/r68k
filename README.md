# r68k

Run native rosco_m68k binaries on your modern computer.

> **Note**: This is not a complete "emulation" or translation layer
> for a full rosco_m68k system. It implements enough of the system
> to run many general purpose programs, and is great for testing /
> iterating on a thing without having to upload to a real machine. 

## Build it

You'll need the rosco_m68k toolchain installed, as well as build tools
for your platform.

You'll also need to have the rosco_m68k codebase checked out somewhere, 
with the standard libs build and installed.

```shell
ROSCO_M68K_DIR=/path/to/your/rosco_m68k make clean all
```

## Run it

```shell
./r68k <rosco_m68k binary file>
```

## That's it

Fin.
