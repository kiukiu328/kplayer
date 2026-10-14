#!/usr/bin/env python3
"""Run Qt GUI tests privately and mirror them without taking desktop focus."""

import argparse
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import time


def stop(process, group=False):
    if process is None:
        return
    try:
        if group:
            os.killpg(process.pid, signal.SIGTERM)
        else:
            process.terminate()
        process.wait(timeout=3)
    except ProcessLookupError:
        pass
    except subprocess.TimeoutExpired:
        if group:
            os.killpg(process.pid, signal.SIGKILL)
        else:
            process.kill()
        process.wait()


def wait_ready(path, process):
    deadline = time.monotonic() + 15
    while not path.exists():
        if process.poll() is not None:
            raise RuntimeError("test display exited before it was ready")
        if time.monotonic() >= deadline:
            raise RuntimeError("timed out waiting for the test display")
        time.sleep(0.1)


def run_display(args, command, renderer):
    env = os.environ.copy()
    env.pop("DISPLAY", None)
    env.pop("WAYLAND_DISPLAY", None)
    env.pop("QT_WAYLAND_SHELL_INTEGRATION", None)
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["GALLIUM_DRIVER"] = renderer
    compositor = None
    tests = None
    log_path = Path(env["XDG_RUNTIME_DIR"]).parent / "kwin.log"
    try:
        with log_path.open("w") as log:
            compositor = subprocess.Popen(
                [args.kwin, "--virtual", "--socket", "kplayer-tests",
                 "--width", "1920", "--height", "1080", "--no-lockscreen",
                 "--no-global-shortcuts", "--no-kactivities"],
                env=env, stdout=log, stderr=log)
            wait_ready(Path(env["XDG_RUNTIME_DIR"]) / "kplayer-tests", compositor)
            env["QT_QPA_PLATFORM"] = "wayland"
            env["WAYLAND_DISPLAY"] = "kplayer-tests"
            tests = subprocess.Popen(command, env=env)
            while tests.poll() is None:
                if compositor.poll() is not None:
                    raise RuntimeError("private compositor stopped during the tests")
                time.sleep(0.1)
            result = tests.returncode
            if result:
                print(log_path.read_text()[-4000:], file=sys.stderr)
            return result if result >= 0 else 128 - result
    except RuntimeError:
        print(log_path.read_text()[-4000:], file=sys.stderr)
        raise
    finally:
        stop(tests)
        stop(compositor)


def isolated(args):
    renderer = os.environ.get("KPLAYER_TEST_RENDERER", "llvmpipe")
    if not args.image_test:
        return run_display(args, args.command, renderer)

    # Enumerate Qt's actual test methods so newly added tests are automatically
    # included. Metadata discovery creates no windows and needs no compositor.
    env = os.environ.copy()
    env["QT_QPA_PLATFORM"] = "offscreen"
    discovery = subprocess.run([args.command[0], "-functions"], env=env,
                               capture_output=True, text=True, check=True)
    methods = [line[:-2] for line in discovery.stdout.splitlines()
               if re.fullmatch(r"\w+\(\)", line)]
    if not methods or any(name not in methods for name in args.image_test):
        raise RuntimeError("could not enumerate the configured image tests")
    regular = [name for name in methods if name not in args.image_test]
    print(f"Running {len(regular)} test methods with {renderer}", flush=True)
    result = run_display(args, [*args.command, *regular], renderer)
    image_command = []
    for argument in args.command:
        if argument.endswith(",junitxml"):
            path = Path(argument.removesuffix(",junitxml"))
            argument = f"{path.with_stem(path.stem + '-rendering')},junitxml"
        image_command.append(argument)
    image_renderer = os.environ.get("KPLAYER_TEST_IMAGE_RENDERER", "softpipe")
    print(f"Running {len(args.image_test)} image test methods with {image_renderer}", flush=True)
    image_result = run_display(args, [*image_command, *args.image_test], image_renderer)
    return result or image_result


