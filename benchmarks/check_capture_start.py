"""Compare desktop sync with the WAV producer started before or after the video input opens."""
import argparse
import os
from pathlib import Path
import subprocess
import threading
import time


def capture(mode, recorder, reference, output):
    if output.exists():
        raise ValueError(f"Choose a fresh recording path: {output}")
    # Match the measured 60 fps NVENC direct-recording path. FFmpeg only reads
    # the WAV header after opening the first (desktop video) input.
    command = ["ffmpeg", "-n", "-nostdin", "-hide_banner", "-loglevel", "info",
               "-f", "lavfi", "-i", "ddagrab=output_idx=0:framerate=60:dup_frames=1",
               "-probesize", "32", "-ignore_length", "1", "-f", "wav", "-i", "pipe:0",
               "-map", "0:v:0", "-map", "1:a:0", "-c:v", "h264_nvenc", "-b:v", "30M",
               "-g", "60", "-fps_mode:v", "passthrough", "-enc_time_base:v", "demux",
               "-c:a", "aac", "-ar", "48000", "-ac", "2", "-b:a", "192k", "-t", "15", str(output)]
    reader, writer = os.pipe()
    helper = None
    player = None
    ffmpeg = None
    try:
        if mode == "early":
            helper = subprocess.Popen([recorder, "--internal-loopback"], stdout=writer)
        start = time.monotonic()
        ffmpeg = subprocess.Popen(command, stdin=reader, stderr=subprocess.PIPE)
        os.close(reader)
        reader = None
        # FFmpeg prints the input description before attempting to open input 1.
        # Keep stdout/stderr out of the binary audio pipe.
        ready = threading.Event()

        def read_log():
            for line in ffmpeg.stderr:
                if line.startswith(b"Input #0,"):
                    ready.set()
                if b"Error" in line or b"failed" in line.lower():
                    print(line.decode(errors="replace").rstrip(), flush=True)

        log = threading.Thread(target=read_log, daemon=True)
        log.start()
        if not ready.wait(10):
            raise RuntimeError("FFmpeg did not announce the desktop video input within 10 seconds")
        print(f"Video input announced after {time.monotonic() - start:.3f} s", flush=True)
        if mode == "video-first":
            helper = subprocess.Popen([recorder, "--internal-loopback"], stdout=writer)
        os.close(writer)
        writer = None
        time.sleep(1)
        player = subprocess.Popen(["ffplay", "-loglevel", "error", "-fs", "-autoexit", reference])
        if ffmpeg.wait(timeout=45):
            raise RuntimeError("FFmpeg failed")
        log.join()
    finally:
        for fd in (reader, writer):
            if fd is not None:
                os.close(fd)
        for proc in (ffmpeg, helper, player):
            if proc is not None:
                if proc.poll() is None:
                    proc.kill()
                proc.wait()


if __name__ == "__main__":
    from check_av_sync import inspect

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["early", "video-first"])
    parser.add_argument("reference", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--recorder", type=Path, default=Path("build-x64/klippit-record.exe"))
    args = parser.parse_args()
    try:
        if not args.reference.is_file():
            parser.error("Missing flash-and-tone reference")
        capture(args.mode, args.recorder.resolve(), args.reference, args.output)
        inspect(args.output)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        parser.exit(1, f"{exc}\n")
