# KPlayer

A Qt Quick media-player UI based on the supplied KPlayer reference. The
1225 × 824 frameless window includes the title bar, empty video surface,
transport controls, seek and volume tracks, timestamps, media tools, playlist
tabs, ordering buttons, and search surface. Window controls, playback, playlist
file drops, seeking, mute, volume, and track navigation are connected. Playlist
search, queue editing, ordering, application menus, repeat modes, and always-on-top
window pinning are functional. Named playlists keep separate queues during the session.

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

The libmpv backend in `src/playback/` renders media in the play area. The player
starts idle. If `assets/dummy.mp4` exists in the working directory, Play starts
that sample; otherwise Play opens a file picker. Open Media can select another
file, and a file path or URL can also be supplied as the first command-line
argument.

- Top-right Close, Minimize, Maximize/Restore, and Full Screen/Exit Full Screen
  buttons control the window. Exiting fullscreen restores its prior normal or
  maximized state.
- Hold the left mouse button and drag the play area to move the window.
- Double-click the play area with the left mouse button to play or pause.
- Drag any window edge or corner to resize. Resizing is available in the normal
  window state and respects the minimum window size.
- Click the Playlist button to hide or show the playlist. Hiding it expands the
  play area and keeps the queue and current selection.
- Drag one or more files onto the playlist to append them in drop order.
  Double-click a playlist entry, or select it and press Enter, to play it.
  Dropping files queues them without interrupting playback; directories and
  missing files are ignored. Open Media supports selecting multiple files and
  starts the first new entry.
- Add (新增), the Browse tab, and **Add to playlist…** queue files without
  interrupting playback. Remove (移除) or Delete removes the selected entry;
  removing the current entry selects its next neighbor (or the previous final
  entry). Playback stops if the media changes. Clear Playlist empties the queue
  and stops playback.
- The ordering buttons move the selected entry to the top, up, down, or bottom.
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
  unavailable features are disabled. Press Escape or click outside to dismiss it.
  Open (開啟) contains file queueing, search, and Clear Playlist actions.
  Settings offers repeat off, repeat current track, or repeat the playlist;
  manual Previous/Next track navigation still stops at the queue boundaries.
  The pin button toggles Always on Top and highlights while enabled.
- Keyboard shortcuts: F3 or Ctrl+O opens media, F4 closes playback while keeping
  the queue, F6 or Ctrl+L toggles the playlist, F11 toggles fullscreen, and Escape
  leaves fullscreen. With focus in the video area, Enter also toggles fullscreen, Space
  toggles playback, M toggles mute, Left/Right seek 10 seconds, and Up/Down adjust
  volume by 5%. Text fields, focused controls, and playlist selection keep their
  own keyboard behavior.
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
- A middle click in the play area toggles fullscreen. A middle double-click
  toggles play/pause. The single-click action waits for the system double-click
  interval so the two actions do not overlap.

## Requirements

- Qt 6.11.2 or newer with Qt Quick, Qt OpenGL, Qt SVG, and its development tools
- libmpv development headers and library, discoverable as `mpv` through pkg-config
- pkg-config
- FFmpeg with the `libx264` encoder (optional, to generate a future playback sample)
- CMake 3.24 or newer
- A C++20 compiler
- Ninja
- A system font available to fontconfig on Linux (for example, DejaVu Sans)

## Build and run

No media file is needed to open the player. To optionally prepare a silent
10-second H.264 test video, run:

```sh
ffmpeg -hide_banner -y -f lavfi -i testsrc2=size=960x540:rate=30 -t 10 -c:v libx264 -pix_fmt yuv420p -movflags +faststart assets/dummy.mp4
```

This command overwrites `assets/dummy.mp4`. The generated video is ignored by Git;
regenerate it when needed after cloning the project.

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
│       └── PlaylistModel.cpp # Ordered file queue and current track
├── qml/
│   ├── Main.qml            # Window composition
│   ├── Theme.qml           # Singleton colors, typography, and sizing
│   ├── PlayerIcon.qml      # Vector icon library
│   ├── IconButton.qml      # Reusable visual button
│   ├── PlayerSlider.qml    # Seek and volume slider with tooltip
│   ├── AppMenu.qml         # Traditional Chinese application/context menu
│   ├── StyledMenu.qml      # Shared compact menu styling
│   ├── StyledMenuItem.qml  # Labels, shortcuts, checks, and submenu arrows
│   ├── StyledMenuSeparator.qml # Shared menu dividers
│   ├── PlaylistTab.qml     # Reusable visual tab
│   ├── TitleBar.qml        # App menu and window control icons
│   ├── VideoStage.qml      # Video rendering, watermark, and mouse actions
│   ├── WindowResizeFrame.qml # Window edge and corner resize handles
│   ├── PlaybackBar.qml     # Tracks, transport, time, and tools
│   └── PlaylistPanel.qml   # Playlist headers, tabs, and footer
└── assets/
    ├── icons/              # Embedded SVG icons
    └── dummy.mp4           # Generated sample video (ignored by Git)
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
They exercise multi-file drops, playlist validation, filtering, queue editing,
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
Linux system, run CTest through an X server with OpenGL support, such as Xvfb.
If WSL's graphics driver stalls, run the tests with software OpenGL and a single
render thread:

```sh
QT_QPA_PLATFORM=wayland LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe QSG_RENDER_LOOP=basic ctest --test-dir build --output-on-failure
```
