"""Measure Klippit replay save latency and sampled process-tree memory."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import sys
import threading
import time

import psutil


def measure(executable, directory, encoder, audio_device=None):
    directory.mkdir(parents=True, exist_ok=False)
    clip = directory.resolve() / "replay.mkv"
    command = [str(executable.resolve()), "--replay", "--fps", "60", "--bitrate", "30",
               "--encoder", encoder, str(clip)]
    if audio_device:
        command.extend(["--audio-device", audio_device])
    metadata = {
        "command": command, "platform": platform.platform(), "psutil": psutil.__version__,
        "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        "ffmpeg_version": subprocess.check_output(["ffmpeg", "-version"], text=True).splitlines()[0],
        "target_sample_interval_ms": 100,
    }
    start = time.monotonic()
    saves = []

    def log_output(output):
        with (directory / "recorder.log").open("w", encoding="utf-8") as log:
            for line in output:
                received = (time.monotonic() - start) * 1000
                print(line, end="", flush=True)
                log.write(f"{received:.1f} {line}")
                log.flush()
                match = re.search(r'^Saved .* in (\d+) ms\s*$', line)
                if match:
                    saves.append((received, int(match[1])))

    samples = []
    env = dict(os.environ, QT_FORCE_STDERR_LOGGING="1")
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          env=env, text=True, errors="replace") as proc:
        reader = threading.Thread(target=log_output, args=(proc.stdout,))
        reader.start()
        try:
            root = psutil.Process(proc.pid)
            with (directory / "memory.csv").open("w", newline="") as raw:
                writer = csv.writer(raw)
                writer.writerow(["elapsed_ms", "pid", "process", "rss_bytes"])
                while proc.poll() is None:
                    tick = time.monotonic()
                    elapsed = (tick - start) * 1000
                    total = 0
                    for process in [root, *root.children(recursive=True)]:
                        try:
                            name = process.name()
                            rss = process.memory_info().rss
                        except psutil.NoSuchProcess:
                            continue
                        writer.writerow([round(elapsed, 1), process.pid, name, rss])
                        total += rss
                    samples.append((elapsed, total))
                    time.sleep(max(0, tick + 0.1 - time.monotonic()))
        except KeyboardInterrupt:
            metadata["interrupted"] = True
        except psutil.NoSuchProcess:
            pass
        finally:
            if proc.poll() is None:
                proc.terminate()
            proc.wait()
            reader.join()

    metadata["exit_code"] = proc.returncode
    write_summary(directory, metadata, samples, saves)
    return proc.returncode or (0 if saves else 1)


def write_summary(directory, metadata, samples, saves):
    metadata.update(save_latency_ms=[ms for _, ms in saves],
                    median_save_ms=statistics.median(ms for _, ms in saves) if saves else None,
                    peak_sampled_tree_rss_bytes=max((rss for _, rss in samples), default=None),
                    peak_sampled_save_rss_bytes=[
                        max((rss for t, rss in samples if end - ms <= t <= end), default=None)
                        for end, ms in saves])
    report = json.dumps(metadata, indent=2)
    (directory / "summary.json").write_text(report + "\n", encoding="utf-8")
    print(report)


def recover(directory):
    totals = {}
    with (directory / "memory.csv").open(newline="") as raw:
        for row in csv.DictReader(raw):
            try:
                t = float(row["elapsed_ms"])
                rss = int(row["rss_bytes"])
            except (TypeError, ValueError):
                continue  # An interrupted last row can be incomplete.
            totals[t] = totals.get(t, 0) + rss
    saves = []
    for line in (directory / "recorder.log").read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r'(\d+(?:\.\d+)?) Saved .* in (\d+) ms', line)
        if match:
            saves.append((float(match[1]), int(match[2])))
    write_summary(directory, {"recovered_from_raw_files": True}, list(totals.items()), saves)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="fresh directory for clips and reports")
    parser.add_argument("--recorder", type=Path, default=Path("build-x64/klippit-record.exe"))
    parser.add_argument("--encoder", default="h264_nvenc",
                        choices=["h264_nvenc", "h264_amf", "h264_qsv"])
    parser.add_argument("--audio-device", help="Windows recording endpoint used for game audio")
    parser.add_argument("--recover", action="store_true", help="summarize an earlier run's raw files")
    args = parser.parse_args()
    if args.recover:
        if (args.directory / "summary.json").exists():
            parser.error("summary.json already exists in this directory")
        recover(args.directory)
        sys.exit(0)
    if args.directory.exists() or not args.recorder.is_file():
        parser.error("Choose a new run directory and an existing recorder executable.")
    sys.exit(measure(args.recorder, args.directory, args.encoder, args.audio_device))
