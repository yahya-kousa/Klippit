"""Measure CPU time and context switches of Klippit's running desktop audio helper."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import sys
import time

import psutil


def find_helper():
    matches = []
    for proc in psutil.process_iter(["name"]):
        if (proc.info["name"] or "").lower() != "klippit-record.exe":
            continue
        try:
            if proc.cmdline()[1:] == ["--internal-loopback"]:
                matches.append(proc)
        except psutil.NoSuchProcess:
            continue
    if len(matches) != 1:
        raise ValueError(f"Found {len(matches)} desktop audio helpers. Start one Klippit "
                         "replay buffer with desktop audio enabled, then retry.")
    return matches[0]


def counters(proc):
    cpu = proc.cpu_times()
    switches = proc.num_ctx_switches()
    return {"user_s": cpu.user, "kernel_s": cpu.system,
            "context_switches": switches.voluntary + switches.involuntary}


def measure(seconds):
    proc = find_helper()
    exe = Path(proc.exe())
    digest = hashlib.sha256(exe.read_bytes()).hexdigest()
    print(f"Measuring desktop audio helper PID {proc.pid} for {seconds} seconds.",
          file=sys.stderr, flush=True)
    start = counters(proc)
    then = time.perf_counter()
    time.sleep(seconds)
    end = counters(proc)
    elapsed = time.perf_counter() - then
    if not proc.is_running():
        raise ValueError("The original helper exited during the measurement. Retry with a stable session.")
    cpu_s = end["user_s"] - start["user_s"] + end["kernel_s"] - start["kernel_s"]
    return {"platform": platform.platform(), "psutil_version": psutil.__version__,
            "helper_pid": proc.pid, "executable_sha256": digest, "elapsed_s": elapsed,
            "start": start, "end": end, "helper_cpu_s": cpu_s,
            "cpu_percent_of_one_core": 100 * cpu_s / elapsed,
            "context_switches_per_s": (end["context_switches"] - start["context_switches"]) / elapsed}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=int, default=300)
    args = parser.parse_args()
    if args.seconds < 1:
        parser.error("--seconds must be positive")
    try:
        print(json.dumps(measure(args.seconds), indent=2))
    except (psutil.Error, OSError, ValueError) as exc:
        parser.exit(1, f"Measurement failed: {exc}\n")
