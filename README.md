# Klippit

Klippit is a keyboard-driven desktop app for trimming gameplay recordings without re-encoding. Open a replay, mark a highlight, and save it as a new MP4 or MKV. The original recording stays intact, and trimming requires no hardware encoder.

## Install and update on Windows

Download `Klippit-win-Setup.exe` from the [latest GitHub release](https://github.com/yahya-kousa/Klippit/releases/latest), run Setup, and open **Klippit** from Start.

Klippit checks for updates shortly after launch and periodically while it runs in the tray. When it finds a new version, it shows a notification and adds **Download update** to the tray menu. You can also choose **Check for updates…** manually. When the download finishes, choose **Restart and update**. Save any replay you want to keep and finish editing first. Restarting clears unsaved replay history and trim marks.

## Contribute

Klippit is open source under the [GNU General Public License version 3](LICENSE). Bug reports, feature ideas, documentation fixes, and testing on different hardware are welcome.

[Report a bug](https://github.com/yahya-kousa/Klippit/issues/new?template=bug_report.yml) · [Suggest a feature](https://github.com/yahya-kousa/Klippit/issues/new?template=feature_request.yml) · [Contributing guide](CONTRIBUTING.md)

## Trim a clip

| Action | Control |
| --- | --- |
| Open an MKV, MP4, or MOV recording | Open button or Ctrl+O (⌘O on macOS) |
| Play or pause | K |
| Seek | Drag the playhead |
| Step one frame backward or forward | `,` / `.` |
| Mark the requested start | I |
| Mark the end | O |
| Preview the selection from its lossless start | Space while paused |
| Export to MP4 or MKV | Enter |

The green flag marks your requested start and the orange flag marks your end. The playhead shows your current position. Preview plays the default audio track. Export copies the first video track and all audio tracks. MP4 is selected by default for sharing. Choose Matroska (MKV) in the export dialog, or use **Save as MKV** if FFmpeg reports that MP4 cannot carry a track.

Tap either key to pause and step one frame. Hold period to play forward until release, or hold comma to step backward repeatedly. Press **I** or **O** to mark the selected frame's timestamp.

## Choose where clips save

In the Klippit window, open **Settings → Save folders…**. You can also open **Save folders…** from the Windows tray. Use **Browse…** to choose a folder for replay captures or trimmed exports. The replay folder takes effect the next time you start replay. The trim folder sets the starting location of the export dialog, where you can still pick a different destination for each export.

**Use default** restores **Videos → Klippit** for replays and the source clip's folder for trims. Saved clips stay in their original locations.

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

## Run the regression tests

With Python 3, ffmpeg, and ffprobe available, enable the tests from the repository root:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The suite runs without opening a window. The trim test generates a temporary 60 FPS recording with two audio tracks and checks MP4 and MKV exports against the source. Other tests cover capture startup, replay buffering, and recording metrics.

## Project status

Klippit is an early single-range editor for existing recordings. The **Clips to Trim** queue shows replays saved by the recorder, including saves made while the editor is closed. In the editor, choose **Mark done** to move a replay to **Show completed**. The original stays available for more trims. In **Show completed**, choose **Remove from queue** to forget a replay and keep its files. This action is also available on **File missing** entries in either view. From either queue view or the editor, choose **Trash original…** on a pending or completed replay and confirm to move the source replay to the system trash and remove its queue entry. Exported trims stay in their save folder. Use Ctrl+click to select individual clips, Shift+click for a range, or Ctrl+A in the list to select all. **Trash originals** confirms the batch once. **Remove from queue** keeps files on disk and is available when every selected entry is completed or missing. Both queue views persist across restarts. Edits last until you open another recording or quit. The experimental Windows recorder can retain recent video in RAM and save it with a hotkey.

The editor and export workflow have been tested on CachyOS with a 1440p Overwatch recording. All 300 decoded frames in a five-second export matched the corresponding source frames, and the AAC packet payloads matched the source. The Windows installer workflow also tests editor startup. The macOS GUI build still needs verification.

## Experimental Windows recorder

Build the Windows target `klippit` to build the app and its recorder together, then open `klippit.exe`. On Home, select **Start Replay** and watch the status for when the buffer has footage. Press the save hotkey (**Ctrl+Shift+F9** by default). **Clips to Trim** shows the pending queue count and opens the clip list. Select a replay to open it in the editor, or use **Open file…** to edit a recording from disk. **Back to Home** pauses the editor and **Resume editing** returns to your trim. Replay controls and the clip queue are also available from the Windows tray. The tray defaults to desktop audio and 15 seconds of history.

The app groups new MKVs under the selected replay folder (default **Videos → Klippit**) by the foreground app when you press Save. Under **Settings → Save folders…**, check previously clipped apps to send their future clips to **Desktop**. Uncheck one to resume saving to its own folder. Rules take effect on the next save after you confirm the dialog. Explorer and unidentified windows use **Desktop**. All folders feed the same trim queue.

Closing the editor leaves the tray and recorder running. Launching Klippit again opens the existing window. **Stop Replay** finishes accepted saves, and **Quit Klippit** also closes the tray. Use **Quit Klippit** before rebuilding on Windows.

Open **Settings → Replay Settings…** in the Klippit window or **Replay Settings…** from the tray to set the history length, save hotkey, and audio source. **Advanced** exposes target FPS and video bitrate. The save hotkey updates while replay is running. Other changes take effect the next time replay starts, including automatic startup. If Windows rejects a live hotkey change, Klippit keeps the previous shortcut. For a microphone option, choose an input from the dropdown. Click **Refresh** after connecting a device. A saved microphone stays selected if it is temporarily unavailable. If Windows rejects your shortcut, open **Recorder Error** from the tray for details.

To launch minimized to the tray when you sign in, enable **Launch at Windows sign-in**. Enable **Start replay automatically** as well to fill the buffer whenever Klippit starts. Both options are off by default. Windows starts the app from the path registered when you enable the option. If you move the build, toggle **Launch at Windows sign-in** off and back on from the new copy.

If you built Klippit from source, make sure it works from Explorer before enabling sign-in launch. If Qt is only on your developer terminal's PATH, run `windeployqt build-x64\klippit.exe` from the matching Qt environment first. Put your FFmpeg and ffprobe executables beside Klippit or on your Windows user PATH. [Qt deployment documentation](https://doc.qt.io/qt-6/windows-deployment.html)

A separate command-line target supports a 15-second recording or a RAM replay buffer with optional desktop and microphone audio and global save and stop hotkeys. On Windows, use `--desktop-audio` for sound from the default playback device or `--mic "device name"` for a shareable mix of desktop and microphone. Add `--separate-mic` to keep desktop on the first audio track and voice on the second for editing. The [recording benchmark](benchmarks/README.md) covers building it, inspecting the saved clip, and comparing game frame times across recorders. Windows gameplay and encoder compatibility still need hardware validation.

## Third-party acknowledgments

- Klippit uses Qt 6. Qt Core handles process management, Qt Widgets provides the editor interface, and Qt Multimedia and Multimedia Widgets handle audio and video preview. [Qt licensing](https://doc.qt.io/qt-6/licensing.html)
- Klippit invokes FFmpeg and ffprobe for keyframe indexing, stream-copy exports, and experimental Windows recording. [FFmpeg licensing](https://ffmpeg.org/legal.html)
