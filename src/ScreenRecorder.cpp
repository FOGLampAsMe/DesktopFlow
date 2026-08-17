#include "ScreenRecorder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <iomanip>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <sstream>

namespace desktopflow {
namespace {

template <typename T>
void safeRelease(T*& pointer) {
    if (pointer != nullptr) {
        pointer->Release();
        pointer = nullptr;
    }
}

std::wstring hresultText(const wchar_t* operation, HRESULT result) {
    std::wostringstream stream;
    stream << operation << L"失败（HRESULT 0x" << std::hex << std::uppercase
           << static_cast<unsigned long>(result) << L"）";
    return stream.str();
}

CaptureRect normalizedEvenRect(CaptureRect rect) {
    rect.width = std::max(2, rect.width - rect.width % 2);
    rect.height = std::max(2, rect.height - rect.height % 2);
    return rect;
}

void deleteEmptyOutput(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data {};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) &&
        data.nFileSizeHigh == 0 && data.nFileSizeLow == 0) {
        DeleteFileW(path.c_str());
    }
}

} // namespace

CaptureRect virtualDesktopRect() {
    return normalizedEvenRect({
        GetSystemMetrics(SM_XVIRTUALSCREEN),
        GetSystemMetrics(SM_YVIRTUALSCREEN),
        GetSystemMetrics(SM_CXVIRTUALSCREEN),
        GetSystemMetrics(SM_CYVIRTUALSCREEN)
    });
}

CaptureRect recordingFrameRect(CaptureRect screenRect) {
    return normalizedEvenRect(screenRect);
}

GdiCaptureSource::GdiCaptureSource(CaptureRect rect) : rect_(normalizedEvenRect(rect)) {
    const CaptureRect output = recordingFrameRect(rect_);
    outputWidth_ = output.width;
    outputHeight_ = output.height;
}

GdiCaptureSource::~GdiCaptureSource() {
    if (memoryDc_ != nullptr && previousBitmap_ != nullptr) SelectObject(memoryDc_, previousBitmap_);
    if (bitmap_ != nullptr) DeleteObject(bitmap_);
    if (memoryDc_ != nullptr) DeleteDC(memoryDc_);
    if (screenDc_ != nullptr) ReleaseDC(nullptr, screenDc_);
}

