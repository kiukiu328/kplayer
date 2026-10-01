# KPlayer

A minimal Qt Quick application that plays `dummy.mp4` inside a 960 × 540
window using libmpv. The video starts automatically and loops until the window
is closed. Resizing the window preserves the video's aspect ratio.

## Requirements

- Qt 6.11.2 or newer with Qt Quick, Qt OpenGL, and its development tools
- libmpv development headers and library, discoverable as `mpv` through pkg-config
- pkg-config
- FFmpeg with the `libx264` encoder (to generate the sample video)
- CMake 3.24 or newer
- A C++20 compiler
- Ninja

## Build and run

Generate the silent 10-second, 960 × 540, 30 fps H.264 test video from the
project directory:

```sh
ffmpeg -hide_banner -y -f lavfi -i testsrc2=size=960x540:rate=30 -t 10 -c:v libx264 -pix_fmt yuv420p -movflags +faststart dummy.mp4
```

This command overwrites `dummy.mp4`. The generated video is ignored by Git;
regenerate it after cloning the project. If it is missing, the window displays
a playback error.

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

The build records the source directory's absolute path to `dummy.mp4`, so
launching from Qt Creator or another working directory also works. Reconfigure
if you move the project. The video remains an external file, not an embedded resource.
Playback requires an OpenGL-capable Qt platform; the application explicitly
selects Qt Quick's OpenGL backend for libmpv's render API.

## Hot reload

Qt 6.11.2 provides [QML hot reload](https://www.qt.io/blog/hot-reload-in-qt-6.11.2)
through `qmlpreview`. Start a development session with:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build && cmake --build build --target KPlayer_qmlpreview
```

Save changes to `Main.qml` to update the running window. Ordinary property and
binding edits preserve state; structural edits can recreate components or require
a restart. C++ changes still require stopping the preview and rerunning the command.
The normal run command above does not watch files.

Qt generates `KPlayer_qmlpreview` automatically from the QML module and maps its
embedded resources back to source files. The Qt installation must include the
`qmlpreview` tool. QML debugging is enabled only for Debug builds; use Release
builds for distribution. In Qt Creator, the equivalent action is **Build > QML Preview**.

## Structure

- `CMakeLists.txt`: Qt executable, libmpv linkage, and embedded QML module.
- `main.cpp`: OpenGL setup, sample video location, and QML loading via `loadFromModule`.
- `Main.qml`: the video window and playback error message.
- `MpvVideo.h` / `MpvVideo.cpp`: QML video item using libmpv's OpenGL render API.
- `dummy.mp4`: generated test video (ignored by Git).

The video item uses a [Qt Quick framebuffer object](https://doc.qt.io/qt-6/qquickframebufferobject.html)
to integrate [libmpv rendering](https://github.com/mpv-player/mpv/blob/master/libmpv/render_gl.h)
with the Qt scene graph.

The project follows Qt's [CMake setup for QML applications](https://doc.qt.io/qt-6/cmake-build-qml-application.html).
To check QML with Qt's linter, run `cmake --build build --target all_qmllint`.
