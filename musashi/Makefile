# Just a basic makefile to quickly test that everyting is working, it just
# compiles the .o and the generator

MUSASHIFILES     = m68kcpu.c m68kdasm.c softfloat/softfloat.c
MUSASHIGENCFILES = m68kops.c
MUSASHIGENHFILES = m68kops.h
MUSASHIGENERATOR = m68kmake

EXE =
EXEPATH = ./

.CFILES   = $(MAINFILES) $(OSDFILES) $(MUSASHIFILES) $(MUSASHIGENCFILES)
.OFILES   = $(.CFILES:%.c=%.o)

CC        = gcc
WARNINGS  = -Wall -Wextra -pedantic
CFLAGS    = -O2 $(WARNINGS)
LFLAGS    = $(WARNINGS)

DELETEFILES = $(MUSASHIGENCFILES) $(MUSASHIGENHFILES) $(.OFILES) $(TARGET) $(MUSASHIGENERATOR)$(EXE)


all: $(.OFILES)

clean:
	rm -f $(DELETEFILES)

m68kcpu.o: $(MUSASHIGENHFILES) m68kfpu.c m68kmmu.h softfloat/softfloat.c softfloat/softfloat.h

# Everything sees the configuration (m68kconf.h) through m68k.h
$(.OFILES): m68k.h m68kconf.h
m68kcpu.o m68kops.o softfloat/softfloat.o: m68kcpu.h softfloat/milieu.h softfloat/softfloat.h
m68kops.o: $(MUSASHIGENHFILES)
softfloat/softfloat.o: softfloat/softfloat-macros softfloat/softfloat-specialize

$(MUSASHIGENCFILES) $(MUSASHIGENHFILES): $(MUSASHIGENERATOR)$(EXE) m68k_in.c
	$(EXEPATH)$(MUSASHIGENERATOR)$(EXE)

$(MUSASHIGENERATOR)$(EXE):  $(MUSASHIGENERATOR).c
	$(CC) -o  $(MUSASHIGENERATOR)$(EXE)  $(MUSASHIGENERATOR).c
