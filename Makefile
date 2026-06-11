CC      := gcc
CFLAGS  := -std=c99 -O2 -Wall -Wextra
BUILD   := build
SRC     := src

MACHINE_OBJS := $(BUILD)/z80.o $(BUILD)/machine.o $(BUILD)/video.o \
                $(BUILD)/tape.o $(BUILD)/snapshot.o $(BUILD)/keys.o \
                $(BUILD)/png.o $(BUILD)/wav.o $(BUILD)/disasm.o \
                $(BUILD)/debug.o $(BUILD)/sdl.o $(BUILD)/inflate.o $(BUILD)/ay.o \
                $(BUILD)/rzx.o $(BUILD)/main.o

# sdl.c dlopen()s the SDL2 runtime; -ldl covers pre-2.34 glibc.
LDLIBS  := -ldl

all: $(BUILD)/hc91emu $(BUILD)/zexrun $(BUILD)/ttest $(BUILD)/ctest \
     $(BUILD)/bordertap $(BUILD)/multitap $(BUILD)/fbcheck \
     $(BUILD)/tap2tzx $(BUILD)/cpmtap $(BUILD)/snowtap $(BUILD)/dtest $(BUILD)/sst

$(BUILD)/hc91emu: $(MACHINE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/zexrun: $(SRC)/zexrun.c $(SRC)/z80.c $(SRC)/z80.h
	$(CC) $(CFLAGS) -o $@ $(SRC)/zexrun.c $(SRC)/z80.c

$(BUILD)/ttest: tests/ttest.c $(BUILD)/z80.o
	$(CC) $(CFLAGS) -o $@ tests/ttest.c $(BUILD)/z80.o

$(BUILD)/ctest: tests/ctest.c $(BUILD)/z80.o
	$(CC) $(CFLAGS) -o $@ tests/ctest.c $(BUILD)/z80.o

$(BUILD)/bordertap: tests/bordertap.c
	$(CC) $(CFLAGS) -o $@ tests/bordertap.c

$(BUILD)/multitap: tests/multitap.c
	$(CC) $(CFLAGS) -o $@ tests/multitap.c

$(BUILD)/fbcheck: tests/fbcheck.c
	$(CC) $(CFLAGS) -o $@ tests/fbcheck.c

$(BUILD)/tap2tzx: tests/tap2tzx.c
	$(CC) $(CFLAGS) -o $@ tests/tap2tzx.c

$(BUILD)/snowtap: tests/snowtap.c
	$(CC) $(CFLAGS) -o $@ tests/snowtap.c

$(BUILD)/cpmtap: tests/cpmtap.c
	$(CC) $(CFLAGS) -o $@ tests/cpmtap.c

$(BUILD)/dtest: tests/dtest.c $(BUILD)/disasm.o
	$(CC) $(CFLAGS) -o $@ tests/dtest.c $(BUILD)/disasm.o

$(BUILD)/sst: tests/sst.c $(BUILD)/z80.o
	$(CC) $(CFLAGS) -o $@ tests/sst.c $(BUILD)/z80.o

$(BUILD)/%.o: $(SRC)/%.c $(SRC)/z80.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

test: all
	./tests/run_tests.sh

test-full: all
	RUN_ZEXALL=1 ./tests/run_tests.sh

clean:
	rm -f $(BUILD)/*.o $(BUILD)/hc91emu $(BUILD)/zexrun $(BUILD)/ttest \
	      $(BUILD)/ctest $(BUILD)/bordertap $(BUILD)/multitap \
	      $(BUILD)/fbcheck $(BUILD)/tap2tzx $(BUILD)/cpmtap $(BUILD)/snowtap $(BUILD)/dtest $(BUILD)/sst

.PHONY: all test test-full clean
