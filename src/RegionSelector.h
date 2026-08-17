#pragma once

#include "ScreenRecorder.h"

namespace desktopflow {

class RegionSelector {
public:
    bool select(CaptureRect& result, HWND owner) const;
};

} // namespace desktopflow
