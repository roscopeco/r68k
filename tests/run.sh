#!/usr/bin/env bash
#
# r68k regression tests: tiny hand-assembled programs (loaded at 0x40000
# and run by the translator ROM), plus booting the real rosco_m68k
# firmware with -d when its ROMs are around.
#
#   tests/run.sh [path/to/r68k]
#
# R68K_REAL_ROMS: zip with the real ROMs (rosco_m68k.rom = 2.50.DEV,
# rosco_m68k_242.rom = 2.42), default ../rosco-emulator/roms/rosco_m68k_010.zip
# next to this checkout. Those tests are skipped when it is missing.

set -u

here=$(cd "$(dirname "$0")" && pwd)
r68k=$(cd "$(dirname "${1:-$here/../r68k}")" && pwd)/$(basename "${1:-r68k}")
real_roms=${R68K_REAL_ROMS:-$here/../../rosco-emulator/roms/rosco_m68k_010.zip}

# r68k's own ROM, for -r
romfile=$(dirname "$r68k")/firmware/rosco_m68k.rom

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

passed=0
failed=0
skipped=0

ok()   { echo "ok    $1"; passed=$((passed + 1)); }
fail() { echo "FAIL  $1: $2"; failed=$((failed + 1)); }
skip() { echo "skip  $1: $2"; skipped=$((skipped + 1)); }

# bin <file> <hex>: write the program bytes
bin() {
    printf "$(echo "$2" | tr -d ' ' | sed 's/../\\x&/g')" > "$1"
}

# run <secs> <args...>: r68k with a time limit, stdout to out, stderr to err;
# sets rc
run() {
    local secs=$1
    shift
    timeout "$secs" "$r68k" "$@" > out 2> err
    rc=$?
}

# expect <name> <rc> [stdout grep] [stderr grep]
expect() {
    local name=$1 want=$2
    if [ "$rc" != "$want" ]; then
        fail "$name" "exit status $rc, expected $want"
    elif [ -n "${3:-}" ] && ! grep -q -- "$3" out; then
        fail "$name" "stdout lacks '$3'"
    elif [ -n "${4:-}" ] && ! grep -q -- "$4" err; then
        fail "$name" "stderr lacks '$4'"
    else
        ok "$name"
    fi
}

# Firmware trap: move.l #$F0F0F0nn,d7 / move.l #$AA55AA55,d6 / illegal
trap_() { echo "2e3c f0f0 f0$1 2c3c aa55 aa55 4afc"; }
EXIT=$(trap_ f3)

# --- translator ROM ------------------------------------------------------

bin exit.bin "$EXIT"
run 5 exit.bin < /dev/null
expect "exit trap" 0

# moveq #42,d0 / move.l d0,-(a7) x2 / exit: the code is at 4(a7)
bin exit42.bin "702a 2f00 2f00 $EXIT"
run 5 exit42.bin < /dev/null
expect "exit status from the stack" 42

# moveq #'H',d0 / printchar / moveq #'i',d0 / printchar / exit
bin print.bin "7048 $(trap_ f2) 7069 $(trap_ f2) $EXIT"
run 5 print.bin < /dev/null
expect "printchar trap" 0 "^Hi$"

# move.b #'A',$F00007 / move.b #10,$F00007 (THRA) / exit
bin duart.bin "13fc 0041 00f0 0007 13fc 000a 00f0 0007 $EXIT"
run 5 -d duart.bin < /dev/null
expect "-d: DUART channel A output" 0 "^A$"
run 5 duart.bin < /dev/null
if [ "$rc" = 0 ] && [ ! -s out ]; then ok "no -d: DUART not there"; else fail "no -d: DUART not there" "rc $rc, output '$(cat out)'"; fi

# -d: the ROM's memory sizing runs into bus errors past the RAM, its
# handler sets RR and returns: still has to get to the program
run 5 -d exit.bin < /dev/null
expect "-d: memory sizing with bus errors" 0

