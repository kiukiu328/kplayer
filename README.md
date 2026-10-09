# KPlayer

**A PotPlayer-inspired desktop media player built for watching anime on Linux.**

I like using PotPlayer to watch anime on Windows, but I couldn't find a Linux
replacement that felt right for me. So I started building my own. KPlayer aims
to bring the familiar controls, playlists, and subtitle workflow I enjoy to my
Linux desktop.

I know about rein_player, which uses Flutter. I wanted a more native desktop
solution built with C++ and Qt, so I started KPlayer.

Built with **C++20, Qt Quick, and libmpv**, KPlayer plays local media files and
URLs in a compact desktop interface. Linux is the motivation behind the project;
the repository also includes Windows build and file-association support.

![KPlayer on Linux with its Traditional Chinese menu open, playback controls below, and playlist on the right](docs/screenshots/kplayer-linux.png)

*KPlayer running on Linux, shown idle with the application menu open.*

## What it can do

- **Watch a series:** open an episode and add videos from the same folder to the
  playlist automatically, with automatic playback of the next item.
- **Work with subtitles:** select embedded tracks, load external subtitle files,
  and save preferred subtitle languages such as `zh,en`.
- **Use familiar controls:** PotPlayer-style keyboard shortcuts, playback speed
  controls, frame stepping, repeat modes, fullscreen, and always-on-top.
- **Find a scene:** seek with keyboard shortcuts or hover over the timeline for
  a frame preview without interrupting playback.
- **Organize your media:** drag and drop files, search and reorder the queue,
  switch between named playlists, and save favorites.
- **Adjust the picture:** choose an aspect ratio or change brightness, contrast,
  and saturation during playback.

The interface currently uses Traditional Chinese menus with some English labels.
Named playlists last for the current session; favorites and subtitle language
preferences are saved across launches.

