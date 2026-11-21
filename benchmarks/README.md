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
