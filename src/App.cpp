#include "App.h"

#include "RegionSelector.h"

#include <QApplication>
#include <QComboBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QPixmap>
#include <QStatusBar>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <windows.h>

namespace desktopflow {
namespace {

QString wide(const std::wstring& value) {
    return QString::fromStdWString(value);
}

} // namespace

DesktopFlowApp::DesktopFlowApp(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("DesktopFlow - 本地屏幕录像"));
    resize(1180, 760);
    setMinimumSize(900, 620);
    setStyleSheet(QStringLiteral(
        "QMainWindow { background: #f3f4f6; }"
        "QWidget { font-family: 'Microsoft YaHei UI'; font-size: 13px; color: #20242a; }"
        "QWidget#toolbar { background: #ffffff; border: 1px solid #dfe2e7; border-radius: 8px; }"
        "QWidget#previewSurface { background: #171a1f; border-radius: 8px; }"
        "QWidget#statusStrip { background: transparent; }"
        "QLabel#brand { font-size: 22px; font-weight: 600; color: #17191d; }"
        "QLabel#fieldLabel { color: #6f7680; font-size: 12px; }"
        "QLabel#previewPlaceholder { color: #7e8792; font-size: 13px; }"
        "QLabel#stats { color: #4e5661; font-size: 12px; }"
        "QLabel#status { color: #68717d; font-size: 12px; }"
        "QComboBox { min-height: 34px; padding: 0 28px 0 10px; background: #f7f8fa; "
        "border: 1px solid #d9dde3; border-radius: 6px; }"
        "QComboBox:hover { border-color: #aeb5bf; background: #ffffff; }"
        "QComboBox:focus { border-color: #4c7bd9; background: #ffffff; }"
        "QPushButton { min-height: 34px; padding: 0 14px; border: 1px solid #d7dbe1; "
        "border-radius: 6px; background: #ffffff; color: #343a43; }"
        "QPushButton:hover { background: #f4f6f8; border-color: #aeb5bf; }"
        "QPushButton:disabled { color: #a6acb4; background: #f1f2f4; }"
        "QPushButton#recordButton { min-width: 116px; color: #ffffff; background: #e5484d; "
        "border-color: #e5484d; font-weight: 600; }"
        "QPushButton#recordButton:hover { background: #d93d42; border-color: #d93d42; }"
        "QPushButton#recordButton[recording='true'] { background: #2f343b; border-color: #2f343b; }"));

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(18, 14, 18, 14);
    root->setSpacing(10);

    auto* header = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("DesktopFlow"));
    title->setObjectName(QStringLiteral("brand"));
    header->addWidget(title);
    header->addStretch();
    root->addLayout(header);

    root->addWidget(createVideoPage(), 1);
    setCentralWidget(central);

    statusBar()->hide();
    updateTimer_ = new QTimer(this);
    connect(updateTimer_, &QTimer::timeout, this, &DesktopFlowApp::refreshVideo);
    updateTimer_->start(100);
    lastVideoPreviewAt_ = std::chrono::steady_clock::now() - std::chrono::seconds(2);
    const HWND appWindow = reinterpret_cast<HWND>(winId());
    // Keep the control window out of desktop capture to prevent recursive previews.
    if (!SetWindowDisplayAffinity(appWindow, WDA_EXCLUDEFROMCAPTURE)) {
        SetWindowDisplayAffinity(appWindow, WDA_MONITOR);
    }
}

DesktopFlowApp::~DesktopFlowApp() {
    stopEverything();
}

