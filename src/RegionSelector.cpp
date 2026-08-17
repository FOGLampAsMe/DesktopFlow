#include "RegionSelector.h"

#include <algorithm>
#include <string>
#include <windowsx.h>

namespace desktopflow {
namespace {

struct SelectionState {
    POINT start {};
    POINT current {};
    bool dragging = false;
    bool accepted = false;
};

LRESULT CALLBACK selectorProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<SelectionState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_NCCREATE: {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_LBUTTONDOWN:
        if (state != nullptr) {
            state->start = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            state->current = state->start;
            state->dragging = true;
            SetCapture(window);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (state != nullptr && state->dragging) {
            state->current = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (state != nullptr && state->dragging) {
            state->current = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            state->dragging = false;
            state->accepted = std::abs(state->current.x - state->start.x) >= 32 &&
                              std::abs(state->current.y - state->start.y) >= 32;
            ReleaseCapture();
            DestroyWindow(window);
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) DestroyWindow(window);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint {};
        HDC dc = BeginPaint(window, &paint);
        RECT client {};
        GetClientRect(window, &client);
        FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        HFONT font = CreateFontW(28, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        HGDIOBJ oldFont = SelectObject(dc, font);
        RECT hint {28, 20, client.right - 28, 70};
        DrawTextW(dc, L"按住鼠标拖出录像区域 · Esc 取消", -1, &hint,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (state != nullptr && state->dragging) {
            RECT selected {
                std::min(state->start.x, state->current.x),
                std::min(state->start.y, state->current.y),
                std::max(state->start.x, state->current.x),
                std::max(state->start.y, state->current.y)
            };
            HPEN pen = CreatePen(PS_SOLID, 4, RGB(73, 151, 255));
            HGDIOBJ oldPen = SelectObject(dc, pen);
            HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(dc, selected.left, selected.top, selected.right, selected.bottom);
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            DeleteObject(pen);
        }
        SelectObject(dc, oldFont);
        DeleteObject(font);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

} // namespace

bool RegionSelector::select(CaptureRect& result, HWND owner) const {
    static const wchar_t* className = L"DesktopFlowRegionSelector";
    static bool registered = false;
    if (!registered) {
        WNDCLASSW windowClass {};
        windowClass.lpfnWndProc = selectorProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        windowClass.lpszClassName = className;
        registered = RegisterClassW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    if (!registered) return false;

    const CaptureRect desktop = virtualDesktopRect();
    SelectionState state;
    HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
                                  className, L"选择录像区域", WS_POPUP,
                                  desktop.x, desktop.y, desktop.width, desktop.height,
                                  owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (window == nullptr) return false;
    SetLayeredWindowAttributes(window, 0, 150, LWA_ALPHA);
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    SetFocus(window);

    MSG message {};
    while (IsWindow(window) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (!state.accepted) return false;
    result.x = desktop.x + std::min(state.start.x, state.current.x);
    result.y = desktop.y + std::min(state.start.y, state.current.y);
    result.width = std::abs(state.current.x - state.start.x);
    result.height = std::abs(state.current.y - state.start.y);
    result.width -= result.width % 2;
    result.height -= result.height % 2;
    return result.width >= 32 && result.height >= 32;
}

} // namespace desktopflow
