# Contributing to Klippit

Bug reports, feature ideas, documentation fixes, and testing on different Windows hardware are all welcome. You can help even if you have never worked on a C++ project.

## Report a bug or suggest a feature

Search [existing issues](https://github.com/yahya-kousa/Klippit/issues) first. If someone has reported the same problem, a new reproduction or a test result from a different machine can help.

- [Report a bug](https://github.com/yahya-kousa/Klippit/issues/new?template=bug_report.yml) with the Klippit version, your operating system, and the shortest steps that reproduce it. For recording problems, include the GPU, encoder and capture settings if you know them.
- [Suggest a feature](https://github.com/yahya-kousa/Klippit/issues/new?template=feature_request.yml) by describing what you want to do and where the current workflow gets in the way.

Logs and small sample clips are optional. Issues are public, so remove personal paths, account names, conversations, and other private material from anything you share.

## Open a pull request

1. Fork Klippit, create a short-lived branch from `main`, and follow the [source build instructions](README.md#build-from-source).
2. Keep the change focused. Small fixes and documentation improvements can go straight to a PR. For a larger feature or a change to replay or export behavior, opening an issue first gives everyone a chance to discuss the tradeoffs.
3. For code changes, run the [regression suite](README.md#run-the-regression-tests) with Python, FFmpeg and ffprobe available. Include the OS and any manual checks you ran. For hardware-dependent changes, describe the configuration you could test and any coverage gaps.
4. Open a PR against `main`. Summarize the user-visible change and link any related issue. The Windows workflow builds the editor and recorder, runs the regression tests, and checks staged app startup.

If you change performance-sensitive behavior, include the workload, hardware, settings, before-and-after measurements, and how to reproduce them when available. The [benchmark guide](benchmarks/README.md) has existing procedures.

Klippit's source is licensed under the [GNU General Public License version 3](LICENSE).
