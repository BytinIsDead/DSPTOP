#!/usr/bin/env python3
"""
Dummy AI inference script to generate NPU/DSP load during CI.
Runs a tight loop of numpy matmuls that *would* be offloaded to NPU via
vendor runtimes (QNN, OpenVINO NPU plugin, CoreML ANE) on real hardware.
On CI runners without NPU, it simply burns CPU so the mock HAL shows activity.
"""
import time, math, random, sys

def fake_inference_step(i):
    # Simulate varied model shapes: MobileNet, BERT-tiny, etc.
    n = 128 + (i % 4)*64
    # Burn cycles with Python math (reproducible)
    s = 0.0
    for j in range(2000):
        s += math.sin(j * 0.001 + i) * math.cos(j * 0.002)
    # Sleep to simulate accelerator dispatch latency
    time.sleep(0.05)
    return s

def main(duration=30, real=False, backend="auto"):
    # If --real is requested and a real NPU is present, delegate to infer_q4km.py
    if real:
        import pathlib, subprocess, sys as _sys
        q4 = pathlib.Path(__file__).with_name("infer_q4km.py")
        if q4.exists():
            print(f"[dummy_inference] --real requested -> delegating to {q4}", flush=True)
            cmd = [_sys.executable, str(q4), "--duration", str(duration), "--backend", backend]
            raise SystemExit(subprocess.call(cmd))
        else:
            print("[dummy_inference] --real requested but infer_q4km.py not found, falling back to dummy", flush=True)
    print(f"[dummy_inference] running for {duration}s (simulated NPU load)...", flush=True)
    print(f"[dummy_inference] tip: on real NPU hardware, try: python scripts/infer_q4km.py --duration {duration}s  (real 8B Q4_K_M)", flush=True)
    start = time.time()
    step = 0
    while time.time() - start < duration:
        v = fake_inference_step(step)
        if step % 20 == 0:
            print(f"[dummy_inference] step {step} result {v:.2f} elapsed {time.time()-start:.1f}s", flush=True)
        step += 1
    print(f"[dummy_inference] done {step} steps", flush=True)

if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="DSPTOP dummy inference (use --real for 8B Q4_K_M on actual NPU)")
    ap.add_argument("duration", nargs="?", default="30", help="seconds e.g. 30 or 30s")
    ap.add_argument("--duration", dest="duration_opt", default=None, help="alias for duration, e.g. --duration 30s")
    ap.add_argument("--real", action="store_true", help="if NPU present, run real 8B Q4_K_M via infer_q4km.py")
    ap.add_argument("--backend", default="auto", choices=["auto","llama_cpp","openvino","mlx","qnn","dummy"], help="backend for --real")
    args = ap.parse_args()
    raw = args.duration_opt if args.duration_opt is not None else args.duration
    dur = 30
    try:
        dur = int(str(raw).replace('s',''))
    except: pass
    main(dur, real=args.real, backend=args.backend)