# move.l $A00000,d0: nothing there, the ROM's default bus error handler
# is in place
bin berr.bin "2039 00a0 0000 $EXIT"
run 5 -d berr.bin < /dev/null
expect "-d: bus error exits 128+SIGBUS" $((128 + $(kill -l BUS))) "" "bus error at 00A00000"
run 5 berr.bin < /dev/null
expect "no -d: no bus error" 0

# Address errors (always, they are the CPU's): a word or long access at
# an odd address, or an odd PC. Bytes may be anywhere.
sigbus=$((128 + $(kill -l BUS)))
# lea $1001.w,a0 / move.l d0,(a0) / exit
bin aerr_wl.bin "41f8 1001 2080 $EXIT"
run 5 aerr_wl.bin < /dev/null
expect "address error: move.l to an odd address" $sigbus "" "address error: write at 00001001"
run 5 -d aerr_wl.bin < /dev/null
expect "-d: address error too" $sigbus "" "address error: write at 00001001"
# move.w $1001.w,d0 / exit
bin aerr_rw.bin "3038 1001 $EXIT"
run 5 aerr_rw.bin < /dev/null
expect "address error: move.w from an odd address" $sigbus "" "address error: read at 00001001"
# jmp $40001
bin aerr_pc.bin "4ef9 0004 0001"
run 5 aerr_pc.bin < /dev/null
expect "address error: odd PC" $sigbus "" "address error: read at 00040001"
# move.b d0,$1001.w / exit
bin oddbyte.bin "11c0 1001 $EXIT"
run 5 oddbyte.bin < /dev/null
expect "byte at an odd address is fine" 0
# move.l #handler,$C.w / lea $1001.w,a0 / move.l d0,(a0) / nop x2 /
# handler: move.w 6(a7),d0 / lsr.w #8,d0 / lsr.w #4,d0 / and.l #$F,d0 /
# push x2 / exit: the 68010 frame format, 8
bin aerr_frame.bin "21fc 0004 0012 000c 41f8 1001 2080 4e71 4e71 302f 0006 e048 e848 0280 0000 000f 2f00 2f00 $EXIT"
run 5 aerr_frame.bin < /dev/null
expect "address error handler gets a format 8 frame" 8

# moveq #0,d7 / illegal: a real illegal instruction, the ROM's halting
# handler is still in place
bin illegal.bin "7e00 4afc"
run 5 illegal.bin < /dev/null
expect "illegal instruction exits 132" 132 "" "no handler: exiting"

# the trap magic with another illegal opcode ($4AFB) is not a trap
bin badop.bin "2e3c f0f0 f0f3 2c3c aa55 aa55 4afb"
run 5 badop.bin < /dev/null
expect "trap magic needs ILLEGAL (4AFC)" 132 "" "illegal instruction 4AFB"

# read_char / move.l d0,-(a7) x2 / exit: the character is the status
bin readchar.bin "$(trap_ f5) 2f00 2f00 $EXIT"
rc=0; echo x | timeout 5 "$r68k" readchar.bin > out 2> err || rc=$?
expect "read_char" 120
run 5 readchar.bin < /dev/null
expect "read_char at end of input" 0 "" "end of input"

# move.w #$2000,sr / move.l #40000000,d0 / 1: subq.l #1,d0 / bne 1b /
# GETUPTICKS / move.l d1,-(a7) x2 / exit: some 100Hz ticks have passed
bin ticks.bin "46fc 2000 203c 0262 5a00 5380 66fc $(trap_ d8) 2f01 2f01 $EXIT"
run 30 ticks.bin < /dev/null
if [ "$rc" -ge 1 ] && [ "$rc" -le 255 ]; then ok "uptime ticks ($rc)"; else fail "uptime ticks" "exit status $rc"; fi