QWidget* DesktopFlowApp::createVideoPage() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto* toolbar = new QWidget();
    toolbar->setObjectName(QStringLiteral("toolbar"));
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(12, 10, 12, 10);
    toolbarLayout->setSpacing(10);

    auto* sourceLabel = new QLabel(QStringLiteral("范围"));
    sourceLabel->setObjectName(QStringLiteral("fieldLabel"));
    videoSourceCombo_ = new QComboBox();
    videoSourceCombo_->addItems({QStringLiteral("整个桌面"), QStringLiteral("自定义区域")});
    videoSourceCombo_->setMinimumWidth(128);
    regionButton_ = new QPushButton(QStringLiteral("选择区域"));

    auto* fpsLabel = new QLabel(QStringLiteral("帧率"));
    fpsLabel->setObjectName(QStringLiteral("fieldLabel"));
    videoFpsCombo_ = new QComboBox();
    videoFpsCombo_->addItem(QStringLiteral("15 FPS"), 15);
    videoFpsCombo_->addItem(QStringLiteral("24 FPS"), 24);
    videoFpsCombo_->addItem(QStringLiteral("30 FPS"), 30);
    videoFpsCombo_->addItem(QStringLiteral("60 FPS"), 60);
    videoFpsCombo_->addItem(QStringLiteral("120 FPS"), 120);
    videoFpsCombo_->addItem(QStringLiteral("165 FPS"), 165);
    videoFpsCombo_->addItem(QStringLiteral("240 FPS（重复帧）"), 240);
    videoFpsCombo_->setMinimumWidth(118);
    videoFpsCombo_->setCurrentIndex(videoFpsCombo_->findData(60));

    auto* overlayLabel = new QLabel(QStringLiteral("标记"));
    overlayLabel->setObjectName(QStringLiteral("fieldLabel"));
    videoOverlayCombo_ = new QComboBox();
    videoOverlayCombo_->addItems({QStringLiteral("无标记"), QStringLiteral("鼠标标记")});
    videoOverlayCombo_->setMinimumWidth(110);

    videoRecordButton_ = new QPushButton(QStringLiteral("开始录制"));
    videoRecordButton_->setObjectName(QStringLiteral("recordButton"));
    videoRecordButton_->setProperty("recording", false);

    toolbarLayout->addWidget(sourceLabel);
    toolbarLayout->addWidget(videoSourceCombo_);
    toolbarLayout->addWidget(regionButton_);
    toolbarLayout->addSpacing(6);
    toolbarLayout->addWidget(fpsLabel);
    toolbarLayout->addWidget(videoFpsCombo_);
    toolbarLayout->addSpacing(6);
    toolbarLayout->addWidget(overlayLabel);
    toolbarLayout->addWidget(videoOverlayCombo_);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(videoRecordButton_);
    layout->addWidget(toolbar);

    auto* previewSurface = new QWidget();
    previewSurface->setObjectName(QStringLiteral("previewSurface"));
    auto* previewLayout = new QVBoxLayout(previewSurface);
    previewLayout->setContentsMargins(1, 1, 1, 1);
    videoPreviewLabel_ = new QLabel(QStringLiteral("录制开始后显示预览"));
    videoPreviewLabel_->setObjectName(QStringLiteral("previewPlaceholder"));
    videoPreviewLabel_->setAlignment(Qt::AlignCenter);
    videoPreviewLabel_->setMinimumSize(520, 320);
    videoPreviewLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    previewLayout->addWidget(videoPreviewLabel_);
    layout->addWidget(previewSurface, 1);

    auto* statusStrip = new QWidget();
    statusStrip->setObjectName(QStringLiteral("statusStrip"));
    auto* statusLayout = new QHBoxLayout(statusStrip);
    statusLayout->setContentsMargins(4, 2, 4, 2);
    videoStatsLabel_ = new QLabel(QStringLiteral("00:00  ·  0.0 FPS  ·  0 帧  ·  丢帧 0"));
    videoStatsLabel_->setObjectName(QStringLiteral("stats"));
    videoStatusLabel_ = new QLabel(QStringLiteral("准备就绪"));
    videoStatusLabel_->setObjectName(QStringLiteral("status"));
    videoStatusLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    statusLayout->addWidget(videoStatsLabel_);
    statusLayout->addStretch();
    statusLayout->addWidget(videoStatusLabel_, 1);
    layout->addWidget(statusStrip);

    connect(videoRecordButton_, &QPushButton::clicked, this, &DesktopFlowApp::startOrStopVideoRecording);
    connect(regionButton_, &QPushButton::clicked, this, &DesktopFlowApp::selectCaptureRegion);
    connect(videoSourceCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        regionButton_->setVisible(index == 1);
        regionButton_->setEnabled(index == 1 && !screenRecorder_.stats().recording);
        videoStatusLabel_->setText(index == 0 ? QStringLiteral("准备就绪") :
            (hasSelectedRegion_ ? QStringLiteral("区域已选择") : QStringLiteral("请选择区域")));
    });
    regionButton_->setVisible(false);
    return page;
}

