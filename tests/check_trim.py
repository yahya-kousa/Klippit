"""Exercise Klippit's range selection and exporter against a generated 60 FPS recording."""

import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def ffmpeg(*args):
    return subprocess.check_output(["ffmpeg", "-nostdin", "-v", "error", *args], text=True)


def packets(path, track):
    output = subprocess.check_output([
        "ffprobe", "-v", "error", "-select_streams", track, "-show_packets",
        "-show_data_hash", "sha256", "-show_entries", "packet=pts_time,flags,data_hash",
        "-of", "json", path,
    ])
    return json.loads(output)["packets"]


def frames(path):
    output = ffmpeg("-i", path, "-map", "0:v:0", "-fps_mode", "passthrough", "-f", "framehash", "-")
    return [line.split(",")[-1].strip() for line in output.splitlines() if not line.startswith("#")]


def check(directory):
    source = directory / "source.mkv"
    ffmpeg("-f", "lavfi", "-i", "testsrc2=size=160x90:rate=60",
           "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000",
           "-f", "lavfi", "-i", "sine=frequency=880:sample_rate=48000",
           "-t", "12", "-map", "0:v", "-map", "1:a", "-map", "2:a",
           "-c:v", "mpeg4", "-g", "30", "-bf", "0", "-sc_threshold", "1000000000",
           "-c:a", "alac", source)
    original = hashlib.sha256(source.read_bytes()).digest()
    keys = [str(round(float(p["pts_time"]) * 1000000)) for p in packets(source, "v:0")
            if "K" in p["flags"] and "D" not in p["flags"]]
    expected = frames(source)[330:630]
    assert len(expected) == 300
    for fmt in ("mp4", "mkv"):
        out = directory / f"trim.{fmt}"
        subprocess.run([sys.argv[1], source, out, fmt, "5500000", *keys], check=True)
        assert frames(out) == expected, f"{fmt}: decoded frames differ from source frames 330–629"
        for track in ("a:0", "a:1"):
            before = [p["data_hash"] for p in packets(source, track)]
            after = [p["data_hash"] for p in packets(out, track)]
            assert after and any(before[i:i + len(after)] == after for i in range(len(before))), track
    assert hashlib.sha256(source.read_bytes()).digest() == original, "source changed"


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        check(Path(tmp))