# bra.s *: SIGUSR1 dumps the CPU, in both modes
bin loop.bin "60fe"
for mode in "" "-d"; do
    "$r68k" $mode loop.bin < /dev/null > out 2> err &
    pid=$!
    sleep 1
    kill -USR1 $pid
    sleep 1
    kill $pid
    wait $pid 2> /dev/null
    if grep -q "PC=00040000" err; then ok "SIGUSR1 dump ${mode:-without -d}"; else fail "SIGUSR1 dump ${mode:-without -d}" "no dump"; fi
done

# move.l #$23C00000,d0 / move.l d0,$1000 / exit: the immediate looks like
# an instruction, a disassembly from 0x40 bytes back goes across the writer
bin watch.bin "203c 23c0 0000 23c0 0000 1000 $EXIT"
R68K_WATCH=1000-1004 run 5 watch.bin < /dev/null
if [ "$rc" = 0 ] && grep -q "write 4 bytes 23C00000 to 00001000 at PC 00040006" err &&
   grep -q "00040000  move.l  #\$23c00000, D0" err && grep -q "00040006  move.l  D0, \$1000" err; then
    ok "R68K_WATCH"
else
    fail "R68K_WATCH" "rc $rc, $(grep -c . err) lines on stderr"
fi

# exit.bin: the second instruction is at 0x40006
R68K_DUMPAT=40006 run 5 exit.bin < /dev/null
expect "R68K_DUMPAT" 0 "" "reached 00040006"

# move.b #$5A,$E00100 / move.l #$A5A5A5A5,$E00104 / move.b $E00100,d0 /
# add.b $E00107,d0 / and.l #$FF,d0 / push x2 / exit: $5A + $A5 = $FF with
# the ROM writable, else the ROM's own bytes ($00 + $8A here)
bin romwrite.bin "13fc 005a 00e0 0100 23fc a5a5 a5a5 00e0 0104 1039 00e0 0100 d039 00e0 0107 0280 0000 00ff 2f00 2f00 $EXIT"
run 5 romwrite.bin < /dev/null
if [ "$rc" != 255 ] && [ "$rc" != 124 ]; then ok "ROM read-only"; else fail "ROM read-only" "exit status $rc"; fi
run 5 -d romwrite.bin < /dev/null
if [ "$rc" != 255 ] && [ "$rc" != 124 ]; then ok "-d: ROM read-only, no bus error"; else fail "-d: ROM read-only, no bus error" "exit status $rc"; fi
run 5 romwrite.bin -w < /dev/null
expect "-w: ROM writable" 255

# options: anywhere, -r with or without a space, --, errors
run 5 duart.bin -d < /dev/null
expect "-d after the binary" 0 "^A$"
run 5 -r "$romfile" -d duart.bin < /dev/null
expect "-r <rom>" 0 "^A$"
run 5 duart.bin "-r$romfile" -d < /dev/null
expect "-r<rom> after the binary" 0 "^A$"
R68K_ROM=nonexistent.rom run 5 -r "$romfile" exit.bin < /dev/null
expect "-r over R68K_ROM" 0
run 5 -r nonexistent.rom exit.bin < /dev/null
expect "-r with a missing ROM" 1 "" "cannot open nonexistent.rom"
cp exit.bin ./-x.bin
run 5 -- -x.bin < /dev/null
expect "-- ends the options" 0
run 5 -x exit.bin < /dev/null
expect "unknown option" 1 "" "unknown option -x"
run 5 exit.bin exit42.bin < /dev/null
expect "two binaries" 1 "" "one binary only"
run 5 exit.bin -r < /dev/null
expect "-r without a file" 1 "" "-r needs a ROM file"
run 5 -h < /dev/null
expect "-h" 0 "^Usage: r68k"

