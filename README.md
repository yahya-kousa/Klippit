# Klippit

Klippit is a keyboard-driven desktop app for trimming gameplay recordings without re-encoding. Open a replay, mark a highlight, and save it as a new MP4 or MKV. The original recording stays intact, and trimming requires no hardware encoder.

## Trim a clip

| Action | Control |
| --- | --- |
| Open an MKV, MP4, or MOV recording | Open button or Ctrl+O (⌘O on macOS) |
| Play or pause | K |
| Seek | Drag the playhead |
| Mark the requested start | I |
| Mark the end | O |
| Preview the selection from its lossless start | Space while paused |
| Export to MP4 or MKV | Enter |

The green flag marks your requested start and the orange flag marks your end. The playhead shows your current position. Preview plays the default audio track. Export copies the first video track and all audio tracks. MP4 is selected by default for sharing. Choose Matroska (MKV) in the export dialog, or use **Save as MKV** if FFmpeg reports that MP4 cannot carry a track.

## How lossless trimming works

Klippit uses ffprobe to find video keyframes. If you mark a start between keyframes, preview and export begin at the preceding usable keyframe. The timeline readout shows both the requested start and the lossless start.

For example, if you select **5.800 s to 10.500 s** and the preceding keyframe is at **5.500 s**, the export includes **0.300 s** of lead-in and still aims to end at **10.500 s**. FFmpeg copies the encoded packets, preserving video and audio quality. Packet boundaries and frame reordering can affect the precise end.

## Build from source

Install a C++20 compiler, CMake 3.21 or newer, Ninja, and Qt 6.5 or newer with the Widgets, Multimedia, and MultimediaWidgets development components. Put both `ffmpeg` and `ffprobe` on your PATH so Klippit can index and export recordings.

From the repository root, run the following in a terminal configured for your compiler. On Windows with MSVC, use a Developer PowerShell with a matching Qt kit.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

If CMake cannot find Qt, add `-DCMAKE_PREFIX_PATH="<Qt kit directory>"` to the configure command. You can also open the root `CMakeLists.txt` in Qt Creator and select the matching desktop kit.

Run `./build/klippit` on Linux or `.\build\klippit.exe` in Windows PowerShell. On macOS, open the generated app bundle if your Qt kit produces one, or run `./build/klippit`.

## Run the trim regression test

With Python 3, ffmpeg, and ffprobe available, enable the test from the repository root:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The test generates a temporary 60 FPS recording with two audio tracks, runs the range calculation and exporter used by the editor, and compares MP4 and MKV exports against the source. It runs without opening a window.

## Project status

Klippit is an early single-range editor for existing recordings. The **Clips to Trim** queue shows replays saved by the recorder, including saves made while the editor is closed. Open a clip to trim it, then mark it done to move it to **Show completed**. Both views persist across restarts and keep the source MKV in its original location. Edits last until you open another recording or quit. The experimental Windows recorder can retain recent video in RAM and save it with a hotkey.

The editor and export workflow have been tested on CachyOS with a 1440p Overwatch recording. All 300 decoded frames in a five-second export matched the corresponding source frames, and the AAC packet payloads matched the source. Windows and macOS GUI builds still need verification.

## Experimental Windows recorder

Build both Windows targets into the same folder, then open `klippit.exe`. In the Windows system tray, right-click Klippit and select **Start Replay**. The menu shows when recording starts and when the buffer has footage. Press the save hotkey (**Ctrl+Shift+F9** by default), and open **Clips to Trim** from the tray when you want to edit. The tray defaults to desktop audio and 15 seconds of history. It saves MKVs in **Videos → Klippit**. Closing the editor leaves the tray and recorder running. Launching Klippit again opens the existing editor. **Stop Replay** finishes accepted saves, and **Quit Klippit** also closes the tray. Use **Quit Klippit** before rebuilding on Windows.

Open **Replay Settings** from the tray to set the history length, save hotkey, and audio source. **Advanced** exposes target FPS and video bitrate. Changes take effect when you next start replay, including automatic startup. For a microphone option, enter the exact DirectShow name listed by `ffmpeg -hide_banner -list_devices true -f dshow -i dummy`. If Windows rejects your shortcut, open **Recorder Error** from the tray for details.

To launch minimized to the tray when you sign in, enable **Launch at Windows sign-in**. Enable **Start replay automatically** as well to fill the buffer whenever Klippit starts. Both options are off by default. Windows starts the app from the path registered when you enable the option. If you move the build, toggle **Launch at Windows sign-in** off and back on from the new copy.

For sign-in launch, make sure Klippit works from Explorer. If Qt is only on your developer terminal's PATH, run `windeployqt build-x64\klippit.exe` from the matching Qt environment first. Put your FFmpeg and ffprobe executables beside Klippit or on your Windows user PATH. [Qt deployment documentation](https://doc.qt.io/qt-6/windows-deployment.html)


A separate command-line target supports a 15-second recording or a RAM replay buffer with optional desktop and microphone audio and global save and stop hotkeys. On Windows, use `--desktop-audio` for sound from the default playback device or `--mic "device name"` for a shareable mix of desktop and microphone. Add `--separate-mic` to keep desktop on the first audio track and voice on the second for editing. The [recording benchmark](benchmarks/README.md) covers building it, inspecting the saved clip, and comparing game frame times across recorders. Windows gameplay and encoder compatibility still need hardware validation.

## Third-party acknowledgments

- Klippit uses Qt 6. Qt Core handles process management, Qt Widgets provides the editor interface, and Qt Multimedia and Multimedia Widgets handle audio and video preview. [Qt licensing](https://doc.qt.io/qt-6/licensing.html)
- Klippit invokes FFmpeg and ffprobe for keyframe indexing, stream-copy exports, and experimental Windows recording. [FFmpeg licensing](https://ffmpeg.org/legal.html)