def run(args):
    if not os.environ.get("WAYLAND_DISPLAY"):
        raise RuntimeError("a Wayland desktop with a second monitor is required")
    with tempfile.TemporaryDirectory(prefix="kplayer-test-display-") as directory:
        root = Path(directory)
        frame = root / "frame.png"
        ready = root / "monitor-ready"
        monitor = None
        session = None
        try:
            env = os.environ.copy()
            env["QT_QPA_PLATFORM"] = "wayland"
            if args.screen:
                env["KPLAYER_TEST_SCREEN"] = args.screen
            monitor = subprocess.Popen([args.monitor, str(frame), str(ready)],
                                       env=env, start_new_session=True)
            # Fail before launching any test windows if the passive preview cannot
            # be placed on a second monitor using the compositor's layer protocol.
            wait_ready(ready, monitor)
            print(f"Showing isolated tests on {ready.read_text()}", flush=True)
            env.pop("DISPLAY", None)
            env.pop("WAYLAND_DISPLAY", None)
            env.pop("DBUS_SESSION_BUS_ADDRESS", None)
            env.pop("QT_WAYLAND_SHELL_INTEGRATION", None)
            # Keep audio playback connected while isolating the display socket.
            # PipeWire and PulseAudio otherwise look in the private runtime dir.
            desktop_runtime = os.environ.get("XDG_RUNTIME_DIR", "")
            if desktop_runtime:
                env.setdefault("PIPEWIRE_RUNTIME_DIR", desktop_runtime)
                pulse_socket = Path(desktop_runtime) / "pulse" / "native"
                if pulse_socket.exists():
                    env.setdefault("PULSE_SERVER", f"unix:{pulse_socket}")
            for variable, subdirectory in (("XDG_RUNTIME_DIR", "runtime"),
                                          ("XDG_CONFIG_HOME", "config"),
                                          ("XDG_CACHE_HOME", "cache"),
                                          ("XDG_DATA_HOME", "data")):
                path = root / subdirectory
                path.mkdir(mode=0o700)
                env[variable] = str(path)
            env["KPLAYER_TEST_MIRROR"] = str(frame)
            env["LIBGL_ALWAYS_SOFTWARE"] = "1"
            env["QSG_RENDER_LOOP"] = "basic"
            # Use the session bus's normal permissions without auto-starting
            # desktop portals, wallets, or settings daemons in the private run.
            bus_config = root / "dbus.conf"
            bus_config.write_text('''<busconfig>
  <type>session</type><listen>unix:tmpdir=/tmp</listen><auth>EXTERNAL</auth>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/><allow own="*"/>
  </policy>
</busconfig>
''')
            session = subprocess.Popen(
                [args.dbus, f"--config-file={bus_config}", "--", sys.executable,
                 str(Path(__file__).resolve()),
                 "--isolated", "--kwin", args.kwin,
                 *(f"--image-test={name}" for name in args.image_test),
                 "--", *args.command],
                env=env, start_new_session=True)
            while session.poll() is None:
                if monitor.poll() is not None:
                    raise RuntimeError("second-monitor preview stopped during the tests")
                time.sleep(0.1)
            return session.returncode
        finally:
            stop(session, group=True)
            stop(monitor, group=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--monitor")
    parser.add_argument("--screen", default="")
    parser.add_argument("--image-test", action="append", default=[],
                        help="run this test separately using the image-check renderer")
    parser.add_argument("--kwin", required=True)
    parser.add_argument("--dbus", default="dbus-run-session")
    parser.add_argument("--isolated", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.command and args.command[0] == "--":
        args.command.pop(0)
    if not args.command or (not args.isolated and not args.monitor):
        parser.error("a monitor executable and a test command are required")
    # Let CTest timeouts and Ctrl+C clean up both the preview and private session.
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, lambda signum, _: sys.exit(128 + signum))
    try:
        return isolated(args) if args.isolated else run(args)
    except RuntimeError as error:
        print(f"GUI test runner: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