bool GdiCaptureSource::initialize(std::wstring& error) {
    if (memoryDc_ != nullptr) return true;
    if (rect_.width <= 0 || rect_.height <= 0) {
        error = L"录制区域尺寸无效";
        return false;
    }
    screenDc_ = GetDC(nullptr);
    if (screenDc_ == nullptr) {
        error = L"无法获取桌面绘图上下文";
        return false;
    }
    memoryDc_ = CreateCompatibleDC(screenDc_);
    BITMAPINFO bitmapInfo {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = outputWidth_;
    bitmapInfo.bmiHeader.biHeight = -outputHeight_;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    bitmap_ = CreateDIBSection(screenDc_, &bitmapInfo, DIB_RGB_COLORS,
                               reinterpret_cast<void**>(&pixels_), nullptr, 0);
    if (screenDc_ == nullptr || memoryDc_ == nullptr || bitmap_ == nullptr || pixels_ == nullptr) {
        error = L"无法创建屏幕采集缓冲区";
        return false;
    }
    previousBitmap_ = SelectObject(memoryDc_, bitmap_);
    return true;
}

bool GdiCaptureSource::capture(FrameView& frame, std::wstring& error) {
    if (!initialize(error)) return false;
    BOOL captured = FALSE;
    if (outputWidth_ == rect_.width && outputHeight_ == rect_.height) {
        captured = BitBlt(memoryDc_, 0, 0, outputWidth_, outputHeight_,
                          screenDc_, rect_.x, rect_.y, SRCCOPY);
    } else {
        SetStretchBltMode(memoryDc_, COLORONCOLOR);
        captured = StretchBlt(memoryDc_, 0, 0, outputWidth_, outputHeight_,
                              screenDc_, rect_.x, rect_.y, rect_.width, rect_.height,
                              SRCCOPY);
    }
    if (!captured) {
        error = L"屏幕帧采集失败";
        return false;
    }
    frame = {memoryDc_, pixels_, outputWidth_, outputHeight_, outputWidth_ * 4, rect_, true};
    return true;
}

CaptureRect GdiCaptureSource::bounds() const {
    return {rect_.x, rect_.y, outputWidth_, outputHeight_};
}

DesktopCaptureSource::DesktopCaptureSource() : GdiCaptureSource(virtualDesktopRect()) {}
const wchar_t* DesktopCaptureSource::name() const { return L"整个桌面"; }

struct DxgiDesktopCaptureSource::Impl {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGIOutputDuplication* duplication = nullptr;
    ID3D11Texture2D* stagingTexture = nullptr;
    HDC screenDc = nullptr;
    HDC memoryDc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previousBitmap = nullptr;
    std::uint8_t* pixels = nullptr;
    CaptureRect rect;
    bool hasFrame = false;
    std::wstring initializationError;

    ~Impl() {
        if (memoryDc != nullptr && previousBitmap != nullptr) SelectObject(memoryDc, previousBitmap);
        if (bitmap != nullptr) DeleteObject(bitmap);
        if (memoryDc != nullptr) DeleteDC(memoryDc);
        if (screenDc != nullptr) ReleaseDC(nullptr, screenDc);
        safeRelease(stagingTexture);
        safeRelease(duplication);
        safeRelease(context);
        safeRelease(device);
    }

    bool initialize() {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL selectedLevel {};
        const D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };
        HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                           levels, static_cast<UINT>(sizeof(levels) / sizeof(levels[0])),
                                           D3D11_SDK_VERSION, &device, &selectedLevel, &context);
        if (result == E_INVALIDARG) {
            result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                       levels + 1, static_cast<UINT>(sizeof(levels) / sizeof(levels[0]) - 1),
                                       D3D11_SDK_VERSION, &device, &selectedLevel, &context);
        }
        if (FAILED(result)) {
            initializationError = hresultText(L"D3D11 设备创建", result);
            return false;
        }

        IDXGIDevice* dxgiDevice = nullptr;
        IDXGIAdapter* adapter = nullptr;
        IDXGIOutput* output = nullptr;
        IDXGIOutput1* output1 = nullptr;
        result = device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
        if (SUCCEEDED(result)) result = dxgiDevice->GetAdapter(&adapter);
        const HMONITOR primary = MonitorFromPoint(POINT {0, 0}, MONITOR_DEFAULTTOPRIMARY);
        if (SUCCEEDED(result)) {
            for (UINT index = 0; adapter->EnumOutputs(index, &output) != DXGI_ERROR_NOT_FOUND; ++index) {
                DXGI_OUTPUT_DESC description {};
                output->GetDesc(&description);
                if (description.AttachedToDesktop && description.Monitor == primary) break;
                safeRelease(output);
            }
            if (output == nullptr) result = DXGI_ERROR_NOT_FOUND;
        }
        DXGI_OUTPUT_DESC description {};
        if (SUCCEEDED(result)) result = output->GetDesc(&description);
        if (SUCCEEDED(result)) result = output->QueryInterface(
            __uuidof(IDXGIOutput1), reinterpret_cast<void**>(&output1));
        if (SUCCEEDED(result)) result = output1->DuplicateOutput(device, &duplication);
        safeRelease(output1);
        safeRelease(output);
        safeRelease(adapter);
        safeRelease(dxgiDevice);
        if (FAILED(result)) {
            initializationError = hresultText(L"DXGI 桌面复制", result);
            return false;
        }

        rect = normalizedEvenRect({
            description.DesktopCoordinates.left,
            description.DesktopCoordinates.top,
            description.DesktopCoordinates.right - description.DesktopCoordinates.left,
            description.DesktopCoordinates.bottom - description.DesktopCoordinates.top
        });
        D3D11_TEXTURE2D_DESC textureDescription {};
        textureDescription.Width = static_cast<UINT>(rect.width);
        textureDescription.Height = static_cast<UINT>(rect.height);
        textureDescription.MipLevels = 1;
        textureDescription.ArraySize = 1;
        textureDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        textureDescription.SampleDesc.Count = 1;
        textureDescription.Usage = D3D11_USAGE_STAGING;
        textureDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        result = device->CreateTexture2D(&textureDescription, nullptr, &stagingTexture);
        if (FAILED(result)) {
            initializationError = hresultText(L"DXGI 读回缓冲区创建", result);
            return false;
        }

        screenDc = GetDC(nullptr);
        memoryDc = screenDc != nullptr ? CreateCompatibleDC(screenDc) : nullptr;
        BITMAPINFO bitmapInfo {};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = rect.width;
        bitmapInfo.bmiHeader.biHeight = -rect.height;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;
        bitmap = screenDc != nullptr ? CreateDIBSection(
            screenDc, &bitmapInfo, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0) : nullptr;
        if (memoryDc == nullptr || bitmap == nullptr || pixels == nullptr) {
            initializationError = L"DXGI CPU 帧缓冲区创建失败";
            return false;
        }
        previousBitmap = SelectObject(memoryDc, bitmap);
        return true;
    }
};

