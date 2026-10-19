# YT2Ogg

Download YouTube videos as **Opus audio (.ogg)** with a clean, cross-platform Slint C++ GUI.

Requires [yt-dlp](https://github.com/yt-dlp/yt-dlp) to be installed and available in your system PATH or next to the executable.

## Features

- **Direct Opus download** — Keeps the original Opus stream by default, without re-encoding
- **Cross-platform** — Works on Windows, Linux (glibc), and macOS
- **System package support** — Uses yt-dlp from your system PATH or next to the executable
- **Slint C++ GUI** — Native Slint widget style and built-in dark palette
- **Concurrent downloads** — Up to three simultaneous downloads; additional URLs are queued automatically
- **Configurable quality** — Keep the source Opus stream, or re-encode at 96, 80, or 64 kbps via a slider with live labels
- **Lean & optimized** — Stripped binaries, link-time optimization, dead code elimination

## Setup

### First Run

YT2Ogg looks for `yt-dlp` next to the executable, then in your system `PATH`.

### Build-time Requirements

- [xmake](https://xmake.io/) >= 2.8.0
- Slint C++ development package and `slint-compiler` (the `slint-cpp` AUR package on Arch)
- C++26 compiler (GCC, Clang, MSVC)

## Building

Slint and its compiler must be installed and discoverable by xmake.

### Linux (Arch/Debian/Ubuntu)

```bash
# Install dependencies
# Arch:
sudo pacman -S xmake
# Install slint-cpp from the AUR using your preferred AUR helper.
# slint-cpp provides both the C++ SDK and slint-compiler.

# Build
xmake

# Package
xmake package-yt2ogg
```

### macOS and Windows

Install xmake and the Slint C++ SDK for your platform. Put `slint-compiler` on `PATH`; if the SDK
is not installed in a standard location, set `SLINT_INCLUDE_DIR` to its `include/slint` directory
and `SLINT_LIBRARY_DIR` to its library directory. Then run `xmake`.

## Usage

1. Run `YT2Ogg` (or `YT2Ogg.exe` on Windows)
   - Uses yt-dlp next to the application, or resolves it from your system `PATH`
2. Paste a YouTube URL — the input **rejects non-YouTube URLs** (including shorts and youtu.be links are accepted)
3. Click **Download**
   - The button text changes dynamically: **"Download"** when idle, **"Queue"** when all slots are busy, or **"Queue (N)"** showing how many items are waiting
   - Submit more URLs with **Queue**; up to three downloads run simultaneously, and additional downloads start as slots become available
4. Click **Browse...** beside the output folder and choose a folder; audio saves there as `.ogg`

The GitHub project link is available at the bottom of the window.

### History

The history table shows all downloads with four columns:

| Column | Description |
|--------|-------------|
| **Title** | Initially shows the URL (host + path); updates to the real video title once yt-dlp discovers it |
| **Status** | Shows `Queued`, `Downloading...`, `Encoding` (during post-processing), `Complete`, `Cancelled`, or `Failed` |
| **Size** | Shows download progress (e.g. `12.3 MB / 45.1 MB`) while downloading, or final file size when complete |
| **Speed** | Shows transfer speed during download; **after completion, this column shows elapsed time** (e.g. `2m 30s`) |

**History actions:**

- Use the row's **Open** and **Folder** buttons for completed downloads
- Use **Remove** on an entry, or **Clear All** to clear all inactive entries

### Quality Slider

The quality slider has four positions:

| Position | Behavior |
|----------|----------|
| **Source** (default) | Copies the existing Opus stream without re-encoding |
| **96 kbps** | Re-encodes to Opus at 96 kbps |
| **80 kbps** | Re-encodes to Opus at 80 kbps |
| **64 kbps** | Re-encodes to Opus at 64 kbps |

Settings (output folder, quality) are **saved automatically** and restored on next launch.

## Technical Details

### yt-dlp Integration

- Uses `-f bestaudio[acodec=opus] -x --audio-format opus`; videos without an Opus audio stream are not downloaded
- `--no-playlist` ensures only individual videos are downloaded
- The default source setting copies an existing Opus stream without re-encoding
- Optional 96, 80, and 64 kbps settings re-encode using `--postprocessor-args` with explicit `ffmpeg -c:a libopus -b:a` flags
- The final path is read from a marked yt-dlp `after_move` output; completed files are **renamed from `.opus` to `.ogg`**
- If an `.ogg` file with the same name already exists, the app appends `(1)`, `(2)`, etc. to avoid overwrites
- The active history title is updated from yt-dlp metadata via a custom `__YT2OGG_TITLE__` print marker; extraction is shown as "Encoding" and elapsed time is saved in the Speed column when a job finishes

### yt-dlp Download & Verification

- On first run (or if not found in known locations), yt-dlp is downloaded from GitHub releases
- The app **fetches the latest version from the GitHub API** (`/repos/yt-dlp/yt-dlp/releases/latest`); if the API fails, a hardcoded fallback version (`2026.08.19`) is used
- After download, the binary is **verified** by checking executable permissions and running `--version`
- On Linux/macOS, executable permissions are set for all users; on Windows, `.exe` files skip permission changes
- Network timeouts: 10s for API requests, 60s for file download, 3s for verification

### Error Handling

Downloads use numeric status codes for precise error tracking:

| Code | Meaning |
|------|---------|
| `0` | Success |
| `-1` | yt-dlp failed to start |
| `-2` | Download was cancelled |
| `-3` | yt-dlp exited with an error |
| `-4` | Output file not found after processing |

### Build Optimizations

- **Link-time optimization** (`/GL` on MSVC, `-flto` on GCC/Clang)
- **Dead code elimination** (`-Wl,--gc-sections` on Linux)
- **Stripped symbols** in release builds
- Slint provides the complete GUI, uses its native widget style and palette, and is forced to dark mode. The downloader uses the C++ standard library and native process APIs; no Qt dependency is required.

### Cross-platform Notes

| Platform | Notes |
|----------|-------|
| Windows | MSVC build, no console window; uses the native folder picker |
| Linux | Static RPATH (`$ORIGIN`) for bundled yt-dlp; requires `zenity` or `kdialog` for the folder picker |
| macOS | Universal binary support (arm64 + x86_64); uses the native folder picker |

## Source Files

| File | Description |
|------|-------------|
| `src/main.cpp` | Application entry point |
| `src/mainwindow.h` / `mainwindow.cpp` | Slint window controller, history model, download queue, and settings |
| `src/ui/mainwindow.slint` | Slint UI layout and widgets |
| `src/downloadthread.h` / `downloadthread.cpp` | yt-dlp worker thread, progress parsing, file renaming |

## Credits

- [yt-dlp](https://github.com/yt-dlp/yt-dlp) — YouTube downloader
- [Slint](https://slint.dev/) — C++ GUI framework
- [xmake](https://xmake.io/) — Build system