void DesktopFlowApp::refreshVideo() {
    const RecorderStats stats = screenRecorder_.stats();
    const bool changed = stats.recording != lastRecorderStats_.recording ||
        stats.finalizing != lastRecorderStats_.finalizing ||
        stats.framesWritten != lastRecorderStats_.framesWritten ||
        stats.sourceFrames != lastRecorderStats_.sourceFrames ||
        stats.droppedFrames != lastRecorderStats_.droppedFrames ||
        stats.status != lastRecorderStats_.status;
    const auto now = std::chrono::steady_clock::now();
    if (stats.recording && !stats.finalizing &&
        now - lastVideoPreviewAt_ >= std::chrono::milliseconds(100)) {
        refreshVideoPreview();
        lastVideoPreviewAt_ = now;
    }
    if (changed || stats.recording != lastRecorderStats_.recording) {
        lastRecorderStats_ = stats;
        updateVideoControls(stats);
        const int totalSeconds = stats.elapsedMilliseconds / 1000;
        const QString elapsed = QStringLiteral("%1:%2")
            .arg(totalSeconds / 60, 2, 10, QChar('0'))
            .arg(totalSeconds % 60, 2, 10, QChar('0'));
        videoStatsLabel_->setText(QStringLiteral("%1   %2 FPS   %3 帧   丢帧 %4")
                                  .arg(elapsed)
                                  .arg(stats.actualFps, 0, 'f', 1)
                                  .arg(stats.framesWritten)
                                  .arg(stats.droppedFrames));
        videoStatsLabel_->setToolTip(QStringLiteral("桌面真实更新：%1 帧，%2 FPS")
                                     .arg(stats.sourceFrames)
                                     .arg(stats.sourceFps, 0, 'f', 1));
        QString statusText;
        if (stats.finalizing) statusText = QStringLiteral("正在保存");
        else if (stats.recording) {
            statusText = stats.status.find(L"已自动使用") != std::wstring::npos
                ? QStringLiteral("正在录制（帧率已自动调整）")
                : QStringLiteral("正在录制");
        } else if (stats.status.find(L"完成") != std::wstring::npos) {
            statusText = QStringLiteral("已保存");
        } else if (stats.status == L"录像未开始") {
            statusText = QStringLiteral("准备就绪");
        } else {
            statusText = wide(stats.status);
        }
        videoStatusLabel_->setText(statusText);
        videoStatusLabel_->setToolTip(wide(stats.status));
        if (!stats.recording && stats.framesWritten == 0 && !videoErrorShown_ &&
            stats.status.find(L"失败") != std::wstring::npos) {
            videoErrorShown_ = true;
            QMessageBox::warning(this, QStringLiteral("录制启动失败"), wide(stats.status));
        }
    }
}