DxgiDesktopCaptureSource::DxgiDesktopCaptureSource() : impl_(std::make_unique<Impl>()) {
    impl_->initialize();
}

DxgiDesktopCaptureSource::~DxgiDesktopCaptureSource() = default;

bool DxgiDesktopCaptureSource::capture(FrameView& frame, std::wstring& error) {
    if (impl_->duplication == nullptr || impl_->stagingTexture == nullptr) {
        error = impl_->initializationError.empty() ? L"DXGI 桌面采集初始化失败" : impl_->initializationError;
        return false;
    }
    DXGI_OUTDUPL_FRAME_INFO frameInfo {};
    IDXGIResource* resource = nullptr;
    HRESULT result = impl_->duplication->AcquireNextFrame(impl_->hasFrame ? 0 : 1000, &frameInfo, &resource);
    if (result == DXGI_ERROR_WAIT_TIMEOUT && impl_->hasFrame) {
        frame = {impl_->memoryDc, impl_->pixels, impl_->rect.width, impl_->rect.height,
                 impl_->rect.width * 4, impl_->rect, false};
        return true;
    }
    if (FAILED(result)) {
        error = result == DXGI_ERROR_ACCESS_LOST
            ? L"DXGI 桌面采集已失效，请重新开始录制"
            : hresultText(L"获取 DXGI 桌面帧", result);
        return false;
    }

    ID3D11Texture2D* desktopTexture = nullptr;
    result = resource->QueryInterface(__uuidof(ID3D11Texture2D),
                                      reinterpret_cast<void**>(&desktopTexture));
    if (SUCCEEDED(result)) impl_->context->CopyResource(impl_->stagingTexture, desktopTexture);
    safeRelease(desktopTexture);
    safeRelease(resource);
    impl_->duplication->ReleaseFrame();
    if (FAILED(result)) {
        error = hresultText(L"复制 DXGI 桌面帧", result);
        return false;
    }

    D3D11_MAPPED_SUBRESOURCE mapped {};
    result = impl_->context->Map(impl_->stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(result)) {
        error = hresultText(L"读取 DXGI 桌面帧", result);
        return false;
    }
    const std::size_t rowBytes = static_cast<std::size_t>(impl_->rect.width) * 4;
    for (int y = 0; y < impl_->rect.height; ++y) {
        std::memcpy(impl_->pixels + static_cast<std::size_t>(y) * rowBytes,
                    static_cast<const std::uint8_t*>(mapped.pData) +
                        static_cast<std::size_t>(y) * mapped.RowPitch,
                    rowBytes);
    }
    impl_->context->Unmap(impl_->stagingTexture, 0);
    impl_->hasFrame = true;
    frame = {impl_->memoryDc, impl_->pixels, impl_->rect.width, impl_->rect.height,
             impl_->rect.width * 4, impl_->rect, true};
    return true;
}

CaptureRect DxgiDesktopCaptureSource::bounds() const {
    return impl_->rect.width > 0 && impl_->rect.height > 0 ? impl_->rect : virtualDesktopRect();
}

const wchar_t* DxgiDesktopCaptureSource::name() const { return L"主显示器（DXGI 高性能）"; }

