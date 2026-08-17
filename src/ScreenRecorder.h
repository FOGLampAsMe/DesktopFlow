#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

struct IMFSinkWriter;

namespace desktopflow {

struct CaptureRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct FrameView {
    HDC dc = nullptr;
    std::uint8_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;
    CaptureRect screenRect;
    // True only when this capture call observed a new desktop image. DXGI can
    // legitimately return the last image when the requested output FPS is
    // above the monitor refresh rate.
    bool sourceFrame = false;
};

class FrameCaptureSource {
public:
    virtual ~FrameCaptureSource() = default;
    virtual bool capture(FrameView& frame, std::wstring& error) = 0;
    virtual CaptureRect bounds() const = 0;
    virtual const wchar_t* name() const = 0;
};

class GdiCaptureSource : public FrameCaptureSource {
public:
    explicit GdiCaptureSource(CaptureRect rect);
    ~GdiCaptureSource() override;
    bool capture(FrameView& frame, std::wstring& error) override;
    CaptureRect bounds() const override;

protected:
    bool initialize(std::wstring& error);
    CaptureRect rect_;

private:
    HDC screenDc_ = nullptr;
    HDC memoryDc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previousBitmap_ = nullptr;
    std::uint8_t* pixels_ = nullptr;
    int outputWidth_ = 0;
    int outputHeight_ = 0;
};

class DesktopCaptureSource final : public GdiCaptureSource {
public:
    DesktopCaptureSource();
    const wchar_t* name() const override;
};

class DxgiDesktopCaptureSource final : public FrameCaptureSource {
public:
    DxgiDesktopCaptureSource();
    ~DxgiDesktopCaptureSource() override;
    bool capture(FrameView& frame, std::wstring& error) override;
    CaptureRect bounds() const override;
    const wchar_t* name() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class RegionCaptureSource final : public GdiCaptureSource {
public:
    explicit RegionCaptureSource(CaptureRect rect);
    const wchar_t* name() const override;
};

class FrameOverlayRenderer {
public:
    virtual ~FrameOverlayRenderer() = default;
    virtual void apply(FrameView& frame, std::int64_t elapsedMilliseconds) = 0;
    virtual const wchar_t* name() const = 0;
};

class CleanFrameOverlay final : public FrameOverlayRenderer {
public:
    void apply(FrameView& frame, std::int64_t elapsedMilliseconds) override;
    const wchar_t* name() const override;
};

class PointerFrameOverlay final : public FrameOverlayRenderer {
public:
    PointerFrameOverlay();
    ~PointerFrameOverlay() override;
    void apply(FrameView& frame, std::int64_t elapsedMilliseconds) override;
    const wchar_t* name() const override;

private:
    HPEN normalPen_ = nullptr;
    HPEN pressedPen_ = nullptr;
    HBRUSH badgeBrush_ = nullptr;
    HFONT timerFont_ = nullptr;
};

class VideoEncoder {
public:
    virtual ~VideoEncoder() = default;
    virtual bool open(const std::wstring& path, int width, int height, int fps,
                      std::wstring& error) = 0;
    virtual bool writeFrame(const FrameView& frame, std::int64_t time100ns,
                            std::int64_t duration100ns, std::wstring& error) = 0;
    virtual bool finalize(std::wstring& error) = 0;
    virtual const wchar_t* name() const = 0;
    virtual bool canRetryWithLowerFrameRate() const { return false; }
};

class MediaFoundationH264Encoder final : public VideoEncoder {
public:
    MediaFoundationH264Encoder() = default;
    ~MediaFoundationH264Encoder() override;
    bool open(const std::wstring& path, int width, int height, int fps,
              std::wstring& error) override;
    bool writeFrame(const FrameView& frame, std::int64_t time100ns,
                    std::int64_t duration100ns, std::wstring& error) override;
    bool finalize(std::wstring& error) override;
    const wchar_t* name() const override;
    bool canRetryWithLowerFrameRate() const override;

private:
    void shutdown();
    IMFSinkWriter* writer_ = nullptr;
    unsigned long streamIndex_ = 0;
    bool comInitialized_ = false;
    bool mediaFoundationStarted_ = false;
    bool writing_ = false;
    bool mediaTypeRejected_ = false;
};

struct RecorderStats {
    bool recording = false;
    bool finalizing = false;
    int framesWritten = 0;
    int sourceFrames = 0;
    int droppedFrames = 0;
    int elapsedMilliseconds = 0;
    double actualFps = 0.0;
    double sourceFps = 0.0;
    std::wstring status;
    std::wstring outputPath;
};

class ScreenRecorder {
public:
    ScreenRecorder() = default;
    ~ScreenRecorder();
    ScreenRecorder(const ScreenRecorder&) = delete;
    ScreenRecorder& operator=(const ScreenRecorder&) = delete;

    bool start(std::unique_ptr<FrameCaptureSource> source,
               std::unique_ptr<FrameOverlayRenderer> overlay,
               std::unique_ptr<VideoEncoder> encoder,
               const std::wstring& outputPath, int fps);
    void requestStop();
    void stop();
    RecorderStats stats() const;
    bool copyPreview(std::vector<std::uint8_t>& bgra, int& width, int& height) const;

private:
    void recordLoop(std::unique_ptr<FrameCaptureSource> source,
                    std::unique_ptr<FrameOverlayRenderer> overlay,
                    std::unique_ptr<VideoEncoder> encoder,
                    std::wstring outputPath, int fps);
    void setStatus(const std::wstring& status);

    std::thread worker_;
    std::atomic_bool recording_ {false};
    std::atomic_bool finalizing_ {false};
    std::atomic_bool stopRequested_ {false};
    std::atomic_int framesWritten_ {0};
    std::atomic_int sourceFrames_ {0};
    std::atomic_int droppedFrames_ {0};
    std::atomic_int elapsedMilliseconds_ {0};
    mutable std::mutex stateMutex_;
    std::wstring status_ = L"录像未开始";
    std::wstring outputPath_;
    mutable std::mutex previewMutex_;
    std::vector<std::uint8_t> previewBgra_;
    int previewWidth_ = 0;
    int previewHeight_ = 0;
};

CaptureRect virtualDesktopRect();
CaptureRect recordingFrameRect(CaptureRect screenRect);

} // namespace desktopflow
