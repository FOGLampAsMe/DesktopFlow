#pragma once

#include "ScreenRecorder.h"

#include <QImage>
#include <QMainWindow>

#include <chrono>
#include <memory>
#include <vector>

class QCloseEvent;
class QComboBox;
class QLabel;
class QPushButton;
class QString;
class QTimer;

namespace desktopflow {

class DesktopFlowApp final : public QMainWindow {
public:
    explicit DesktopFlowApp(QWidget* parent = nullptr);
    ~DesktopFlowApp() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QWidget* createVideoPage();
    void refreshVideo();
    void refreshVideoPreview();
    void stopEverything();
    void selectCaptureRegion();
    void startOrStopVideoRecording();
    void updateVideoControls(const RecorderStats& stats);
    std::wstring chooseVideoPath() const;
    static QImage bgraToImage(const std::vector<std::uint8_t>& bgra, int width, int height);

    ScreenRecorder screenRecorder_;
    QTimer* updateTimer_ = nullptr;

    QPushButton* videoRecordButton_ = nullptr;
    QPushButton* regionButton_ = nullptr;
    QComboBox* videoSourceCombo_ = nullptr;
    QComboBox* videoOverlayCombo_ = nullptr;
    QComboBox* videoFpsCombo_ = nullptr;
    QLabel* videoPreviewLabel_ = nullptr;
    QLabel* videoStatsLabel_ = nullptr;
    QLabel* videoStatusLabel_ = nullptr;

    CaptureRect selectedRegion_ {};
    bool hasSelectedRegion_ = false;
    bool videoStopPending_ = false;
    bool videoErrorShown_ = false;
    RecorderStats lastRecorderStats_;
    std::chrono::steady_clock::time_point lastVideoPreviewAt_;
};

} // namespace desktopflow