RegionCaptureSource::RegionCaptureSource(CaptureRect rect) : GdiCaptureSource(rect) {}
const wchar_t* RegionCaptureSource::name() const { return L"自定义区域"; }

void CleanFrameOverlay::apply(FrameView&, std::int64_t) {}
const wchar_t* CleanFrameOverlay::name() const { return L"纯净画面"; }

PointerFrameOverlay::PointerFrameOverlay() {
    normalPen_ = CreatePen(PS_SOLID, 3, RGB(47, 112, 219));
    pressedPen_ = CreatePen(PS_SOLID, 4, RGB(235, 65, 58));
    badgeBrush_ = CreateSolidBrush(RGB(28, 34, 44));
    timerFont_ = CreateFontW(24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
}

PointerFrameOverlay::~PointerFrameOverlay() {
    if (normalPen_ != nullptr) DeleteObject(normalPen_);
    if (pressedPen_ != nullptr) DeleteObject(pressedPen_);
    if (badgeBrush_ != nullptr) DeleteObject(badgeBrush_);
    if (timerFont_ != nullptr) DeleteObject(timerFont_);
}

void PointerFrameOverlay::apply(FrameView& frame, std::int64_t elapsedMilliseconds) {
    POINT cursor {};
    GetCursorPos(&cursor);
    const int x = (cursor.x - frame.screenRect.x) * frame.width / frame.screenRect.width;
    const int y = (cursor.y - frame.screenRect.y) * frame.height / frame.screenRect.height;
    if (x < 0 || x >= frame.width || y < 0 || y >= frame.height) return;

    const bool pressed = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    HGDIOBJ oldPen = SelectObject(frame.dc, pressed ? pressedPen_ : normalPen_);
    HGDIOBJ oldBrush = SelectObject(frame.dc, GetStockObject(HOLLOW_BRUSH));
    const int radius = pressed ? 18 : 12;
    Ellipse(frame.dc, x - radius, y - radius, x + radius, y + radius);
    MoveToEx(frame.dc, x - radius - 7, y, nullptr);
    LineTo(frame.dc, x + radius + 7, y);
    MoveToEx(frame.dc, x, y - radius - 7, nullptr);
    LineTo(frame.dc, x, y + radius + 7);
    SelectObject(frame.dc, oldBrush);
    SelectObject(frame.dc, oldPen);

    const int totalSeconds = static_cast<int>(elapsedMilliseconds / 1000);
    std::wostringstream timer;
    timer << std::setfill(L'0') << std::setw(2) << totalSeconds / 60 << L":"
          << std::setw(2) << totalSeconds % 60;
    RECT badge {14, 12, 126, 54};
    FillRect(frame.dc, &badge, badgeBrush_);
    HGDIOBJ oldFont = SelectObject(frame.dc, timerFont_);
    SetBkMode(frame.dc, TRANSPARENT);
    SetTextColor(frame.dc, RGB(255, 255, 255));
    RECT timerRect {28, 12, 118, 54};
    DrawTextW(frame.dc, timer.str().c_str(), -1, &timerRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(frame.dc, oldFont);
}
const wchar_t* PointerFrameOverlay::name() const { return L"鼠标与计时标记"; }

MediaFoundationH264Encoder::~MediaFoundationH264Encoder() {
    std::wstring ignored;
    finalize(ignored);
}

bool MediaFoundationH264Encoder::open(const std::wstring& path, int width, int height, int fps,
                                      std::wstring& error) {
    shutdown();
    mediaTypeRejected_ = false;
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    comInitialized_ = SUCCEEDED(result);
    if (FAILED(result) && result != RPC_E_CHANGED_MODE) {
        error = hresultText(L"COM 初始化", result);
        return false;
    }
    result = MFStartup(MF_VERSION);
    if (FAILED(result)) {
        error = hresultText(L"Media Foundation 初始化", result);
        shutdown();
        return false;
    }
    mediaFoundationStarted_ = true;

    IMFAttributes* attributes = nullptr;
    IMFMediaType* outputType = nullptr;
    IMFMediaType* inputType = nullptr;
    result = MFCreateAttributes(&attributes, 3);
    if (SUCCEEDED(result)) result = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    if (SUCCEEDED(result)) result = attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    if (SUCCEEDED(result)) result = attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
    if (SUCCEEDED(result)) result = MFCreateSinkWriterFromURL(path.c_str(), nullptr, attributes, &writer_);
    if (SUCCEEDED(result)) result = MFCreateMediaType(&outputType);
    if (SUCCEEDED(result)) result = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(result)) result = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    const UINT32 bitrate = static_cast<UINT32>(std::clamp(
        static_cast<long long>(width) * height * std::max(1, fps) / 8,
        3'000'000LL, 16'000'000LL));
    if (SUCCEEDED(result)) result = outputType->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
    if (SUCCEEDED(result)) result = outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(result)) result = MFSetAttributeSize(outputType, MF_MT_FRAME_SIZE, width, height);
    if (SUCCEEDED(result)) result = MFSetAttributeRatio(outputType, MF_MT_FRAME_RATE, fps, 1);
    if (SUCCEEDED(result)) result = MFSetAttributeRatio(outputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (SUCCEEDED(result)) result = writer_->AddStream(outputType, &streamIndex_);

    if (SUCCEEDED(result)) result = MFCreateMediaType(&inputType);
    if (SUCCEEDED(result)) result = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(result)) result = inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (SUCCEEDED(result)) result = inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(result)) result = inputType->SetUINT32(MF_MT_DEFAULT_STRIDE,
                                                         static_cast<UINT32>(width * 4));
    if (SUCCEEDED(result)) result = inputType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    if (SUCCEEDED(result)) result = MFSetAttributeSize(inputType, MF_MT_FRAME_SIZE, width, height);
    if (SUCCEEDED(result)) result = MFSetAttributeRatio(inputType, MF_MT_FRAME_RATE, fps, 1);
    if (SUCCEEDED(result)) result = MFSetAttributeRatio(inputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (SUCCEEDED(result)) result = writer_->SetInputMediaType(streamIndex_, inputType, nullptr);
    if (SUCCEEDED(result)) result = writer_->BeginWriting();

    safeRelease(attributes);
    safeRelease(outputType);
    safeRelease(inputType);
    if (FAILED(result)) {
        mediaTypeRejected_ = result == MF_E_INVALIDMEDIATYPE;
        error = mediaTypeRejected_
            ? hresultText(L"当前分辨率与帧率组合不受系统 H.264 编码器支持", result)
            : hresultText(L"H.264 编码器创建", result);
        shutdown();
        return false;
    }
    writing_ = true;
    return true;
}