See [Build and run](#build-and-run) to try it, or [Player controls](#player-controls)
for the detailed controls and shortcuts.

## UI organization

All colors are declared as named QML properties in `qml/Theme.qml`, along with
shared typography and sizing. Change these properties to restyle the interface.
`PlayerIcon.qml` renders the embedded SVG assets in `assets/icons/` with the
theme's icon colors; `IconButton.qml`, `PlayerSlider.qml`, and `PlaylistTab.qml`
are reusable controls. Each major region has its own component:
`TitleBar.qml`, `VideoStage.qml`, `PlaybackBar.qml`, and `PlaylistPanel.qml`.
`Main.qml` assembles the window and connects component signals to window and
playback actions. Buttons have hover styling, accessible labels, and mouse and
keyboard activation. Buttons and controls show hover tooltips.

## Player controls

The libmpv backend in `src/playback/` renders media in the play area. The player
starts idle when launched without a media argument. Open Media can select files, and file paths or URLs supplied as command-line arguments are
queued and the first valid entry starts playing immediately.

- On Windows, startup registers common video formats (including MP4, MKV, AVI,
  MOV, WebM, WMV, MPEG, and TS) for **Open with** and **Default apps**, for the
  current user without administrator rights. The first launch opens KPlayer's
  Default apps page. Select KPlayer for the video types you want to open with a
  double click. Windows requires this choice in its own UI; KPlayer does not
  overwrite existing default choices. **設為預設影片播放器…** in Settings or
  the application menu opens that page again. Moving the executable requires
  launching it from its new location once to refresh registration.
- Opening local videos from the file manager, command line, or Open Media also
  adds videos from the same folders to the active playlist, without duplicates
  or scanning subfolders. The queue is sorted in natural filename order (episode 2
  before episode 10). The requested file stays selected and starts playing.
  Discovery runs only when a file is opened and stays in its immediate folder;
  launching the player alone does not search for videos. TS/MTS files need a
  transport-stream header so TypeScript sources are ignored.
  Add to Playlist and drag-and-drop retain explicit queue order.
- `--register-file-types` registers the formats, opens Default apps, and exits.

- Top-right Close, Minimize, Maximize/Restore, and Full Screen/Exit Full Screen
  buttons control the window. Exiting fullscreen restores its prior normal or
  maximized state. Fullscreen hides the title bar, playlist, and controls.
  Move the cursor to the bottom to reveal the timeline and controls over the video.
  Return toggles fullscreen; Escape or the play area's middle click also leaves it.
  Exiting restores the previous playlist and control visibility.
- Hold the left mouse button and drag the play area to move the window.
- Double-click the play area with the left mouse button to play or pause.
- Drag any window edge or corner to resize. Resizing is available in the normal
  window state and respects the minimum window size.
- Click the Playlist button to hide or show the playlist. Hiding it expands the
  play area and keeps the queue and current selection. Drag the divider at the
  playlist's left edge to resize it; its width is saved across launches.
- Drag one or more files onto the playlist to append them in drop order.
  Double-click a playlist entry, or select it and press Enter, to play it.
  Dropping files queues them without interrupting playback; directories and
  missing files are ignored. Open Media supports selecting multiple files and
  starts the first new entry.
- Add (新增), the Browse tab, and **Add to playlist…** queue files without
  interrupting playback. Remove (移除) or Delete removes the selected entries;
  removing the current entry selects its next neighbor (or the previous final
  entry). Playback stops if the media changes. Clear Playlist empties the queue
  and stops playback.
- Each playlist entry shows its queue index, including while searching.
  Shift-click or Shift+Up/Down selects a range; Ctrl-click toggles individual
  entries, and Ctrl+A selects all visible entries. Drag selected entries to the
  insertion line to move them together, preserving their order. Drag near the
  list's top or bottom to scroll. Remove or Delete removes the selected entries.
- The ordering buttons move the selected entries to the top, up, down, or bottom.
  Ctrl+Up/Ctrl+Down also reorder a focused playlist selection. Sort (排序)
  offers natural, case-insensitive title order in either direction. Queue edits
  preserve the current track without restarting it when its media stays in the queue.
- Type in the playlist search field to filter titles without changing the queue
  or playback order. Enter plays the selected matching entry; Escape clears the
  search. The Search Media button or Ctrl+F reveals the playlist and focuses
  search. The arrow above the queue reveals the current track.
- The down arrow above the queue opens a compact playlist selector. Its blue
  radio marker identifies the active playlist. Use **+** to create a named
  playlist, **編輯專輯** to rename it, and **刪除專輯** to delete it. The default
  playlist (預設) cannot be renamed or deleted. Switching playlists stops playback
  and restores that playlist's queue and current track; tabs also switch playlists.
- Click the top-left KPlayer button, right-click it or the video area, or use the
  bottom Menu button to open the reference-style Traditional Chinese menu.
  It includes shortcut labels, checked states, separators, and nested menus;
  Press Escape or click outside to dismiss it.
  Open (開啟) contains file queueing, search, and Clear Playlist actions.
  The Playback menu offers repeat off, repeat current track, or repeat the playlist;
  manual Previous/Next track navigation still stops at the queue boundaries.
  The pin button toggles Always on Top and highlights while enabled.
- **我的最愛** saves the current file or URL as a favorite across launches. Click a
  saved entry to queue and play it; toggle the current entry to remove it, or use
  Clear Favorites to remove all saved entries. Missing files remain saved for
  disconnected drives and show an error when they cannot be opened.
- **比例** selects the original aspect ratio, 4:3, 16:9, 16:10, 1:1, or 2.35:1.
  **濾鏡 > 影像調整…** adjusts brightness, contrast, and saturation immediately,
  with a Reset button to restore neutral values. These video settings apply to
  the current session and do not alter the media files.
- Keyboard shortcuts: F3 or Ctrl+O opens media, F4 closes playback while keeping
  the queue, F5 opens Advanced Settings, F6 or Ctrl+L toggles the playlist, and Escape
  leaves fullscreen. With focus in the video area, Return toggles fullscreen, Space
  toggles playback, M toggles mute, Left/Right seek 10 seconds, and Up/Down adjust
  volume by 5%. Text fields, focused controls, and playlist selection keep their
  own keyboard behavior.
- In **Advanced Settings** (F5), drag the title bar to move the panel. Under
  **General > Interface language**, choose Traditional Chinese or English and
  Save to apply it immediately and remember it for the next launch.
- The bottom settings button opens a floating **Control Panel** with Audio,
  Video, Subtitle, and Playback tabs. Drag its title bar to move it, use the pin
  to keep it on top, and drag the yellow title-bar slider to change this panel's
  opacity (25–100%). Close or Escape hides the panel; F5 still opens Advanced Settings.
  Audio provides a ten-band equalizer with presets, preamp, mute, and volume.
  Video provides brightness, contrast, saturation, hue, soften, sharpen, deblock,
  and PNG frame capture. Subtitle provides font, size, vertical position,
  left/center/right alignment, and timing in 0.1-second steps. Font and alignment
  controls apply to plain text subtitles; styled ASS and bitmap tracks may retain
  their own styling. Playback provides ±5/±30-second seeking, speed, and A–B
  looping. Click A and B to mark the current playback positions; Undo clears
  the loop, and stopping or changing files clears it automatically.
  Adjustments apply immediately for the current session.
- The **字幕** menu shows/hides subtitles, loads external subtitle files, and
  selects embedded or external tracks by title/language/filename. Hiding subtitles
  keeps the selected track; selecting or loading a track makes it visible again.
  Start playback before adding subtitles. Loaded tracks clear when playback closes
  or changes files.
  In **進階設定** (F5), choose a default subtitle language or enter language codes
  in priority order (for example, `zh,en`). Save keeps the preference across
  launches and applies it when opening media. Leave it blank for automatic selection.
- Additional PotPlayer-style shortcuts (with focus in the video area):

  | Keys | Action |
  | --- | --- |
  | Z | Toggle between 1.0× and the last non-normal speed |
  | X / C | Decrease / increase speed by 0.1× (0.2–16.0× by default) |
  | L | Open the subtitle selection menu |
  | Alt+O | Add subtitle files |
  | Alt+H | Show / hide subtitles |
  | Alt+L | Cycle subtitle tracks |
  | PgUp / PgDown | Previous / next playlist item |
  | Ctrl+Left / Ctrl+Right | Seek backward / forward 30 seconds |
  | Shift+Left / Shift+Right | Seek backward / forward 60 seconds |
  | Backspace | Seek to the beginning |
  | Ctrl+Backspace | Seek to the midpoint |
  | Shift+Backspace | Seek to 30 seconds before the end |
  | D / F | Previous / next frame (pauses playback) |

  Bindings were checked against [PotPlayer's bundled English menu definitions](https://github.com/Onemeshnik/PotPlayer-Median-subtitles-installer/blob/master/%7Bapp%7D/Language/English.ini).
  Playback speed also appears in the Playback menu. **Custom playback speed…**
  or Advanced Settings (F5) lets you enter a speed in 0.1× steps and change the
  maximum (16× by default, configurable from 1× to 100×). The maximum is saved
  across launches; lowering it also clamps the current speed. Setting changes show blue
  hints at the top-left of the play area for two seconds, including normal speed
  (`1.0x`), volume, mute, subtitles, repeat, and window controls.
- Play toggles play/pause. Stop clears playback; pressing Play again restarts
  the selected media from the beginning.
- Click the audio button to mute or unmute without changing the saved volume.
  The volume slider controls 0–100%; adjusting it above zero also unmutes audio.
- Left-click Previous/Next to rewind/fast-forward 10 seconds. Right-click them
  to play the previous/next playlist entry. Navigation stops at the playlist
  boundaries, and reaching the end of a file automatically plays the next entry.
- The seek slider and elapsed/total timestamps follow the actual playback state.
  Drag or click the seek slider to seek; it is disabled for unseekable media.
- Hover over the seek slider to preview the frame and timestamp at that point;
  dragging also shows the target frame. Preview decoding uses a separate silent
  libmpv instance and does not seek or pause the playing media. Audio-only files
  show the timestamp with an unavailable-preview message.
- A middle click in the play area toggles fullscreen immediately. A middle
  double-click counts as one fullscreen toggle. Left double-click toggles
  play/pause.

## Requirements

- Qt 6.11.2 or newer with Qt Quick, Qt OpenGL, Qt SVG, and its development tools
- libmpv development headers and library (on Windows, found through CMake;
  on Linux and macOS, discoverable as `mpv` through pkg-config)
- pkg-config (Linux and macOS)
- CMake 3.24 or newer
- A C++20 compiler
- Ninja
- A system font available to fontconfig on Linux (for example, DejaVu Sans)

## Build and run

No media file is needed to open the player.

From the project directory (Linux):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build && ./build/KPlayer
```

If Qt is installed outside the system search paths, add
`-DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.2.0/gcc_64` to the configure command,
using your actual Qt installation prefix.

You can also open `CMakeLists.txt` in Qt Creator and select a Qt 6.11.2+ kit.
On Windows the executable is `build/KPlayer.exe`; on macOS it is
`build/KPlayer.app/Contents/MacOS/KPlayer`.

### Arch Linux package

Install the build dependencies:

```sh
sudo pacman -S --needed base-devel cmake ninja pkgconf qt6-base qt6-declarative qt6-svg mpv
```

Qt packages must be version 6.11.2 or newer. From the project directory, build
an installable package as your regular user:

```sh
bash scripts/build-arch.sh
sudo pacman -U build/arch/kplayer-0.1.0-1-$(uname -m).pkg.tar.zst
```

The script packages the current working source, including local edits, and writes
the source archive, checksummed PKGBUILD, build files, and package to `build/arch/`.
It excludes the optional sample video. The package installs `/usr/bin/KPlayer`
and a desktop launcher with media file associations. Install `noto-fonts-cjk`
if the Traditional Chinese labels appear as squares.

Builds use your makepkg configuration; additional arguments are passed to
`makepkg` (for example, `--syncdeps` to install missing dependencies).
Set `CMAKE_BUILD_PARALLEL_LEVEL` to change the default of four build jobs.

### Native Windows build (MSYS2 UCRT64)

Install [MSYS2](https://www.msys2.org/) and CMake 3.24 or newer. In the MSYS2
UCRT64 shell, update the installation with `pacman -Syu`. If the update closes
the shell, reopen it and repeat the command to finish updating. Then install:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-declarative mingw-w64-ucrt-x86_64-qt6-svg mingw-w64-ucrt-x86_64-qt6-tools mingw-w64-ucrt-x86_64-mpv
```

From the project directory in PowerShell, build and run with:

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

Adjust the MSYS2 paths if it is installed elsewhere. Use a separate build
directory from Linux or WSL: CMake caches are specific to their platform and
compiler. The Windows build copies Qt plugins, QML imports, libmpv, and their
runtime DLLs beside `KPlayer.exe`, so Explorer and file associations can launch
it directly without changing `PATH`. Keep the whole build folder together. Set
`-DKPLAYER_DEPLOY_WINDOWS_RUNTIME=OFF` only for development builds where the
UCRT64 `bin` directory is already on `PATH`.
The tests need a Windows desktop with OpenGL support.

To also smoke-test an existing video (including paths with non-ASCII characters),
run the optional test from PowerShell:

```powershell
$env:KPLAYER_TEST_MEDIA = 'C:\path\to\video.mp4'
.\build-windows\KPlayerTests.exe externalMediaPlayback
Remove-Item Env:KPLAYER_TEST_MEDIA
```

The test checks playback, seeking, mute/volume controls, and timeline previews.
Choose a seekable video longer than three seconds. Without `KPLAYER_TEST_MEDIA`,
this optional test is skipped and the other tests use generated fixtures.

The optional video remains an external file, not an embedded resource.
The application selects Qt Quick's OpenGL backend for libmpv rendering.

### WSL title displays as squares

For Chinese labels, the application reuses Windows' installed Microsoft
JhengHei font when running in WSL without a Traditional Chinese system font.
This fallback requires the usual `/mnt/c/Windows/Fonts/msjh.ttc` mount. On other
Linux systems, install a CJK font such as Noto Sans CJK to display these labels.

Minimal WSL distributions may have no fonts installed. Qt's Wayland title bar
then displays square boxes instead of `KPlayer`; QML text can be affected too.
Check that `fc-list` lists fonts and `fc-match sans-serif` returns a usable font.

On Arch Linux, install a font and refresh the cache:

```sh
sudo pacman -S --needed ttf-dejavu
fc-cache -f
```

On Ubuntu or Debian, use `sudo apt install fonts-dejavu-core` followed by
`fc-cache -f`. Close and reopen KPlayer after installing fonts. No rebuild is
needed.

## Hot reload

Qt 6.11.2 provides [QML hot reload](https://www.qt.io/blog/hot-reload-in-qt-6.11.2)
through `qmlpreview`. Start a development session with:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build && cmake --build build --target KPlayer_qmlpreview
```

Save changes to files in `qml/` to update the running window. Ordinary property and
binding edits preserve state; structural edits can recreate components or require
a restart. C++ changes still require stopping the preview and rerunning the command.
The normal run command above does not watch files.

Qt generates `KPlayer_qmlpreview` automatically from the QML module and maps its
embedded resources back to source files. The Qt installation must include the
`qmlpreview` tool. QML debugging is enabled only for Debug builds; use Release
builds for distribution. In Qt Creator, the equivalent action is **Build > QML Preview**.

## Structure

```text
KPlayer/
├── CMakeLists.txt          # Build configuration and QML module
├── README.md
├── src/
│   ├── main.cpp            # Application startup and QML loading
│   └── playback/
│       ├── MpvVideo.h      # QML video item interface
│       ├── MpvVideo.cpp    # libmpv playback and OpenGL rendering
│       ├── MpvPreview.h    # Timeline preview interface
│       ├── MpvPreview.cpp  # Independent silent frame decoder
│       ├── PlaylistModel.h # Playlist interface
│       ├── PlaylistModel.cpp # Ordered file queue and current track
│       ├── Favorites.h     # Saved favorite media interface
│       └── Favorites.cpp   # Persistent favorites and duplicate handling
├── qml/
│   ├── Main.qml            # Window composition
│   ├── Theme.qml           # Singleton colors, typography, and sizing
│   ├── PlayerIcon.qml      # Vector icon library
│   ├── IconButton.qml      # Reusable visual button
│   ├── PlayerSlider.qml    # Seek and volume slider with tooltip
│   ├── AppMenu.qml         # Traditional Chinese application/context menu
│   ├── VideoAdjustments.qml # Live brightness, contrast, and saturation controls
│   ├── PlayerMenu.qml      # Shared compact menu styling
│   ├── PlayerMenuItem.qml  # Labels, shortcuts, checks, and submenu arrows
│   ├── PlayerMenuSeparator.qml # Shared menu dividers
│   ├── PlaylistTab.qml     # Reusable visual tab
│   ├── TitleBar.qml        # App menu and window control icons
│   ├── VideoStage.qml      # Video rendering, watermark, and mouse actions
│   ├── WindowResizeFrame.qml # Window edge and corner resize handles
│   ├── PlaybackBar.qml     # Tracks, transport, time, and tools
│   └── PlaylistPanel.qml   # Playlist headers, tabs, and footer
└── assets/
    └── icons/              # Embedded SVG icons
```

C++ implementation files live in `src/`, with headers beside their sources.
The interface lives in `qml/`, and external media lives in `assets/`.
Build output stays in the ignored `build/` directory. The embedded QML module
remains `KPlayer`, with `Main` as its entry point.

The video item uses a [Qt Quick framebuffer object](https://doc.qt.io/qt-6/qquickframebufferobject.html)
to integrate [libmpv rendering](https://github.com/mpv-player/mpv/blob/master/libmpv/render_gl.h)
with the Qt scene graph.

The project follows Qt's [CMake setup for QML applications](https://doc.qt.io/qt-6/cmake-build-qml-application.html).
To check QML with Qt's linter, run `cmake --build build --target all_qmllint`.

## Tests

The optional tests require Qt Test and generate their own temporary WAV and Y4M files.
They exercise favorite persistence, video adjustments, aspect-ratio controls,
same-folder video discovery, idle startup without discovery, duplicate suppression,
Explorer-style startup playback, isolated Windows association registration,
multi-file drops, playlist validation, filtering, queue editing,
active-track preservation, title/video context-menu opening and dismissal,
submenus, menu bounds at minimum window size, keyboard shortcuts, repeat modes, playlist visibility, resize
handle states, double-click playback, timeline hover, mute, volume, seeking,
left/right-click transport actions, and automatic advancement at end-of-file:

```sh
cmake -S . -B build -DKPLAYER_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The playback tests require an OpenGL-capable display (WSLg works). On a headless
Linux system, run CTest through an X server with OpenGL support, such as Xvfb,
and a window manager for the fullscreen and focus tests. The tests select the
Basic Qt Quick Controls style to avoid depending on the desktop theme.
If WSL's graphics driver stalls, run the tests with software OpenGL and a single
render thread:

```sh
QT_QPA_PLATFORM=wayland LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe QSG_RENDER_LOOP=basic ctest --test-dir build --output-on-failure
```

If llvmpipe produces corrupted frames, use `GALLIUM_DRIVER=softpipe` instead.
Preview diagnostics are opt-in with `QT_LOGGING_RULES="kplayer.preview.debug=true"`.
