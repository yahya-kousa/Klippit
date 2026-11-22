"""Generate a flash/beep reference or measure its fullscreen replay capture."""
import argparse
import array
import json
import math
import statistics
import subprocess
import sys
from pathlib import Path


CUE_TIMES = (2, 5, 9, 14, 17)


def make(path):
    flash = "+".join(f"between(t,{t},{t}+0.15)" for t in CUE_TIMES)
    tone = "+".join(f"between(t,{t},{t}+0.12)" for t in CUE_TIMES)
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-n", "-f", "lavfi",
                    "-i", f"color=black:s=640x360:r=60:d=20,drawbox=w=iw:h=ih:"
                          f"color=white:t=fill:enable='{flash}'",
                    "-f", "lavfi", "-i", f"aevalsrc='0.3*sin(2*PI*1000*t)*({tone})':s=48000:d=20",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "pcm_s16le", str(path)],
                   check=True)


def identify(times):
    """Assign observed onsets to the known sequence, allowing a partial capture."""
    candidates = set()
    for onset in times:
        for cue in CUE_TIMES:
            origin = onset - cue
            matches = []
            for t in CUE_TIMES:
                nearby = [value for value in times if abs(value - origin - t) <= 0.15]
                if len(nearby) == 1:
                    matches.append((t, nearby[0]))
                elif nearby:
                    break  # Two plausible onsets for this cue.
            else:
                candidates.add(tuple(matches))
    best = max(map(len, candidates), default=0)
    winners = [c for c in candidates if len(c) == best]
    if best < 3 or len(winners) != 1:
        return {}
    return dict(winners[0])


def inspect(path):
    def read(*args):
        return subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(path), *args, "-"])

    # Pair each decoded luminance value with its actual presentation timestamp.
    frames = json.loads(subprocess.check_output([
        "ffprobe", "-v", "error", "-select_streams", "v:0", "-show_frames",
        "-show_entries", "frame=best_effort_timestamp_time", "-of", "json", str(path)]))["frames"]
    pixels = read("-map", "0:v:0", "-vf", "crop=iw/2:ih/2,scale=1:1,format=gray,settb=1/60,setpts=N",
                  "-enc_time_base:v", "1:60",
                  "-fps_mode", "passthrough", "-f", "rawvideo")
    if len(frames) != len(pixels):
        raise ValueError("Could not match decoded images to timestamps.")
    flashes = [float(frame["best_effort_timestamp_time"]) for i, frame in enumerate(frames)
               if i and pixels[i] > 180 >= pixels[i - 1]]

    # Preserve timestamps through decode and place audio samples on the file timeline.
    pcm = array.array("h")
    pcm.frombytes(read("-copyts", "-map", "0:a:0", "-af", "aresample=8000:async=1:first_pts=0",
                       "-ac", "1", "-ar", "8000", "-f", "s16le"))
    if sys.byteorder != "little":
        pcm.byteswap()
    beeps = []
    start = None
    for i in range(0, len(pcm) - 79, 80):
        block = pcm[i:i + 80]
        # Magnitude at 1 kHz in a 10 ms window (eight samples per cycle).
        phase = [sum(block[j::8]) for j in range(8)]
        amplitude = abs(sum(v * complex(math.cos(j * math.pi / 4), math.sin(j * math.pi / 4))
                            for j, v in enumerate(phase))) / 40
        power = sum(v * v for v in block) / len(block)
        tone = amplitude > 250 and amplitude ** 2 / 2 > 0.5 * power
        if tone and start is None:
            start = i
        elif not tone and start is not None:
            # A full reference pulse lasts 120 ms. Ignore brief noise and
            # partial pulses at the capture boundaries.
            if start and 0.07 <= (i - start) / 8000 <= 0.18:
                beeps.append(start / 8000)
            start = None

    visual, audio = identify(flashes), identify(beeps)
    common = sorted(visual.keys() & audio.keys())
    if len(common) < 3:
        raise ValueError(f"No unique match of at least three shared reference cues. "
                         f"Flash onsets (s): {flashes}. Tone onsets (s): {beeps}. "
                         "Keep this replay for diagnosis.")
    offsets = [round(1000 * (audio[t] - visual[t])) for t in common]
    print(json.dumps({"reference_cues_s": common, "flash_times_s": [visual[t] for t in common],
                      "tone_times_s": [audio[t] for t in common],
                      "audio_minus_video_ms": offsets, "median_ms": statistics.median(offsets)},
                     indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["make", "inspect"])
    parser.add_argument("file", type=Path)
    args = parser.parse_args()
    if args.action == "make" and args.file.exists():
        parser.exit(1, f"{args.file} already exists. Reuse the reference or choose a new path.\n")
    if args.action == "inspect" and not args.file.is_file():
        parser.exit(1, f"Replay file missing: {args.file}. Use the path printed after Saved.\n")
    try:
        (make if args.action == "make" else inspect)(args.file)
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"{exc}\n")