bool MediaFoundationH264Encoder::writeFrame(const FrameView& frame, std::int64_t time100ns,
                                            std::int64_t duration100ns, std::wstring& error) {
    if (!writing_ || writer_ == nullptr) return false;
    IMFMediaBuffer* buffer = nullptr;
    IMFSample* sample = nullptr;
    const DWORD frameBytes = static_cast<DWORD>(frame.stride * frame.height);
    HRESULT result = MFCreateMemoryBuffer(frameBytes, &buffer);
    BYTE* destination = nullptr;
    DWORD maxLength = 0;
    if (SUCCEEDED(result)) result = buffer->Lock(&destination, &maxLength, nullptr);
    if (SUCCEEDED(result)) {
        std::memcpy(destination, frame.pixels, frameBytes);
        buffer->Unlock();
        destination = nullptr;
        result = buffer->SetCurrentLength(frameBytes);
    }
    if (SUCCEEDED(result)) result = MFCreateSample(&sample);
    if (SUCCEEDED(result)) result = sample->AddBuffer(buffer);
    if (SUCCEEDED(result)) result = sample->SetSampleTime(time100ns);
    if (SUCCEEDED(result)) result = sample->SetSampleDuration(duration100ns);
    if (SUCCEEDED(result)) result = writer_->WriteSample(streamIndex_, sample);
    if (destination != nullptr && buffer != nullptr) buffer->Unlock();
    safeRelease(sample);
    safeRelease(buffer);
    if (FAILED(result)) {
        error = hresultText(L"写入视频帧", result);
        return false;
    }
    return true;
}

