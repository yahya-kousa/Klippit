# Recording benchmark

The first recorder experiment captures 15 seconds of a Windows display at its native resolution, with one H.264 encode and video only. Use a 2560×1440 display mode for a 1440p run. The recorder uses FFmpeg's Desktop Duplication filter and probes a real frame with each candidate hardware encoder until one works. A build that lists an encoder still needs a compatible display adapter and driver. [FFmpeg Desktop Duplication documentation](https://ffmpeg.org/ffmpeg-filters.html#ddagrab)

From the configured Windows build described in the project README, run these commands in PowerShell. Put Qt's runtime DLLs and a Windows FFmpeg build with ddagrab on PATH. Choose a fresh output filename for each trial. The entire selected monitor will be recorded.

```powershell
cmake --build build --target klippit-record
.\build\klippit-record.exe --display 0 --fps 60 --bitrate 30 trial-01.mkv > trial-01-run.json
python benchmarks/inspect_clip.py trial-01.mkv --fps 60 > trial-01-clip.json
```

Auto selection tries h264_nvenc, h264_amf, and h264_qsv. Use `--encoder` to pin a candidate across runs. A failed probe prints FFmpeg's diagnostic. The command fails if every candidate fails. The run JSON records the chosen encoder, settings, platform, and elapsed time from launching the recording process through finalizing the file. It excludes the one-frame probe. Open the result in Klippit and check motion, colors, and a trim before accepting that configuration.

## Comparing recorders

1. Record the Klippit commit, competing app version, FFmpeg version, OS, CPU, GPU and driver, display mode, game settings, FPS cap, and power mode. Choose a repeatable replay segment. Match resolution, codec, target FPS, bitrate, audio settings and recording mode as closely as each tool permits. Document mismatches, including comparing instant replay with continuous capture.
2. Warm up the replay. Run the same segment with recording off, Klippit on, and the competing recorder on. Use at least five runs per condition, rotate their order, and analyze the same time window. Keep the game in the foreground. For the CLI, start it on a delay so switching windows stays outside the measured interval.
3. Capture **game** frame times with PresentMon targeting the game process for every condition, including the baseline. Keep the raw CSVs. Compare median and 99th-percentile frame times per run, then report the median and range across runs. For memory comparisons, sample the recorder and its worker processes, including FFmpeg, at a fixed interval and record the memory counter and sampling rate. [PresentMon](https://github.com/GameTechDev/PresentMon)
4. After each trial, run the clip inspector on the saved media from either recorder. It decodes the first video stream and reports frame count, presentation timestamp intervals, and how often a gap exceeded 1.5 target frame periods. A fixed-rate recorder can repeat images while producing perfectly spaced timestamps. These counts describe the saved file and cannot establish dropped game frames or unique captured images.
5. Keep the raw reports with the hardware and workload notes. For a public or résumé claim, give the measured value, comparison baseline, units, trial count, and test configuration. Disk recording here measures continuous capture. Rolling-buffer save latency needs a later experiment once that feature exists.

## RAM replay mode (experimental)

Build the updated Windows recorder and run it from the project directory:

```powershell
cmake --build build-x64 --target klippit-record
.\build-x64\klippit-record.exe --replay --display 0 --fps 60 --bitrate 30 replay-01.mkv
```

Choose a fresh filename. With the game in the foreground, press **Ctrl+Shift+F9** to save and **Ctrl+Shift+F10** to stop. Later saves receive timestamped filenames. The console reports elapsed milliseconds from the save request through publication. One save runs at a time.

FFmpeg encodes continuously and sends keyframe-aligned fragments through a pipe. Klippit retains roughly 15 seconds plus keyframe alignment, subject to a 96 MiB encoded-history cap. A save includes the fragment in progress when you pressed the key, so publication can wait about one keyframe interval. Only requested saves are written to disk. A second FFmpeg process stream-copies the snapshot to MKV. Snapshot data, incoming fragments, process buffers and encoder memory are additional to the history cap.

For a hardware smoke test, play for 30 seconds, save during motion, keep playing, and save again five seconds later. Check both clips in the trimmer and record save latency and peak memory including both FFmpeg processes. The automated replay_buffer test checks time and byte eviction and consecutive decoded frames on synthetic input.

## Measure a RAM replay session

Install the benchmark-only Python dependency with `py -3 -m pip install psutil`. From the Windows project folder run:

```powershell
py -3 benchmarks/measure_replay.py "$env:TEMP/klippit-replay-run-01"
```

Use a new directory for each run. The default command uses the existing build-x64 recorder and pins NVENC at 60 fps and 30 Mbit/s. Select another supported encoder with --encoder. Warm up for 30 seconds, save five times at least five seconds apart with Ctrl+Shift+F9, then stop with Ctrl+Shift+F10. Keep the game in the foreground and record the scene, resolution, GPU and driver alongside the reports.

summary.json contains each successful request-to-file-publication latency, its median, and sampled working-set peaks in bytes. Divide by 1,048,576 for MiB. memory.csv retains per-process samples for the recorder and its children. The sampler targets 100 ms and can miss brief peaks. Shared pages may be counted across processes. Per-save windows are estimated from receipt of the completion message and the recorder's own elapsed time. A null peak means no sample fell in that window. recorder.log retains the timing messages. This run measures recorder memory and save latency; use the separate PresentMon procedure for game frame times.
