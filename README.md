# KPlayer

A minimal Qt Quick application that opens an empty 960 × 540 window.
Closing the window exits the application. Playback is not implemented yet.

## Requirements

- Qt 6.12 or newer with Qt Quick and its development tools
- CMake 3.24 or newer
- A C++20 compiler
- Ninja

## Build and run

From the project directory (Linux):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build && ./build/KPlayer
```

If Qt is installed outside the system search paths, add
`-DCMAKE_PREFIX_PATH=/path/to/Qt/6.12.0/gcc_64` to the configure command,
using your actual Qt installation prefix.

You can also open `CMakeLists.txt` in Qt Creator and select a Qt 6.12+ kit.
On Windows the executable is `build/KPlayer.exe`; on macOS it is
`build/KPlayer.app/Contents/MacOS/KPlayer`.

## Hot reload

Qt 6.12 provides [QML hot reload](https://www.qt.io/blog/hot-reload-in-qt-6.12)
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

- `CMakeLists.txt`: Qt executable and embedded QML module.
- `main.cpp`: application startup and QML loading via `loadFromModule`.
- `Main.qml`: the empty window.

The project follows Qt's [CMake setup for QML applications](https://doc.qt.io/qt-6/cmake-build-qml-application.html).
To check QML with Qt's linter, run `cmake --build build --target all_qmllint`.
