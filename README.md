# YT2Ogg

Download YouTube videos as **Opus audio (.ogg)** with a clean, cross-platform Qt6 GUI.

Uses an installed [yt-dlp](https://github.com/yt-dlp/yt-dlp) when available; otherwise downloads it automatically from GitHub.

## Features

- **Direct Opus download** — Keeps the original Opus stream by default, without re-encoding
- **Cross-platform** — Works on Windows, Linux (glibc), and macOS
- **System package support** — Uses yt-dlp from your system PATH when installed
- **Auto-downloads yt-dlp** — Fetches the latest yt-dlp from GitHub automatically on first run; falls back to a hardcoded version if the GitHub API is unreachable
- **Clean Qt6 GUI** — Modern, responsive interface with download history and Fusion style for consistent cross-platform appearance
- **Concurrent downloads** — Up to three simultaneous downloads; additional URLs are queued automatically
- **Configurable quality** — Keep the source Opus stream, or re-encode at 96, 80, or 64 kbps via a slider with live labels
- **System tray** — Minimize to tray; click the tray icon to quickly restore the window and focus the URL input
- **Lean & optimized** — Stripped binaries, link-time optimization, dead code elimination

## Setup

### First Run

If yt-dlp is not found in your system PATH or next to the executable, a **setup dialog** appears automatically. It downloads the latest yt-dlp from GitHub with a progress indicator and verifies the binary is functional before letting you proceed. You can cancel the download and the application will exit.

### Build-time Requirements

- [xmake](https://xmake.io/) >= 2.8.0
- Qt6 development packages (`qt6-base-dev`, `qt6-tools-dev` or equivalent)
- C++26 compiler (GCC, Clang, MSVC)

## Building

If multiple Qt versions are installed, select Qt 6 explicitly before building:

```bash
xmake f --qt_sdkver=6
```

### Linux (Arch/Debian/Ubuntu)

```bash
# Install dependencies
# Arch:
sudo pacman -S xmake qt6-base qt6-tools

# Build
xmake

# Package
xmake package-yt2ogg
```

### macOS

```bash
# Install dependencies
brew install xmake qt@6

# Build
xmake

# Package
xmake package-yt2ogg
```

### Windows

```powershell
# Install Qt6 via Qt Online Installer (Desktop MSVC build)
# Install xmake: choco install xmake or from https://xmake.io

# Build
xmake

# Package
xmake package-yt2ogg
```

## Usage

1. Run `YT2Ogg` (or `YT2Ogg.exe` on Windows)
   - Uses yt-dlp from your system PATH, or downloads a copy if none is installed
2. Paste a YouTube URL — the input **rejects non-YouTube URLs** (including shorts and youtu.be links are accepted)
3. Click **Download**
   - The button text changes dynamically: **"Download"** when idle, **"Queue"** when all slots are busy, or **"Queue (N)"** showing how many items are waiting
   - Submit more URLs with **Queue**; up to three downloads run simultaneously, and additional downloads start as slots become available
4. Audio saves as `.ogg` in your chosen folder

### History Table

The history table shows all downloads with four columns:

| Column | Description |
|--------|-------------|
| **Title** | Initially shows the URL (host + path); updates to the real video title once yt-dlp discovers it |
| **Status** | Shows `Queued`, `Downloading...`, `Encoding` (during post-processing), `Complete`, `Cancelled`, or `Failed` |
| **Size** | Shows download progress (e.g. `12.3 MB / 45.1 MB`) while downloading, or final file size when complete |
| **Speed** | Shows transfer speed during download; **after completion, this column shows elapsed time** (e.g. `2m 30s`) |

**Interacting with history entries:**

- **Double-click** a completed download to open the file in your system file manager
- **Right-click** for a context menu: `Open File`, `Open Containing Folder`, `Remove`
- Active downloads **cannot be removed** from the history via the context menu
- **Clear All** removes all completed/cancelled entries and empties the queue

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
- The final path is read from yt-dlp's `after_move:filepath` output; completed files are **renamed from `.opus` to `.ogg`**
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
- **Fusion style** for consistent cross-platform appearance

### Cross-platform Notes

| Platform | Notes |
|----------|-------|
| Windows | MSVC build, no console window |
| Linux | Static RPATH (`$ORIGIN`) for bundled yt-dlp |
| macOS | Universal binary support (arm64 + x86_64) |

### Close Behavior

Closing the window **cancels all active downloads** and quits the application. The system tray icon provides a quick "Open" option to restore the window.

## Project Structure

```
YT2Ogg/
├── xmake.lua            # Build configuration
├── src/
│   ├── main.cpp         # Application entry point
│   ├── mainwindow.h     # Main window header
│   ├── mainwindow.cpp   # Main window implementation (UI, history, queue)
│   ├── downloadthread.h # yt-dlp worker thread
│   ├── downloadthread.cpp # Progress parsing, file renaming
│   ├── ytdlmanager.h    # Auto-downloader for yt-dlp
│   └── ytdlmanager.cpp  # GitHub API, download, verification
└── README.md
```

The yt-dlp binary is downloaded at first run and stored next to the executable.

## License

MIT

## Credits

- [yt-dlp](https://github.com/yt-dlp/yt-dlp) — YouTube downloader
- [Qt6](https://www.qt.io/) — GUI framework
- [xmake](https://xmake.io/) — Build system
