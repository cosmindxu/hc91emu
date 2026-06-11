#!/usr/bin/env python3
"""Verify a recorded BASIC `BEEP 1,0`: find the sustained tone in the WAV
and check it is ~1 second of ~261.6 Hz (middle C). Exit 0 on pass."""
import sys, wave, struct

w = wave.open(sys.argv[1])
n, rate = w.getnframes(), w.getframerate()
data = struct.unpack('<%dh' % n, w.readframes(n))

# Find contiguous 0.1s blocks with sustained oscillation (the tone; brief
# keyboard clicks don't qualify).
blk = rate // 10
active = [b for b in range(0, n - blk, blk)
          if sum(1 for i in range(b + 1, b + blk)
                 if (data[i] > 0) != (data[i - 1] > 0)) > 30]
if not active:
    print("FAIL: no sustained tone found"); sys.exit(1)
dur_blocks = len(active)

a, b = active[0] + blk // 2, active[-1] + blk // 2
seg = data[a:b]
if len(seg) < blk:
    print("FAIL: tone too short"); sys.exit(1)
dur = len(seg) / rate
rises = sum(1 for i in range(1, len(seg)) if seg[i - 1] < 0 <= seg[i])
freq = rises / dur

print(f"tone: {dur_blocks * 0.1:.1f}s, {freq:.1f} Hz")
ok = 250 <= freq <= 272 and 8 <= dur_blocks <= 12
print("PASS" if ok else "FAIL: expected ~1.0s of ~261.6 Hz")
sys.exit(0 if ok else 1)
