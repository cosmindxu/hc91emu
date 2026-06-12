#!/usr/bin/env python3
"""Mutation fuzzer for the hc91emu file loaders.

Builds an AddressSanitizer + UBSan emulator in a scratch directory (off
the VirtualBox shared folder, which mishandles sanitizer output), seeds
a corpus covering every loadable format, then feeds mutated inputs to
the loaders. Unique findings (first sanitizer line) are saved with the
triggering input under <scratch>/crashes/.

    tools/fuzz_loaders.py [iterations] [--jobs N] [--scratch DIR]

Default: 20000 iterations split across the CPU count. Exits non-zero if
any finding is recorded, so it can gate CI.
"""
import argparse, hashlib, os, random, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EMUSRC = ("z80 machine video tape snapshot keys png wav disasm debug sdl "
          "inflate ay fdc rzx main").split()

def build(scratch):
    os.makedirs(f"{scratch}/build", exist_ok=True)
    cf = ["-std=c99", "-O1", "-g", "-fsanitize=address,undefined",
          "-fno-sanitize-recover=all", "-Wall"]
    objs = []
    for s in EMUSRC:
        o = f"{scratch}/build/{s}.o"
        subprocess.run(["cc", *cf, "-c", f"{ROOT}/src/{s}.c", "-o", o],
                       check=True)
        objs.append(o)
    emu = f"{scratch}/build/hc91emu"
    subprocess.run(["cc", *cf, "-o", emu, *objs, "-ldl"], check=True)
    return emu

def seed_corpus(scratch, emu):
    """Make one valid input per format with the in-tree generators."""
    c = f"{scratch}/corpus"
    os.makedirs(c, exist_ok=True)
    run = lambda *a: subprocess.run(a, cwd=ROOT, check=False,
                                    stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL)
    run(f"{ROOT}/build/fliptap", f"{c}/seed.tap")
    run(f"{ROOT}/build/tap2tzx", f"{c}/seed.tap", f"{c}/turbo.tzx")
    run(f"{ROOT}/build/tap2tzx", f"{c}/seed.tap", f"{c}/gdb.tzx", "gdb")
    run(f"{ROOT}/build/tap2tzx", f"{c}/seed.tap", f"{c}/csw.tzx", "csw")
    run(f"{ROOT}/build/tap2wav", f"{c}/seed.tap", f"{c}/seed.wav",
        "22050", "8", "1")
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
    rom = f"{ROOT}/roms/hc91.rom"
    for fmt in ("sna", "z80", "szx", "scr"):
        subprocess.run([emu, "--rom", rom, f"{c}/seed.tap", "--autoload",
                        "--turbo", "--frames", "400", f"--save-{fmt}",
                        f"{c}/seed.{fmt}"], env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run([emu, "--rom", rom, "--frames", "100", "--rzx-record",
                    f"{c}/seed.rzx"], env=env, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL)
    with open(f"{c}/seed.img", "wb") as f:
        f.write(b"HCBOOT" + bytes(327674))
    return {n: open(f"{c}/{n}", "rb").read()
            for n in os.listdir(c) if os.path.getsize(f"{c}/{n}")}

def mutate(d, rng):
    d = bytearray(d)
    op = rng.random()
    if op < 0.5:
        for _ in range(rng.randint(1, 16)):
            d[rng.randrange(len(d))] = rng.randrange(256)
    elif op < 0.75:
        d = d[:rng.randrange(1, len(d) + 1)]
    elif op < 0.9 and len(d) > 8:
        a = rng.randrange(len(d))
        d[a:a] = d[a:min(len(d), a + rng.randint(1, 4096))]
    else:
        for _ in range(rng.randint(1, 6)):
            p = rng.randrange(max(1, len(d) - 4))
            v = rng.choice([0, 1, 0xFF, 0xFFFF, 0x8000, len(d), 0xFFFFFFF])
            w = rng.choice([2, 4])
            d[p:p + w] = v.to_bytes(4, "little")[:w]
    return bytes(d)

def worker(emu, scratch, seeds, iters, seed, crashdir):
    rng = random.Random(seed)
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:exitcode=99",
               UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    rom = f"{ROOT}/roms/hc91.rom"
    names = list(seeds)
    found = {}
    for _ in range(iters):
        name = rng.choice(names)
        ext = name[name.rfind("."):]
        data = mutate(seeds[name], rng)
        tf = f"{scratch}/w{seed}{ext}"
        open(tf, "wb").write(data)
        if ext == ".img":
            cmd = [emu, "--machine", "hc2000", "--disk", tf, "--frames", "30"]
        else:
            cmd = [emu, "--rom", rom, tf, "--frames", "2"]
        try:
            r = subprocess.run(cmd, env=env, capture_output=True, timeout=10)
            rc, err = r.returncode, r.stderr
        except subprocess.TimeoutExpired:
            continue
        if rc in (0, 1) and b"Sanitizer" not in err \
           and b"runtime error" not in err:
            continue
        key = f"rc={rc}"
        for ln in err.splitlines():
            if b"Sanitizer" in ln or b"runtime error" in ln:
                key = ln.decode(errors="replace")[-150:]
                break
        if key not in found:
            found[key] = 1
            h = hashlib.md5(data).hexdigest()[:10]
            open(f"{crashdir}/{h}{ext}", "wb").write(data)
            print(f"FOUND {name}: {key} -> crashes/{h}{ext}", flush=True)
    return found

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iterations", nargs="?", type=int, default=20000)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    ap.add_argument("--scratch", default="/tmp/hc91_fuzz")
    args = ap.parse_args()

    if not os.path.exists(f"{ROOT}/build/fliptap"):
        sys.exit("error: run 'make' first (need the corpus generators)")
    os.makedirs(args.scratch, exist_ok=True)
    crashdir = f"{args.scratch}/crashes"
    os.makedirs(crashdir, exist_ok=True)
    print(f"building ASan+UBSan emulator in {args.scratch} ...", flush=True)
    emu = build(args.scratch)
    seeds = seed_corpus(args.scratch, emu)
    print(f"corpus: {', '.join(sorted(seeds))}", flush=True)

    per = max(1, args.iterations // args.jobs)
    print(f"fuzzing: {args.jobs} jobs x {per} iterations", flush=True)
    from multiprocessing import Pool
    with Pool(args.jobs) as pool:
        results = pool.starmap(worker, [
            (emu, args.scratch, seeds, per, 1000 + i, crashdir)
            for i in range(args.jobs)])
    uniq = {}
    for r in results:
        uniq.update(r)
    if uniq:
        print(f"\n{len(uniq)} unique finding(s); inputs in {crashdir}")
        sys.exit(1)
    print(f"\nclean: {per * args.jobs} iterations, no findings")

if __name__ == "__main__":
    main()