bool MediaFoundationH264Encoder::finalize(std::wstring& error) {
    bool success = true;
    if (writing_ && writer_ != nullptr) {
        const HRESULT result = writer_->Finalize();
        if (FAILED(result)) {
            error = hresultText(L"完成 MP4 文件", result);
            success = false;
        }
    }
    shutdown();
    return success;
}

void MediaFoundationH264Encoder::shutdown() {
    writing_ = false;
    safeRelease(writer_);
    if (mediaFoundationStarted_) MFShutdown();
    mediaFoundationStarted_ = false;
    if (comInitialized_) CoUninitialize();
    comInitialized_ = false;
}

const wchar_t* MediaFoundationH264Encoder::name() const { return L"H.264 MP4（Media Foundation）"; }

bool MediaFoundationH264Encoder::canRetryWithLowerFrameRate() const {
    return mediaTypeRejected_;
}

ScreenRecorder::~ScreenRecorder() { stop(); }

bool ScreenRecorder::start(std::unique_ptr<FrameCaptureSource> source,
                           std::unique_ptr<FrameOverlayRenderer> overlay,
                           std::unique_ptr<VideoEncoder> encoder,
                           const std::wstring& outputPath, int fps) {
    if (recording_.load() || source == nullptr || overlay == nullptr || encoder == nullptr ||
        outputPath.empty()) return false;
    if (worker_.joinable()) worker_.join();
    stopRequested_ = false;
    finalizing_ = false;
    framesWritten_ = 0;
    sourceFrames_ = 0;
    droppedFrames_ = 0;
    elapsedMilliseconds_ = 0;
    {
        std::lock_guard<std::mutex> lock(previewMutex_);
        previewBgra_.clear();
        previewWidth_ = 0;
        previewHeight_ = 0;
    }
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        outputPath_ = outputPath;
        status_ = L"正在初始化 MP4 编码器";
    }
    recording_ = true;
    worker_ = std::thread(&ScreenRecorder::recordLoop, this, std::move(source),
                          std::move(overlay), std::move(encoder), outputPath,
                          std::clamp(fps, 10, 240));
    return true;
}

void ScreenRecorder::stop() {
    requestStop();
    if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) worker_.join();
}

void ScreenRecorder::requestStop() {
    stopRequested_ = true;
    if (recording_.load()) setStatus(L"正在封装 MP4，请稍候…");
}

