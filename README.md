# DesktopFlow

DesktopFlow is a lightweight local screen recorder for Windows 10 and 11. It
uses Qt Widgets for the interface, DXGI Desktop Duplication for full-desktop
capture, and Media Foundation for H.264 MP4 encoding.

## Version 1.0

- Record the entire desktop or a custom region.
- Export standard H.264 MP4 files to `Videos/DesktopFlow`.
- Select 15, 24, 30, 60, 120, 165, or 240 FPS. Unsupported frame rates fall
  back automatically to a rate accepted by the encoder.
- Keep the DesktopFlow control window out of captured video to prevent
  recursive previews.
- Show a clear 1280-pixel-wide live preview at about 8 FPS.
- Optionally add a visible mouse marker.
- Display elapsed time, encoded FPS, frame count, and dropped frames.

The recorder cannot capture more distinct screen updates than the monitor
produces. For example, 240 FPS on a 165 Hz display necessarily contains
repeated frames. Audio capture, pause/resume, single-window capture, crash
recovery, and editing are not included in version 1.0.

## Architecture

- `src/App.*`: Qt main window, layout, preview, and UI state.
- `src/ScreenRecorder.*`: capture sources, overlays, and H.264 encoding.
- `src/RegionSelector.*`: Win32 full-screen region selection overlay.

The recorder core does not depend on Qt, keeping capture and encoding work
separate from the interface.

## Build on Windows

Install CMake 3.21 or newer, a C++17 compiler, Qt 6, and the Windows SDK with
Media Foundation components. Qt 5.15 remains supported as a compatibility
fallback.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="C:\\Qt\\6.x.x\\msvc2022_64"
cmake --build build --config Release
```

The CMake project links Qt Widgets, DXGI, Direct3D 11, GDI, and Media
Foundation. Run `windeployqt` on `DesktopFlow.exe` before distributing it.
