# KPlayer

**A personal desktop media player for watching anime, built with C++20, Qt Quick, and libmpv.**

I used PotPlayer on Windows, mainly for watching anime. After switching to Linux,
I couldn't find a player that matched my preferences and the experience I was
used to. Instead of continuing to search for alternatives, I started building
KPlayer with the controls, playlists, and subtitle workflow I use most.

This is an app I build primarily for myself, around my own preferences and
viewing workflow, and share publicly in case others find it useful. I know other
projects with similar goals exist; KPlayer isn't intended to replace or compete
with them. PotPlayer is an inspiration, but I am not trying to recreate all of
its features or achieve feature parity.

Linux is the main motivation behind the project. The repository also includes a
native Windows build, runtime deployment, and Windows file associations.

![KPlayer running on Linux with the Traditional Chinese menu open and the playlist on the right](docs/screenshots/kplayer-linux.png)

## Features

- **Series playback:** discover videos in the opened file's folder, sort episode
  names naturally, and play the next item automatically.
- **Subtitles:** select embedded tracks, load external files, set preferred
  languages, and adjust subtitle appearance and timing.
- **Playback controls:** familiar keyboard shortcuts, frame stepping, adjustable
  speed, repeat modes, and A–B looping.
- **Timeline previews:** hover or drag to preview a frame without interrupting
  the current playback.
- **Playlists and favorites:** drag and drop files, search and reorder the queue,
  use named playlists, and save favorite files or URLs.
- **Audio and video adjustments:** equalizer presets, picture controls, aspect
  ratios, and PNG frame capture.
- **Desktop interface:** fullscreen, always on top, a resizable playlist, and
  English and Traditional Chinese interface languages.

Named playlists currently last for the session. Favorites, interface language,
subtitle language preferences, and playlist width are saved across launches.

## Getting started

Build the player using the instructions below, then launch it with or without
media arguments:

```sh
./build/KPlayer
./build/KPlayer "/path/to/episode.mkv"
./build/KPlayer "https://example.com/video.mp4"
```

Launching without arguments opens an idle player. Use **Open Media** (`Ctrl+O`)
to select files, or drop files onto the playlist to queue them. Opening a local
video also adds other videos from its immediate folder in natural filename
order; subfolders are not scanned. Adding or dropping files into the playlist
preserves their explicit order and does not interrupt playback.

The default interface language is Traditional Chinese. To switch to English,
press `F5`, choose **General > Interface language**, and save. The change applies
immediately and is remembered for the next launch.

## Build and run

### Requirements

- A C++20 compiler
- CMake 3.24 or newer and Ninja
- Qt 6.11.2 or newer, including Qt Quick, OpenGL, SVG, and Linguist tools
- libmpv development headers and library
- pkg-config on Linux and macOS
- Qt Test when building the optional tests
- An OpenGL-capable display; on Linux, installed system fonts are also needed

### Linux

From the repository root:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/KPlayer
```

If Qt is installed outside the system search paths, add
`-DCMAKE_PREFIX_PATH=/path/to/your/Qt/installation` to the configure command.
You can also open `CMakeLists.txt` in Qt Creator and select a Qt 6.11.2+ kit.

### Arch Linux package

Install the dependencies, including the tools used to compile translations:

```sh
sudo pacman -S --needed base-devel cmake ninja pkgconf qt6-base qt6-declarative qt6-svg qt6-tools qt6-translations mpv
```

The installed Qt packages must satisfy the version requirement above. Build as
your regular user, then install the generated package:

```sh
bash scripts/build-arch.sh
sudo pacman -U build/arch/kplayer-0.1.0-1-$(uname -m).pkg.tar.zst
```

The script packages the current working source, including local edits, into
`build/arch/`. It excludes the optional sample video. The package installs
`/usr/bin/KPlayer`, a desktop launcher, and media file associations.

Additional script arguments are passed to `makepkg`, such as `--syncdeps` to
install missing dependencies. Set `CMAKE_BUILD_PARALLEL_LEVEL` to override the
default of four build jobs.

### Windows (MSYS2 UCRT64)

Install [MSYS2](https://www.msys2.org/). In its **UCRT64** shell, update with
`pacman -Syu`. If the update closes the shell, reopen it and repeat the command
before installing the dependencies:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-declarative mingw-w64-ucrt-x86_64-qt6-svg mingw-w64-ucrt-x86_64-qt6-tools mingw-w64-ucrt-x86_64-mpv
```

From the repository root in PowerShell:

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cmake -S . -B build-windows -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64 `
    -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe `
    -DKPLAYER_BUILD_TESTS=ON
