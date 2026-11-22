"""Check that rolling saves contain the expected consecutive source frames."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

from check_trim import frames, packets

with tempfile.TemporaryDirectory() as tmp:
    directory = Path(tmp)
    source = directory / "encoded.mp4"
    subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
                    "testsrc2=size=96x64:rate=30:duration=86",
                    "-f", "lavfi", "-i", "sine=frequency=997:sample_rate=48000:duration=86",
                    "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=86",
                    "-map", "0:v", "-map", "1:a", "-map", "2:a", "-c:a", "aac", "-c:v", "mpeg4",
                    "-g", "30", "-sc_threshold", "1000000000", "-bf", "0",
                    "-movflags", "empty_moov+frag_keyframe+default_base_moof",
                    "-video_track_timescale", "90000", source], check=True)
    original = frames(source)
    for limit, history, ends in [(96 * 1024 * 1024, 15, (18, 22)),
                                 (200_000, 15, (18, 22)),
                                 (96 * 1024 * 1024, 60, (63, 83))]:
        first = directory / f"saved-{limit}-{history}.mkv"
        run = subprocess.run([sys.argv[1], source, first, str(limit), str(history),
                              *map(str, ends), "serial" if limit == 200_000 else "overlap"], check=True, capture_output=True, text=True)
        events = [json.loads(line) for line in run.stdout.splitlines()]
        assert [event["event"] for event in events] == ["clip_saved", "clip_saved"], events
        assert all(event["queued"] for event in events), events
        assert events[0]["last_fragment_ms"] < events[1]["first_fragment_ms"]
        second = Path(events[1]["path"])
        for saved, end in zip((first, second), ends):
            data = frames(saved)
            expected = (history + 1 if saved == first else ends[1] - ends[0]) * 30
            if limit == 200_000:
                assert 0 < len(data) <= expected
                if saved == first:
                    assert len(data) < expected
            else:
                assert len(data) == expected
            assert data == original[end * 30 - len(data):end * 30]
            for stream in ("a:0", "a:1"):
                audio = packets(saved, stream)
                source_audio = packets(source, stream)
                hashes = [p["data_hash"] for p in audio]
                source_hashes = [p["data_hash"] for p in source_audio]
                offset = next(i for i in range(len(source_hashes))
                              if hashes == source_hashes[i:i + len(hashes)])
                first_frame = end * 30 - len(data)
                video_shift = (float(packets(source, "v:0")[first_frame]["pts_time"])
                               - float(packets(saved, "v:0")[0]["pts_time"]))
                for idx in (0, -1):
                    ref = source_audio[offset:offset + len(audio)][idx]
                    shift = float(ref["pts_time"]) - float(audio[idx]["pts_time"])
                    assert abs(shift - video_shift) < 0.002, (shift, video_shift)

            if saved == first and history == 15 and limit == 96 * 1024 * 1024:
                clip = directory / "highlight.mp4"
                keys = [round(float(p["pts_time"]) * 1000000) for p in packets(saved, "v:0")
                        if "K" in p["flags"] and "D" not in p["flags"]]
                cut = max(t for t in keys if t <= 5800000)
                subprocess.run([sys.argv[2], saved, clip, "mp4", str(cut), *map(str, keys)],
                               check=True)
                for stream in ("a:0", "a:1"):
                    hashes = [p["data_hash"] for p in packets(saved, stream)]
                    trimmed = [p["data_hash"] for p in packets(clip, stream)]
                    assert trimmed and any(trimmed == hashes[i:i + len(trimmed)]
                                           for i in range(len(hashes)))
                video = frames(clip)
                assert video and any(video == data[i:i + len(video)] for i in range(len(data)))
