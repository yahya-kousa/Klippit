"""Report decoded video presentation timing for any recorder's clip."""
import argparse
import hashlib
import json
import math
import statistics
import subprocess
from pathlib import Path


def inspect(path, target_fps):
    probe = subprocess.check_output([
        "ffprobe", "-v", "error", "-select_streams", "v:0", "-show_frames",
        "-show_entries", "frame=best_effort_timestamp_time:stream=codec_name,width,height",
        "-show_program_version", "-of", "json", str(path),
    ])
    data = json.loads(probe)
    times = [float(frame["best_effort_timestamp_time"]) for frame in data["frames"]]
    if len(times) < 2 or times[-1] <= times[0]:
        raise ValueError("Need at least two frames with advancing presentation timestamps")
    intervals = [(b - a) * 1000 for a, b in zip(times, times[1:])]
    ordered = sorted(intervals)
    digest = hashlib.sha256()
    with path.open("rb") as media:
        for block in iter(lambda: media.read(1024 * 1024), b""):
            digest.update(block)
    return {
        "file": path.name, "sha256": digest.hexdigest(), "bytes": path.stat().st_size,
        "ffprobe_version": data["program_version"]["version"], **data["streams"][0],
        "target_fps": target_fps, "frames": len(times),
        "average_presented_fps": (len(times) - 1) / (times[-1] - times[0]),
        "median_interval_ms": statistics.median(intervals),
        "p99_interval_ms": ordered[math.ceil(len(ordered) * 0.99) - 1],
        "max_interval_ms": max(intervals),
        "intervals_over_1_5_frame_periods": sum(ms > 1500 / target_fps for ms in intervals),
        "nonadvancing_intervals": sum(ms <= 0 for ms in intervals),
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("clip", type=Path)
    parser.add_argument("--fps", type=int, required=True, help="intended recording frame rate")
    args = parser.parse_args()
    if args.fps < 1:
        parser.error("--fps must be positive")
    print(json.dumps(inspect(args.clip, args.fps), indent=2))
