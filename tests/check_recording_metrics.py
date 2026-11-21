"""One omitted frame in a 60 FPS fixture should produce one long interval."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

with tempfile.TemporaryDirectory() as tmp:
    clip = Path(tmp) / "known-gap.mkv"
    subprocess.run([
        "ffmpeg", "-nostdin", "-v", "error", "-f", "lavfi", "-i",
        "testsrc2=size=64x48:rate=60:duration=1", "-vf", "select='not(eq(n,30))'",
        "-fps_mode", "passthrough", "-c:v", "ffv1", str(clip),
    ], check=True)
    inspector = Path(__file__).resolve().parents[1] / "benchmarks" / "inspect_clip.py"
    report = json.loads(subprocess.check_output([sys.executable, inspector, clip, "--fps", "60"]))
    assert report["frames"] == 59, report
    assert report["intervals_over_1_5_frame_periods"] == 1, report
    assert 32 <= round(report["max_interval_ms"], 3) <= 34, report
    assert report["nonadvancing_intervals"] == 0, report
