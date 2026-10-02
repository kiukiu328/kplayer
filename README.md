# KPlayer

A Qt Quick media-player UI based on the supplied KPlayer reference. The
1225 × 824 frameless window includes the title bar, empty video surface,
transport controls, seek and volume tracks, timestamps, media tools, playlist
tabs, ordering buttons, and search surface. Window controls and play/pause/stop
are connected; search, playlist tabs, track navigation, and the remaining tools
are still visual previews.

## UI organization

All colors are declared as named QML properties in `qml/Theme.qml`, along with
shared typography and sizing. Change these properties to restyle the interface.
`PlayerIcon.qml` contains scalable vector icons; `IconButton.qml` and
`PlaylistTab.qml` are reusable controls. Each major region has its own component:
`TitleBar.qml`, `VideoStage.qml`, `PlaybackBar.qml`, and `PlaylistPanel.qml`.
`Main.qml` assembles the window and connects component signals to window and
playback actions. Buttons have hover styling, accessible labels, and mouse and
keyboard activation.

The libmpv backend in `src/playback/` renders media in the play area. The player
starts idle. If `assets/dummy.mp4` exists in the working directory, Play starts
that sample; otherwise Play opens a file picker. Open Media can select another
file, and a file path or URL can also be supplied as the first command-line
argument.

- Top-right Close, Minimize, Maximize/Restore, and Full Screen/Exit Full Screen
  buttons control the window. Exiting fullscreen restores its prior normal or
  maximized state.
- Hold the left mouse button and drag the play area to move the window.
- Play toggles play/pause. Stop clears playback; pressing Play again restarts
  the selected media from the beginning.
- A middle click in the play area toggles fullscreen. A middle double-click
  toggles play/pause. The single-click action waits for the system double-click
  interval so the two actions do not overlap.

## Requirements

- Qt 6.11.2 or newer with Qt Quick, Qt OpenGL, and its development tools
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
│       └── MpvVideo.cpp    # libmpv playback and OpenGL rendering
├── qml/
│   ├── Main.qml            # Window composition
│   ├── Theme.qml           # Singleton colors, typography, and sizing
│   ├── PlayerIcon.qml      # Vector icon library
│   ├── IconButton.qml      # Reusable visual button
│   ├── PlaylistTab.qml     # Reusable visual tab
│   ├── TitleBar.qml        # App menu and window control icons
│   ├── VideoStage.qml      # Video rendering, watermark, and mouse actions
│   ├── PlaybackBar.qml     # Tracks, transport, time, and tools
│   └── PlaylistPanel.qml   # Playlist headers, tabs, and footer
└── assets/
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
