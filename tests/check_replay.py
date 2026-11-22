"""Check that rolling saves contain the expected consecutive source frames."""
import subprocess
import sys
import tempfile
from pathlib import Path

from check_trim import frames

with tempfile.TemporaryDirectory() as tmp:
    directory = Path(tmp)
    source = directory / "encoded.mp4"
    subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
                    "testsrc2=size=96x64:rate=30:duration=24", "-c:v", "mpeg4",
                    "-g", "30", "-sc_threshold", "1000000000", "-bf", "0",
                    "-movflags", "empty_moov+frag_keyframe+default_base_moof",
                    "-video_track_timescale", "90000", source], check=True)
    original = frames(source)
    for limit in (96 * 1024 * 1024, 200_000):
        first = directory / f"saved-{limit}.mkv"
        subprocess.run([sys.argv[1], source, first, str(limit)], check=True)
        second, = directory.glob(f"saved-{limit}-*.mkv")
        for saved, end in [(first, 18), (second, 22)]:
            data = frames(saved)
            if limit == 200_000:
                assert 0 < len(data) < 16 * 30
            else:
                assert len(data) == 16 * 30
            assert data == original[end * 30 - len(data):end * 30]