cmake --build build-windows --parallel
ctest --test-dir build-windows --output-on-failure
.\build-windows\KPlayer.exe
```

Adjust the paths if MSYS2 is installed elsewhere. Use a separate build directory
from Linux or WSL because CMake caches depend on the platform and compiler.

The build copies Qt plugins, QML imports, libmpv, and runtime DLLs beside
`KPlayer.exe`. Keep the whole build folder together when moving or sharing it.
For development with UCRT64's `bin` directory already on `PATH`, runtime copying
can be disabled with `-DKPLAYER_DEPLOY_WINDOWS_RUNTIME=OFF`.

On launch, KPlayer registers common video formats for the current user's
**Open with** and **Default apps** menus. The first launch opens Windows Default
apps so you can choose the file types to associate with KPlayer. You can reopen
that page from the application menu or run `KPlayer.exe --register-file-types`.
Existing default choices are preserved. After moving the executable, launch it
once from its new location to update registration.

## Controls

Most playback shortcuts apply when focus is in the video area. Text fields and
playlist selections keep their own keyboard behavior.

| Shortcut | Action |
| --- | --- |
| `Ctrl+O` / `F3` | Open media |
| `F4` | Close playback and keep the queue |
| `F5` | Open Advanced Settings |
| `Ctrl+L` / `F6` | Show or hide the playlist |
| `Ctrl+F` | Search the playlist |
| `Space` | Play or pause |
| `Enter` | Toggle fullscreen; play the selection when the playlist has focus |
| `Escape` | Leave fullscreen |
| `Left` / `Right` | Seek backward / forward 10 seconds |
| `Ctrl+Left` / `Ctrl+Right` | Seek backward / forward 30 seconds |
| `Shift+Left` / `Shift+Right` | Seek backward / forward 60 seconds |
| `Up` / `Down` | Raise / lower volume by 5% |
| `M` | Toggle mute |
| `PgUp` / `PgDown` | Previous / next playlist item |
| `X` / `C` | Decrease / increase playback speed by 0.1× |
| `Z` | Switch between normal speed and the last adjusted speed |
| `D` / `F` | Previous / next frame, pausing playback |
| `L` | Open subtitle selection |
| `Alt+O` | Load external subtitles |
| `Alt+H` | Show or hide subtitles |
| `Alt+L` | Cycle subtitle tracks |

- Double-click the video to play or pause; middle-click to toggle fullscreen.
- Right-click the video or click the top-left KPlayer button to open the menu.
- Scroll over the video to adjust volume; drag the video to move the window.
- In fullscreen, move near the bottom for controls or the right edge for the playlist.
- Left-click Previous/Next to seek 10 seconds; right-click to change playlist items.
- Use the bottom settings button to open the floating **Control Panel** for audio,
  video, subtitle, and playback adjustments.

For subtitle language preferences, open Advanced Settings and enter language
codes in priority order, such as `zh,en`. Leave the field blank for automatic
selection. Subtitle font and alignment controls apply to plain text subtitles;
styled ASS or bitmap subtitles may retain their own appearance.

## Development

The interface is written in QML, with C++ handling playback and desktop integration.

| Path | Purpose |
| --- | --- |
| `qml/Main.qml` | Window composition and action wiring |
| `qml/Theme.qml` | Shared colors, typography, and sizing |
| `qml/` | Playback controls, menus, panels, and reusable UI components |
| `src/playback/` | libmpv playback, frame previews, playlists, and favorites |
| `src/integration/` | Interface language, file discovery, and desktop integration |
| `translations/` | English and Traditional Chinese translation sources |
| `assets/icons/` | Embedded application and interface icons |
| `tests/tst_player.cpp` | Qt tests for playback and interface behavior |
| `packaging/` | Linux and Windows packaging resources |

KPlayer uses Qt Quick's OpenGL backend to render libmpv video. Timeline previews
use a separate silent libmpv instance.

### Tests

Enable the optional tests when configuring:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKPLAYER_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Tests generate temporary media fixtures and cover playlist editing, favorites,
file discovery, language switching, shortcuts, playback controls, and previews.
They need an OpenGL-capable desktop. For headless Linux, use an X server with
OpenGL support and a window manager for fullscreen and focus tests.

To test an existing seekable video longer than three seconds on Windows:

```powershell
$env:KPLAYER_TEST_MEDIA = 'C:\path\to\video.mp4'
.\build-windows\KPlayerTests.exe externalMediaPlayback
Remove-Item Env:KPLAYER_TEST_MEDIA
```

This optional test is skipped when `KPLAYER_TEST_MEDIA` is unset.

### QML tools

For QML linting:

```sh
cmake --build build --target all_qmllint
```

For hot reload, configure and build in Debug mode, then run:

```sh
cmake --build build --target KPlayer_qmlpreview
```

This requires Qt's `qmlpreview` tool. Save QML edits to update the preview;
structural changes may require a restart. C++ changes require rebuilding.

## Troubleshooting

**Text appears as squares on Linux or WSL:** install a system font and a CJK font
for Traditional Chinese. On Arch Linux, use `ttf-dejavu` and `noto-fonts-cjk`,
then run `fc-cache -f` and reopen KPlayer. On Ubuntu or Debian, `fonts-dejavu-core`
provides a basic font. Use `fc-list` and `fc-match sans-serif` to check font setup.
WSL can also use Windows' Microsoft JhengHei font at `/mnt/c/Windows/Fonts/msjh.ttc`.

**Playback tests stall under WSL:** try software OpenGL with a single render thread:

```sh
QT_QPA_PLATFORM=wayland LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe QSG_RENDER_LOOP=basic ctest --test-dir build --output-on-failure
```

If frames appear corrupted with llvmpipe, try `GALLIUM_DRIVER=softpipe`.
Enable preview diagnostics with `QT_LOGGING_RULES="kplayer.preview.debug=true"`.

## Contributing

Bug reports and focused pull requests are welcome. For a bug report, include your
operating system, Qt and mpv versions, steps to reproduce, and relevant logs.
For playback issues, describe the media format and subtitle type if applicable.

For a larger feature or interface change, open an issue first to discuss how it
fits the project. KPlayer's direction follows my personal viewing workflow, so
feature requests are considered within that scope.