# R68K_PROF: move.b #1,$FFFFF0 / moveq #100,d0 / 1: subq.l #1,d0 / bne 1b /
# move.b #2,$FFFFF0 / exit: the loop's subq at 0x4000A ran 100 times
bin prof.bin "13fc 0001 00ff fff0 7064 5380 66fc 13fc 0002 00ff fff0 $EXIT"
R68K_PROF=prof.txt run 5 prof.bin < /dev/null
if [ "$rc" = 0 ] && grep -q "^# run 1 cycles" prof.txt 2> /dev/null && grep -q "^04000A [0-9]* 100$" prof.txt; then
    ok "R68K_PROF profile"
else
    fail "R68K_PROF profile" "rc $rc, $(cat prof.txt 2> /dev/null | head -3)"
fi
# Shift timing (68010: 6+2n byte/word, 8+2n long), from the profile:
# start / moveq #8,d1 / lsl.w #8,d0 / lsr.w #1,d0 / lsl.l #8,d0 /
# rol.b #3,d0 / lsl.l d1,d0 / stop / exit
bin shifts.bin "13fc 0001 00ff fff0 7208 e148 e248 e188 e718 e3a8 13fc 0002 00ff fff0 $EXIT"
rm -f shifts.txt
R68K_PROF=shifts.txt run 5 shifts.bin < /dev/null
got=$(awk '$1 ~ /^04001[02]$|^04000[ACE]$/ { printf "%s=%s ", $1, $2 }' shifts.txt 2> /dev/null)
if [ "$got" = "04000A=22 04000C=8 04000E=24 040010=12 040012=24 " ]; then
    ok "shift cycles (immediate and register count)"
else
    fail "shift cycles (immediate and register count)" "got: $got"
fi

run 5 -d prof.bin < /dev/null
expect "-d without R68K_PROF: 0xFFFFF0 is a bus error" $((128 + $(kill -l BUS))) "" "bus error at 00FFFFF0"

run 5 missing.bin < /dev/null
expect "missing program" 1 "" "cannot open missing.bin"

head -c 2000000 /dev/zero > big.bin
run 5 big.bin < /dev/null
expect "program too large" 1 "" "does not fit"

R68K_RAM=700K run 5 exit.bin < /dev/null
expect "bad R68K_RAM" 1 "" "R68K_RAM must be"

R68K_RAM=5MB run 5 exit.bin < /dev/null
expect "R68K_RAM with trailing garbage" 1 "" "R68K_RAM must be"

R68K_RAM=2M run 5 exit.bin < /dev/null
expect "R68K_RAM=2M" 0

# --- real firmware, -d ---------------------------------------------------

# boot <name> <rom> <greps...>: wait for the Kermit prompt (it waits
# there for ever), then check the boot messages
boot() {
    local name=$1 rom=$2
    shift 2
    R68K_ROM=$rom "$r68k" -d exit.bin < /dev/null > out 2> err &
    local pid=$! i
    for i in $(seq 100); do
        grep -q "Ready for Kermit receive" out && break
        kill -0 $pid 2> /dev/null || break
        sleep 0.1
    done
    kill $pid 2> /dev/null
    wait $pid 2> /dev/null

    for want in "Ready for Kermit receive" "$@"; do
        if ! grep -q -- "$want" out; then
            fail "$name" "no '$want' in the boot output"
            return
        fi
    done
    ok "$name"
}

if [ ! -f "$real_roms" ]; then
    skip "real firmware" "no $real_roms"
elif ! unzip -o -j -q "$real_roms" rosco_m68k_242.rom '*/rosco_m68k.rom' -d roms 2> /dev/null; then
    skip "real firmware" "cannot unzip $real_roms"
else
    boot "firmware 2.42 boots" roms/rosco_m68k_242.rom "Classic.*2\.42" "MC68010" "1048576 bytes RAM" "No IDE interface found"
    boot "firmware 2.50.DEV boots" roms/rosco_m68k.rom "Classic.*2\.50" "MC68010" "1048576 bytes RAM"
fi

echo
echo "$passed passed, $failed failed, $skipped skipped"
[ "$failed" = 0 ]
