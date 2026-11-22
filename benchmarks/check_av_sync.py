"""Generate a flash/beep reference or measure its fullscreen replay capture."""
import argparse
import array
import json
import math
import statistics
import subprocess
import sys
from pathlib import Path


def make(path):
    # Nonuniform spacing makes it possible to identify a missed or extra cue.
    times = (2, 5, 9, 14, 17)
    flash = "+".join(f"between(t,{t},{t}+0.15)" for t in times)
    tone = "+".join(f"between(t,{t},{t}+0.12)" for t in times)
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-n", "-f", "lavfi",
                    "-i", f"color=black:s=640x360:r=60:d=20,drawbox=w=iw:h=ih:"
                          f"color=white:t=fill:enable='{flash}'",
                    "-f", "lavfi", "-i", f"aevalsrc='0.3*sin(2*PI*1000*t)*({tone})':s=48000:d=20",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "pcm_s16le", str(path)],
                   check=True)


def inspect(path):
    def read(*args):
        return subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(path), *args, "-"])

    # Pair each decoded luminance value with its actual presentation timestamp.
    frames = json.loads(subprocess.check_output([
        "ffprobe", "-v", "error", "-select_streams", "v:0", "-show_frames",
        "-show_entries", "frame=best_effort_timestamp_time", "-of", "json", str(path)]))["frames"]
    pixels = read("-map", "0:v:0", "-vf", "crop=iw/2:ih/2,scale=1:1,format=gray",
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
    active = False
    for i in range(0, len(pcm) - 79, 80):
        block = pcm[i:i + 80]
        # Magnitude at 1 kHz in a 10 ms window (eight samples per cycle).
        phase = [sum(block[j::8]) for j in range(8)]
        amplitude = abs(sum(v * complex(math.cos(j * math.pi / 4), math.sin(j * math.pi / 4))
                            for j, v in enumerate(phase))) / 40
        if amplitude > 250 and not active and i:
            beeps.append(i / 8000)
        active = amplitude > 250
    if len(flashes) < 2 or len(flashes) != len(beeps):
        raise ValueError(f"Found {len(flashes)} flashes and {len(beeps)} tones. Capture the "
                         "reference fullscreen, with the playback device otherwise quiet.")
    offsets = [round(1000 * (b - f)) for f, b in zip(flashes, beeps)]
    print(json.dumps({"flash_times_s": flashes, "tone_times_s": beeps,
                      "audio_minus_video_ms": offsets, "median_ms": statistics.median(offsets)},
                     indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["make", "inspect"])
    parser.add_argument("file", type=Path)
    args = parser.parse_args()
    (make if args.action == "make" else inspect)(args.file)
