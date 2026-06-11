#!/usr/bin/env python3
"""Verify a sustained tone in a recorded WAV. Default expectation is the
BASIC `BEEP 1,0` case (~1 s of ~261.6 Hz middle C); pass a target
frequency and min/max duration in blocks of 0.1 s to override:
    check_beep.py FILE [HZ MIN_BLOCKS MAX_BLOCKS]
Exit 0 on pass."""
import sys, wave, struct

target = float(sys.argv[2]) if len(sys.argv) > 2 else 261.6
min_blk = int(sys.argv[3]) if len(sys.argv) > 3 else 8
max_blk = int(sys.argv[4]) if len(sys.argv) > 4 else 12

w = wave.open(sys.argv[1])
n, rate = w.getnframes(), w.getframerate()
data = struct.unpack('<%dh' % n, w.readframes(n))

# Find contiguous 0.1s blocks with sustained oscillation (the tone; brief
# keyboard clicks don't qualify). Crossings are counted against the local
# mean so a DC offset (idle-beeper baseline under an AY tone) is ignored.
blk = rate // 10
def crossings(seg):
    m = sum(seg) / len(seg)
    return sum(1 for i in range(1, len(seg))
               if (seg[i] > m) != (seg[i - 1] > m))
active = [b for b in range(0, n - blk, blk)
          if crossings(data[b:b + blk]) > 30]
if not active:
    print("FAIL: no sustained tone found"); sys.exit(1)
dur_blocks = len(active)

a, b = active[0] + blk // 2, active[-1] + blk // 2
seg = data[a:b]
if len(seg) < blk:
    print("FAIL: tone too short"); sys.exit(1)
dur = len(seg) / rate
mean = sum(seg) / len(seg)
rises = sum(1 for i in range(1, len(seg))
            if seg[i - 1] < mean <= seg[i])
freq = rises / dur

print(f"tone: {dur_blocks * 0.1:.1f}s, {freq:.1f} Hz")
ok = (abs(freq - target) <= target * 0.045
      and min_blk <= dur_blocks <= max_blk)
print("PASS" if ok else
      f"FAIL: expected {min_blk/10:.1f}-{max_blk/10:.1f}s of ~{target} Hz")
sys.exit(0 if ok else 1)