void DesktopFlowApp::refreshVideoPreview() {
    std::vector<std::uint8_t> bgra;
    int width = 0;
    int height = 0;
    if (!screenRecorder_.copyPreview(bgra, width, height)) return;
    const QImage image = bgraToImage(bgra, width, height);
    if (image.isNull()) return;
    videoPreviewLabel_->setPixmap(QPixmap::fromImage(image).scaled(
        videoPreviewLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void DesktopFlowApp::updateVideoControls(const RecorderStats& stats) {
    if (!stats.recording) {
        videoStopPending_ = false;
        videoRecordButton_->setText(QStringLiteral("开始录制"));
        videoRecordButton_->setEnabled(true);
        videoRecordButton_->setProperty("recording", false);
    } else if (stats.finalizing || videoStopPending_) {
        videoRecordButton_->setText(QStringLiteral("正在保存…"));
        videoRecordButton_->setEnabled(false);
        videoRecordButton_->setProperty("recording", true);
    } else {
        videoRecordButton_->setText(QStringLiteral("停止录制"));
        videoRecordButton_->setEnabled(true);
        videoRecordButton_->setProperty("recording", true);
    }
    videoRecordButton_->style()->unpolish(videoRecordButton_);
    videoRecordButton_->style()->polish(videoRecordButton_);
    videoSourceCombo_->setEnabled(!stats.recording);
    videoOverlayCombo_->setEnabled(!stats.recording);
    videoFpsCombo_->setEnabled(!stats.recording);
    const bool customRegion = videoSourceCombo_->currentIndex() == 1;
    regionButton_->setVisible(customRegion);
    regionButton_->setEnabled(!stats.recording && customRegion);
}

void DesktopFlowApp::stopEverything() {
    screenRecorder_.stop();
}

std::wstring DesktopFlowApp::chooseVideoPath() const {
    QString movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (movies.isEmpty()) movies = QDir::homePath();
    const QString outputDirectory = QDir(movies).filePath(QStringLiteral("DesktopFlow"));
    QDir().mkpath(outputDirectory);
    const QString defaultName = QStringLiteral("DesktopFlow-%1.mp4")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    return QFileDialog::getSaveFileName(const_cast<DesktopFlowApp*>(this), QStringLiteral("保存录像"),
                                        QDir(outputDirectory).filePath(defaultName),
                                        QStringLiteral("MP4 视频 (*.mp4)")).toStdWString();
}

void DesktopFlowApp::selectCaptureRegion() {
    if (screenRecorder_.stats().recording) return;
    showMinimized();
    QApplication::processEvents();
    Sleep(180);
    RegionSelector selector;
    CaptureRect selection;
    const bool accepted = selector.select(selection, reinterpret_cast<HWND>(winId()));
    showNormal();
    activateWindow();
    raise();
    if (accepted) {
        selectedRegion_ = selection;
        hasSelectedRegion_ = true;
        videoStatusLabel_->setText(QStringLiteral("区域 %1 × %2").arg(selection.width).arg(selection.height));
    } else {
        videoStatusLabel_->setText(QStringLiteral("未选择区域"));
    }
}

void DesktopFlowApp::startOrStopVideoRecording() {
    if (screenRecorder_.stats().recording) {
        videoStopPending_ = true;
        screenRecorder_.requestStop();
        return;
    }
    if (videoSourceCombo_->currentIndex() == 1 && !hasSelectedRegion_) {
        selectCaptureRegion();
        if (!hasSelectedRegion_) return;
    }
    const std::wstring path = chooseVideoPath();
    if (path.empty()) return;
    std::unique_ptr<FrameCaptureSource> source;
    if (videoSourceCombo_->currentIndex() == 0) source = std::make_unique<DxgiDesktopCaptureSource>();
    else source = std::make_unique<RegionCaptureSource>(selectedRegion_);
    std::unique_ptr<FrameOverlayRenderer> overlay;
    if (videoOverlayCombo_->currentIndex() == 0) overlay = std::make_unique<CleanFrameOverlay>();
    else overlay = std::make_unique<PointerFrameOverlay>();
    const int fps = videoFpsCombo_->currentData().toInt();
    if (!screenRecorder_.start(std::move(source), std::move(overlay),
                               std::make_unique<MediaFoundationH264Encoder>(), path, fps)) {
        QMessageBox::warning(this, QStringLiteral("无法开始录制"), QStringLiteral("录像线程启动失败。"));
        return;
    }
    videoStopPending_ = false;
    videoErrorShown_ = false;
    videoStatusLabel_->setText(QStringLiteral("正在准备录制"));
}

QImage DesktopFlowApp::bgraToImage(const std::vector<std::uint8_t>& bgra, int width, int height) {
    if (width <= 0 || height <= 0 || bgra.size() < static_cast<std::size_t>(width) * height * 4) return {};
    return QImage(bgra.data(), width, height, width * 4, QImage::Format_ARGB32).copy();
}

void DesktopFlowApp::closeEvent(QCloseEvent* event) {
    if (screenRecorder_.stats().recording) {
        const auto answer = QMessageBox::question(this, QStringLiteral("退出 DesktopFlow"),
            QStringLiteral("当前仍在录制，退出会停止录制并完成 MP4 封装。继续退出？"));
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }
    stopEverything();
    event->accept();
}

} // namespace desktopflow
