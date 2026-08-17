# DesktopFlow

DesktopFlow 是一款面向 Windows 10 和 Windows 11 的轻量级本地录屏软件。界面基于
Qt Widgets，桌面捕获使用 DXGI Desktop Duplication，视频通过 Windows Media
Foundation 编码为 H.264 MP4 文件。

## 1.0 版本功能

- 支持录制整个桌面或自定义屏幕区域。
- 自动将 H.264 MP4 文件保存到 `视频/DesktopFlow`。
- 提供 15、24、30、60、120、165 和 240 FPS 选项；编码器不支持所选帧率时会自动
  回退到可用帧率。
- 始终将 DesktopFlow 控制窗口排除在录制内容之外，避免预览画面无限套娃。
- 提供宽度为 1280 像素、约 8 FPS 的清晰实时预览。
- 可选择在录制画面中显示鼠标标记。
- 实时显示录制时长、输出帧率、总帧数和丢帧数量。

录制文件无法包含超过显示器刷新率的不同画面。例如，在 165 Hz 显示器上选择
240 FPS 时，超出的部分只能使用重复帧。通常建议选择不高于显示器刷新率的帧率。

当前 1.0 版本暂不包含系统声音、麦克风、暂停与继续、指定窗口录制、异常恢复和
录后剪辑功能。

## 使用方法

1. 在 Releases 页面下载 `DesktopFlowQt-win64.zip`。
2. 解压完整压缩包，不要单独移动其中的 `DesktopFlow.exe`。
3. 运行 `release/DesktopFlow.exe`。
4. 选择录制范围、帧率和鼠标标记，然后点击“开始录制”。

程序默认使用唯一文件名保存录像，因此不会覆盖之前的录制文件。

## 项目结构

- `src/App.*`：Qt 主窗口、界面布局、实时预览和交互状态。
- `src/ScreenRecorder.*`：桌面捕获、画面标记和 H.264 编码。
- `src/RegionSelector.*`：基于 Win32 的全屏区域选择器。

录制核心不依赖 Qt，捕获和编码逻辑与界面层相互独立，便于后续扩展音频录制、
异常恢复和其他输出格式。

## Windows 构建

构建项目需要 CMake 3.21 或更高版本、支持 C++17 的编译器、Qt 6，以及包含 Media
Foundation 组件的 Windows SDK。项目也保留了对 Qt 5.15 的兼容支持。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="C:\\Qt\\6.x.x\\msvc2022_64"
cmake --build build --config Release
```

CMake 项目会链接 Qt Widgets、DXGI、Direct3D 11、GDI 和 Media Foundation。
对外分发前，需要使用 `windeployqt` 为 `DesktopFlow.exe` 部署 Qt 运行库。
