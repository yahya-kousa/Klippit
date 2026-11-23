# Windows release guide

## Build and test an installer

Maintainers can build an unsigned x64 installer from the **Actions → Windows installer → Run workflow** menu on GitHub. Open the completed run, download the **Klippit-Windows-x64** artifact, extract the ZIP, and open its Setup executable. The installed app is available from Start. Each workflow run builds a test version numbered `0.1.<run number>`.

For the first smoke test, quit any development copy from its tray menu, then use a clean Windows account or VM without Qt or FFmpeg on PATH. Launch from Start, open a clip, preview and export a trim. On a PC with a supported encoder, start replay, save a clip and open it from **Clips to Trim**. Quit and relaunch from Start to check the queue persists. If migrating a development machine, re-enable sign-in launch from the installed copy so Windows uses its executable.

## Test an upgrade before publishing

During an update, capture continues while the package downloads. Finish any active trim export before restarting. The recorder finishes requested replay saves before exiting.

1. Run the installer workflow with **local_update_test** checked. Install that build on your Windows test account.
2. Run the workflow again with the box unchecked. Extract the second run’s **Klippit-Windows-x64-update** artifact directly into `%LOCALAPPDATA%\Klippit-update-test`. The directory should contain `releases.win.json` and the full `.nupkg`.
3. In the first build, save a replay and quit Klippit. Start it again and wait for the tray to offer the second version automatically. Select **Download update**, wait for **Restart and update**, and verify that replay still works before restarting. After relaunch, check the saved clip, queue, settings and sign-in preference. The newly installed build now checks GitHub.

## Ship a Windows update

Keep `main` releasable. Open a pull request from a short-lived branch when you want to review a change before merging. The **Windows build and tests** check compiles both executables, runs the regression suite and starts the staged editor with the deployed dependencies. A normal push to `main` updates the source. The manual installer workflow is available when you want a downloadable test build.

When a commit on `main` is ready to ship, push a stable version tag. Pick a version higher than the latest release and any test version installed on the PC receiving the update. Replace the example tag below with the version you want to ship.

```sh
git switch main
git pull --ff-only
git tag -a v0.2.2 -m "Klippit 0.2.2"
git push origin v0.2.2
```

The tag runs the Windows build, regression tests and startup smoke test. After they pass, Actions publishes Setup, the release manifest and the matching update package together. First-time users download Setup from GitHub Releases. Existing users choose **Check for updates…** in the tray. Klippit downloads the release and waits for them to choose **Restart and update**.
