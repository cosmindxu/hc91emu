# ZX-CHESS — build the bootable tape and run a smoke test.
#
#   make            assemble chess.bin and wrap chess.tap
#   make test       boot the tape headlessly and golden-check the board
#   make play       launch in the interactive SDL window (needs SDL2)
#
# Requires: pasmo (apt-get install pasmo) and python3.  The smoke test
# additionally needs a built ../../build/hc91emu and roms/48.rom
# (tools/get_roms.sh).

PASMO  ?= pasmo
PY     ?= python3
EMU    ?= ../../build/hc91emu
ROM    ?= ../../roms/48.rom
ORG    ?= 0x8000

SRC = chess.asm movegen.inc engine.inc pieces.inc

all: chess.tap

pieces.inc: pieces.py
	$(PY) pieces.py > pieces.inc

chess.bin: $(SRC)
	$(PASMO) --bin chess.asm chess.bin

chess.tap: chess.bin ../../tools/zxtap.py
	$(PY) ../../tools/zxtap.py chess.bin chess.tap --org $(ORG) --name CHESS

test: chess.tap
	@$(EMU) --machine 48k --rom $(ROM) chess.tap --autoload --turbo \
		--frames 1000 --screenshot /tmp/chess_board.png >/dev/null 2>&1
	@if [ "`md5sum < /tmp/chess_board.png`" = "`md5sum < initial_golden.png`" ]; then \
		echo "chess: initial board OK (golden match)"; \
	else \
		echo "chess: FAIL - board screenshot differs from golden"; exit 1; \
	fi
	@# play e2-e4 and confirm the engine replies (status returns to 'Your move')
	@$(EMU) --machine 48k --rom $(ROM) chess.tap --autoload --turbo \
		--keys "960:ENTER" --keys "985:Q" --keys "1010:Q" --keys "1035:ENTER" \
		--frames 4000 --text > /tmp/chess_play.txt 2>&1
	@if grep -q "Your move" /tmp/chess_play.txt; then \
		echo "chess: engine replied to 1.e4 OK"; \
	else \
		echo "chess: FAIL - engine did not reply to 1.e4"; cat /tmp/chess_play.txt; exit 1; \
	fi
	@# perft self-test: proves move generation / make-unmake correctness
	@$(EMU) --machine 48k --rom $(ROM) chess.tap --autoload --turbo \
		--keys "960:T" --frames 95000 --text > /tmp/chess_perft.txt 2>&1
	@if grep -q "PERFT OK" /tmp/chess_perft.txt; then \
		echo "chess: perft verified (startpos 1-4, kiwipete, en passant, promotion)"; \
	else \
		echo "chess: FAIL - perft mismatch"; \
		sed -n '/SCREEN/,/END/p' /tmp/chess_perft.txt; exit 1; \
	fi

play: chess.tap
	$(EMU) --machine 48k --rom $(ROM) chess.tap --autoload --sdl --scale 3

clean:
	rm -f chess.bin chess.tap chess.sym

.PHONY: all test play clean
