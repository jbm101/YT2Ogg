#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_root"

export PATH="$HOME/.cargo/bin:$PATH"

for command in curl git python3 rustup cargo xmake slint-compiler \
    upx x86_64-w64-mingw32-gcc x86_64-w64-mingw32-strip; do
    if ! command -v "$command" >/dev/null 2>&1; then
        printf 'Missing required command: %s\n' "$command" >&2
        exit 1
    fi
done

slint_version="$(slint-compiler --version | awk '{print $2}')"
if [[ -z "$slint_version" ]]; then
    echo "Could not determine the installed Slint compiler version." >&2
    exit 1
fi

rustup target add x86_64-pc-windows-gnu

slint_source="$project_root/build/deps/slint-$slint_version"
if [[ ! -d "$slint_source/.git" ]]; then
    mkdir -p "$(dirname "$slint_source")"
    git clone --depth 1 --branch "v$slint_version" \
        https://github.com/slint-ui/slint.git "$slint_source"
fi
slint_size_patch="$project_root/scripts/slint-windows-size.patch"
if ! git -C "$slint_source" apply --reverse --check "$slint_size_patch" >/dev/null 2>&1; then
    git -C "$slint_source" apply "$slint_size_patch"
fi

assets_directory="$project_root/assets"
mkdir -p "$assets_directory"
release_json="$(curl -fsSL -H 'User-Agent: YT2Ogg-build' \
    https://api.github.com/repos/yt-dlp/yt-dlp/releases/latest)"
readarray -t release_values < <(python3 -c '
import json, sys
release = json.loads(sys.stdin.read())
asset = next((asset for asset in release["assets"] if asset["name"] == "yt-dlp.exe"), None)
if not asset:
    raise SystemExit("The latest yt-dlp release has no yt-dlp.exe asset.")
print(release["tag_name"].removeprefix("v"))
print(asset["browser_download_url"])
' <<< "$release_json")
release_version="${release_values[0]}"
download_url="${release_values[1]}"
yt_dlp="$assets_directory/yt-dlp.exe"
version_file="$assets_directory/yt-dlp-version.txt"

if [[ ! -f "$yt_dlp" || ! -f "$version_file" ||
    "$(cat "$version_file")" != "$release_version" ]] ||
    ! python3 -c 'import pathlib, sys; p=pathlib.Path(sys.argv[1]).read_bytes(); assert p[:2] == b"MZ" and p.find(b"PE\0\0", 0x40) >= 0' "$yt_dlp" 2>/dev/null; then
    echo "Downloading yt-dlp $release_version..."
    temporary_file="$assets_directory/yt-dlp.download.exe"
    trap 'rm -f "$temporary_file"' EXIT
    curl -fL --retry 3 "$download_url" -o "$temporary_file"
    python3 -c 'import pathlib, sys; p=pathlib.Path(sys.argv[1]).read_bytes(); assert p[:2] == b"MZ" and p.find(b"PE\0\0", 0x40) >= 0, "Downloaded yt-dlp is not a Windows executable"' "$temporary_file"
    mv "$temporary_file" "$yt_dlp"
    printf '%s' "$release_version" > "$version_file"
    trap - EXIT
else
    echo "yt-dlp $release_version is already current."
fi

generated_include="$project_root/build/slint-generated"
mkdir -p "$generated_include"
export SLINT_GENERATED_INCLUDE_DIR="$generated_include"
export CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER="$(command -v x86_64-w64-mingw32-gcc)"
export CC_x86_64_pc_windows_gnu="$(command -v x86_64-w64-mingw32-gcc)"
export CARGO_PROFILE_RELEASE_OPT_LEVEL=z
export CARGO_PROFILE_RELEASE_LTO=fat
export CARGO_PROFILE_RELEASE_CODEGEN_UNITS=1

echo "Building Slint $slint_version for Windows..."
cargo build --manifest-path "$slint_source/api/cpp/Cargo.toml" \
    --release --target x86_64-pc-windows-gnu \
    --no-default-features --features backend-winit,renderer-software

export SLINT_INCLUDE_DIR="$slint_source/api/cpp/include"
export SLINT_LIBRARY_DIR="$slint_source/target/x86_64-pc-windows-gnu/release"

echo "Building standalone Windows x64 executable..."
xmake f -p windows -a x64 -m release --toolchain=mingw
xmake -r

executable="$project_root/build/windows/x64/release/YT2Ogg.exe"
x86_64-w64-mingw32-strip --strip-all "$executable"
upx --best --lzma --force "$executable"
upx -t "$executable"

echo "Build complete: build/windows/x64/release/YT2Ogg.exe"
