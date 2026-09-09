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

def main(duration=30):
    print(f"[dummy_inference] running for {duration}s (simulated NPU load)...", flush=True)
    start = time.time()
    step = 0
    while time.time() - start < duration:
        v = fake_inference_step(step)
        if step % 20 == 0:
            print(f"[dummy_inference] step {step} result {v:.2f} elapsed {time.time()-start:.1f}s", flush=True)
        step += 1
    print(f"[dummy_inference] done {step} steps", flush=True)

if __name__ == "__main__":
    dur = 30
    if len(sys.argv) > 1:
        try:
            dur = int(sys.argv[1].replace('s',''))
        except: pass
    main(dur)