void ScreenRecorder::recordLoop(std::unique_ptr<FrameCaptureSource> source,
                                std::unique_ptr<FrameOverlayRenderer> overlay,
                                std::unique_ptr<VideoEncoder> encoder,
                                std::wstring outputPath, int fps) {
    std::wstring error;
    const CaptureRect captureBounds = source->bounds();
    const int requestedFps = fps;
    int encoderFps = requestedFps;
    bool encoderOpened = false;
    while (!(encoderOpened = encoder->open(outputPath, captureBounds.width, captureBounds.height,
                                           encoderFps, error))) {
        if (!encoder->canRetryWithLowerFrameRate()) break;
        const int fallbackRates[] = {165, 120, 60, 30, 24, 15};
        const auto fallback = std::find_if(std::begin(fallbackRates), std::end(fallbackRates),
            [encoderFps](int candidate) { return candidate < encoderFps; });
        if (fallback == std::end(fallbackRates)) break;
        encoderFps = *fallback;
        setStatus(L"当前帧率不受编码器支持，正在尝试 " + std::to_wstring(encoderFps) + L" FPS");
    }
    if (!encoderOpened) {
        setStatus(error);
        deleteEmptyOutput(outputPath);
        recording_ = false;
        return;
    }
    std::wstring recordingStatus = std::wstring(L"正在录制：") + source->name() + L" · " + overlay->name();
    if (encoderFps != requestedFps) {
        recordingStatus += L" · " + std::to_wstring(requestedFps) + L" FPS 不受支持，已自动使用 " +
            std::to_wstring(encoderFps) + L" FPS";
    }
    setStatus(recordingStatus);
    const auto started = std::chrono::steady_clock::now();
    const auto frameInterval = std::chrono::microseconds(1'000'000 / encoderFps);
    const std::int64_t duration100ns = 10'000'000LL / encoderFps;
    auto nextFrame = started;
    int frameIndex = 0;
    bool failed = false;
    while (!stopRequested_.load()) {
        FrameView frame;
        if (!source->capture(frame, error)) {
            setStatus(error);
            failed = true;
            break;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count();
        const auto sampleTime100ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now - started).count() / 100;
        elapsedMilliseconds_ = static_cast<int>(elapsed);
        overlay->apply(frame, elapsed);
        if (!encoder->writeFrame(frame, sampleTime100ns, duration100ns, error)) {
            setStatus(error);
            failed = true;
            break;
        }
        ++frameIndex;
        framesWritten_ = frameIndex;
        if (frame.sourceFrame) ++sourceFrames_;

        const int previewEveryFrames = std::max(1, (encoderFps + 7) / 8);
        if (frameIndex % previewEveryFrames == 0) {
            const int targetWidth = std::min(1280, frame.width);
            const int targetHeight = std::max(1, frame.height * targetWidth / frame.width);
            std::vector<std::uint8_t> preview(
                static_cast<std::size_t>(targetWidth) * targetHeight * 4);
            for (int y = 0; y < targetHeight; ++y) {
                const int sourceY = y * frame.height / targetHeight;
                for (int x = 0; x < targetWidth; ++x) {
                    const int sourceX = x * frame.width / targetWidth;
                    const auto* sourcePixel = frame.pixels + sourceY * frame.stride + sourceX * 4;
                    auto* targetPixel = preview.data() +
                        (static_cast<std::size_t>(y) * targetWidth + x) * 4;
                    std::memcpy(targetPixel, sourcePixel, 4);
                }
            }
            std::lock_guard<std::mutex> lock(previewMutex_);
            previewWidth_ = targetWidth;
            previewHeight_ = targetHeight;
            previewBgra_.swap(preview);
        }

        nextFrame += frameInterval;
        const auto afterEncoding = std::chrono::steady_clock::now();
        if (frameIndex == 1) {
            // The H.264 transform performs one-time setup on the first sample.
            // Start pacing after that warm-up instead of reporting it as dropped frames.
            nextFrame = afterEncoding + frameInterval;
            std::this_thread::sleep_until(nextFrame);
        } else if (afterEncoding < nextFrame) {
            std::this_thread::sleep_until(nextFrame);
        } else if (afterEncoding - nextFrame >= frameInterval) {
            const int skipped = static_cast<int>((afterEncoding - nextFrame) / frameInterval);
            droppedFrames_ += skipped;
            nextFrame += frameInterval * skipped;
        }
    }
    finalizing_ = true;
    if (!failed) setStatus(L"正在完成 MP4 文件，通常只需几秒...");
    const bool finalized = encoder->finalize(error);
    finalizing_ = false;
    if (!finalized) setStatus(error);
    else if (stopRequested_.load()) setStatus(L"录像完成，MP4 文件已经写入磁盘");
    else if (failed) setStatus(error);
    recording_ = false;
}

void ScreenRecorder::setStatus(const std::wstring& status) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    status_ = status;
}

RecorderStats ScreenRecorder::stats() const {
    RecorderStats result;
    result.recording = recording_.load();
    result.finalizing = finalizing_.load();
    result.framesWritten = framesWritten_.load();
    result.sourceFrames = sourceFrames_.load();
    result.droppedFrames = droppedFrames_.load();
    result.elapsedMilliseconds = elapsedMilliseconds_.load();
    result.actualFps = result.elapsedMilliseconds > 0
        ? result.framesWritten * 1000.0 / result.elapsedMilliseconds : 0.0;
    result.sourceFps = result.elapsedMilliseconds > 0
        ? result.sourceFrames * 1000.0 / result.elapsedMilliseconds : 0.0;
    std::lock_guard<std::mutex> lock(stateMutex_);
    result.status = status_;
    result.outputPath = outputPath_;
    return result;
}

bool ScreenRecorder::copyPreview(std::vector<std::uint8_t>& bgra, int& width, int& height) const {
    std::lock_guard<std::mutex> lock(previewMutex_);
    if (previewBgra_.empty()) return false;
    bgra = previewBgra_;
    width = previewWidth_;
    height = previewHeight_;
    return true;
}

} // namespace desktopflow
