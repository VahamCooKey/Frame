// Windows-only implementation of INativeWindow. The whole file may assume
// Win32; project lint forbids _WIN32 ifdefs anywhere outside src/platform/.
//
// Keep the Win32 SDK headers ahead of any Qt include and define NOMINMAX so
// the min/max macros cannot collide with Qt/std headers pulled in below.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <shobjidl.h>
// GET_X_LPARAM/GET_Y_LPARAM for the WM_NCHITTEST border hit-test below.
#include <windowsx.h>

// Older SDKs do not declare these; values per Microsoft docs.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_TRANSITIONS_FORCEDISABLED
#define DWMWA_TRANSITIONS_FORCEDISABLED 3
#endif

#include "NativeWindowWin32.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QQuickWindow>
#include <QTimer>

#include <algorithm>

namespace {

// Plain 1px-tall black window that covers the row the main window leaves
// uncovered in fullscreen. It presents no swapchain of its own, so it is
// irrelevant to the NVIDIA full-screen classification; it just paints the
// seam solid black instead of showing the desktop.
constexpr wchar_t kEdgeCoverClassName[] = L"FrameEdgeCover";

ATOM ensureEdgeCoverClass()
{
    static const ATOM atom = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));
        wc.lpszClassName = kEdgeCoverClassName;
        return RegisterClassExW(&wc);
    }();
    return atom;
}

QString pathOfShellItem(IShellItem *item)
{
    if (!item)
        return {};
    QString path;
    PWSTR filePath = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &filePath))) {
        path = QString::fromWCharArray(filePath);
        CoTaskMemFree(filePath);
    }
    return path;
}

// F10: one dialog routine for both the single- and multi-select callers.
// With allowMultiple the returned list can hold more than one entry; the
// single-select wrappers just take the first.
QStringList openFileDialog(HWND owner, const wchar_t *title,
                           const COMDLG_FILTERSPEC *filters, UINT filterCount,
                           bool allowMultiple)
{
    const HRESULT initResult = CoInitializeEx(
        nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initResult) && initResult != RPC_E_CHANGED_MODE)
        return {};

    const bool uninitialize = SUCCEEDED(initResult);
    IFileOpenDialog *dialog = nullptr;
    const HRESULT createResult = CoCreateInstance(
        CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog));
    if (FAILED(createResult)) {
        if (uninitialize)
            CoUninitialize();
        return {};
    }

    FILEOPENDIALOGOPTIONS options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        options |= FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST
                   | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
        if (allowMultiple)
            options |= FOS_ALLOWMULTISELECT;
        dialog->SetOptions(options);
    }
    dialog->SetTitle(title);
    dialog->SetFileTypes(filterCount, filters);
    dialog->SetFileTypeIndex(1);

    QStringList paths;
    if (SUCCEEDED(dialog->Show(owner))) {
        if (allowMultiple) {
            IShellItemArray *items = nullptr;
            if (SUCCEEDED(dialog->GetResults(&items))) {
                DWORD count = 0;
                if (SUCCEEDED(items->GetCount(&count))) {
                    for (DWORD i = 0; i < count; ++i) {
                        IShellItem *item = nullptr;
                        if (FAILED(items->GetItemAt(i, &item)))
                            continue;
                        const QString path = pathOfShellItem(item);
                        if (!path.isEmpty())
                            paths.append(path);
                        item->Release();
                    }
                }
                items->Release();
            }
        } else {
            IShellItem *item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                const QString path = pathOfShellItem(item);
                if (!path.isEmpty())
                    paths.append(path);
                item->Release();
            }
        }
    }

    dialog->Release();
    if (uninitialize)
        CoUninitialize();
    return paths;
}

const COMDLG_FILTERSPEC *mediaFilters(UINT *count)
{
    static const COMDLG_FILTERSPEC filters[] = {
        {L"Video files", L"*.mp4;*.mkv;*.avi;*.mov;*.webm;*.m4v;*.flv;*.wmv;*.ts;*.mpg;*.mpeg"},
        {L"Audio files", L"*.mp3;*.flac;*.wav;*.ogg;*.opus;*.m4a;*.aac;*.wma"},
        {L"All files", L"*.*"}
    };
    *count = ARRAYSIZE(filters);
    return filters;
}

} // namespace

NativeWindowWin32::NativeWindowWin32(QObject *parent)
    : NativeWindowBase(parent)
{
}

NativeWindowWin32::~NativeWindowWin32()
{
    // Restore Qt's window procedure before this object (which owns the
    // subclass state) goes away; a stray staticWndProc into a dead instance
    // would crash on the next message. The overlay is created and destroyed
    // on the GUI thread (this object lives on the stack in main).
    if (s_instance == this && s_subclassedHwnd && IsWindow(s_subclassedHwnd)) {
        SetWindowLongPtrW(s_subclassedHwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(s_originalProc));
    }
    s_instance = nullptr;
    s_subclassedHwnd = nullptr;
    s_originalProc = nullptr;
    if (m_edgeCover)
        DestroyWindow(m_edgeCover);
}

void NativeWindowWin32::attach(QQuickWindow *window)
{
    NativeWindowBase::attach(window);
    if (!m_window)
        return;

    const HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
    if (!hwnd)
        return;

    // Native resize borders: subclass the window procedure (see
    // staticWndProc below). A QAbstractNativeEventFilter turned out not to
    // see mouse button messages for this window at all on Qt 6.8 — the
    // hit-test answers never reached the system either (no sizing cursor),
    // while QML still received the presses. The subclass sees every message.
    // Guard against a repeated attach(): restore before re-subclassing.
    if (s_subclassedHwnd && s_originalProc) {
        SetWindowLongPtrW(s_subclassedHwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(s_originalProc));
    }
    s_instance = this;
    s_subclassedHwnd = hwnd;
    s_originalProc = reinterpret_cast<SubclassedProc>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(&NativeWindowWin32::staticWndProc)));

    BOOL immersiveDark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
                          &immersiveDark, sizeof(immersiveDark));

    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                          &corner, sizeof(corner));

    // Kill the DWM zoom animation on show/hide/minimize/restore/fullscreen:
    // on a video player it reads as a sluggish fullscreen toggle.
    BOOL disableTransitions = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED,
                          &disableTransitions, sizeof(disableTransitions));
}

// Native resize borders for the frameless window. Over an 8px (physical)
// frame zone the subclassed window procedure:
//   - answers WM_NCHITTEST with the standard edge/corner codes so the
//     native sizing cursors show on hover, and
//   - turns a WM_NCLBUTTONDOWN inside the zone into
//     QWindow::startSystemResize — the same machinery the former QML edge
//     strips used, which is proven to engage the sizing loop on this
//     WS_POPUP window (the HT* codes alone do NOT: without WS_THICKFRAME,
//     DefWindowProc never runs a resize loop for them).
// No zone is claimed while fullscreen (the 1px-extended bottom row must
// stay inert) or maximized, matching native window behavior. Everything
// else chains to Qt's original window procedure untouched.
namespace {

// The border hit code at a screen point, or HTCLIENT when outside the zone.
LRESULT borderHitAt(LONG x, LONG y, const RECT &windowRect, LONG border)
{
    const bool nearLeft = x < windowRect.left + border;
    const bool nearRight = x >= windowRect.right - border;
    const bool nearTop = y < windowRect.top + border;
    const bool nearBottom = y >= windowRect.bottom - border;

    if (nearTop && nearLeft)
        return HTTOPLEFT;
    if (nearTop && nearRight)
        return HTTOPRIGHT;
    if (nearBottom && nearLeft)
        return HTBOTTOMLEFT;
    if (nearBottom && nearRight)
        return HTBOTTOMRIGHT;
    if (nearTop)
        return HTTOP;
    if (nearBottom)
        return HTBOTTOM;
    if (nearLeft)
        return HTLEFT;
    if (nearRight)
        return HTRIGHT;
    return HTCLIENT;
}

Qt::Edges edgesForHit(LRESULT hit)
{
    Qt::Edges edges;
    if (hit == HTTOP || hit == HTTOPLEFT || hit == HTTOPRIGHT)
        edges |= Qt::TopEdge;
    if (hit == HTBOTTOM || hit == HTBOTTOMLEFT || hit == HTBOTTOMRIGHT)
        edges |= Qt::BottomEdge;
    if (hit == HTLEFT || hit == HTTOPLEFT || hit == HTBOTTOMLEFT)
        edges |= Qt::LeftEdge;
    if (hit == HTRIGHT || hit == HTTOPRIGHT || hit == HTBOTTOMRIGHT)
        edges |= Qt::RightEdge;
    return edges;
}

HCURSOR cursorForHit(LRESULT hit)
{
    switch (hit) {
    case HTTOP:
    case HTBOTTOM:
        return LoadCursorW(nullptr, IDC_SIZENS);
    case HTLEFT:
    case HTRIGHT:
        return LoadCursorW(nullptr, IDC_SIZEWE);
    case HTTOPLEFT:
    case HTBOTTOMRIGHT:
        return LoadCursorW(nullptr, IDC_SIZENWSE);
    case HTTOPRIGHT:
    case HTBOTTOMLEFT:
        return LoadCursorW(nullptr, IDC_SIZENESW);
    default:
        return nullptr;
    }
}

bool matchesDisplaySize(int width, int height)
{
    struct Dimensions { int width; int height; bool matches; } size{width, height, false};
    EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            auto &size = *reinterpret_cast<Dimensions *>(data);
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info)
                && size.width == info.rcMonitor.right - info.rcMonitor.left
                && size.height == info.rcMonitor.bottom - info.rcMonitor.top) {
                size.matches = true;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&size));
    return size.matches;
}

} // namespace

// Subclass state. One window per process (the QML main window); a static
// instance pointer is sufficient and keeps staticWndProc a plain WNDPROC.
NativeWindowWin32 *NativeWindowWin32::s_instance = nullptr;
HWND NativeWindowWin32::s_subclassedHwnd = nullptr;
NativeWindowWin32::SubclassedProc NativeWindowWin32::s_originalProc = nullptr;

long long NativeWindowWin32::staticWndProc(HWND__ *hwndRaw,
                                           unsigned int message,
                                           unsigned long long wParam,
                                           long long lParam)
{
    const HWND hwnd = static_cast<HWND>(hwndRaw);
    NativeWindowWin32 *self = s_instance;
    if (!self || !self->m_window || hwnd != s_subclassedHwnd)
        return DefWindowProcW(hwnd, message, static_cast<WPARAM>(wParam),
                              static_cast<LPARAM>(lParam));

    const auto chain = [&]() -> LRESULT {
        return CallWindowProcW(reinterpret_cast<WNDPROC>(s_originalProc), hwnd,
                               message, static_cast<WPARAM>(wParam),
                               static_cast<LPARAM>(lParam));
    };
    const bool inFrameState = !self->m_fullscreen && !IsZoomed(hwnd);
    if (!inFrameState)
        return chain();

    if (message == WM_WINDOWPOSCHANGING
        && self->m_window->flags().testFlag(Qt::FramelessWindowHint)) {
        auto *position = reinterpret_cast<WINDOWPOS *>(lParam);
        if (position) {
            RECT current{};
            const bool keepsSize = (position->flags & SWP_NOSIZE) != 0;
            if (!keepsSize || GetWindowRect(hwnd, &current)) {
                const int width = keepsSize ? current.right - current.left : position->cx;
                const int height = keepsSize ? current.bottom - current.top : position->cy;
                // A windowed 1080p surface can exactly match a secondary 1080p
                // display while being dragged from a larger monitor. Drivers
                // can classify that surface as fullscreen despite unchanged
                // Qt/Win32 state. Apply the same +1 physical pixel guard used by
                // explicit fullscreen before Windows commits the new bounds.
                // Compare every monitor: the majority monitor changes mid-drag.
                if (width > 0 && height > 0 && matchesDisplaySize(width, height)) {
                    position->cx = width;
                    position->cy = height + 1;
                    position->flags &= ~SWP_NOSIZE;
                }
            }
        }
        // Let Qt own geometry/DPI notification and normal move/resize behavior.
        return chain();
    }

    // WM_NCHITTEST / WM_NCLBUTTONDOWN pack signed screen coordinates in
    // lParam (negative on monitors left of / above the primary);
    // GET_X/Y_LPARAM sign-corrects them. WM_SETCURSOR's lParam is a
    // hit-code/message pair instead — take the live cursor position.
    LONG x = 0;
    LONG y = 0;
    if (message == WM_SETCURSOR) {
        POINT pt{};
        if (!GetCursorPos(&pt))
            return chain();
        x = pt.x;
        y = pt.y;
    } else {
        x = GET_X_LPARAM(static_cast<LPARAM>(lParam));
        y = GET_Y_LPARAM(static_cast<LPARAM>(lParam));
    }

    LRESULT hit = HTCLIENT;
    if (message == WM_NCHITTEST || message == WM_NCLBUTTONDOWN
        || message == WM_SETCURSOR) {
        RECT windowRect{};
        if (!GetWindowRect(hwnd, &windowRect))
            return chain();
        // Screen coordinates are physical pixels; scale the frame zone.
        const LONG border = static_cast<LONG>(
            qRound(8.0 * self->m_window->devicePixelRatio()));
        hit = borderHitAt(x, y, windowRect, border);
    }

    switch (message) {
    case WM_NCHITTEST:
        if (hit == HTCLIENT)
            break;
        return hit;
    case WM_NCLBUTTONDOWN:
        if (hit == HTCLIENT)
            break;
        // Run the system resize loop ourselves (HT* alone is cosmetic on
        // frameless windows). Returns into Qt's event loop like the old QML
        // strips did. startResize() is NativeWindowBase's — it just forwards
        // to QWindow::startSystemResize().
        self->startResize(static_cast<int>(edgesForHit(hit)));
        return 0;
    case WM_SETCURSOR:
        // wParam is the window, LOWORD(lParam) the hit-test area from the
        // message's own routing — recompute from the cursor position
        // instead so our zone wins.
        if (hit != HTCLIENT) {
            const HCURSOR cursor = cursorForHit(hit);
            if (cursor) {
                SetCursor(cursor);
                return TRUE;
            }
        }
        break;
    default:
        break;
    }

    return chain();
}

bool NativeWindowWin32::animationsEnabled() const
{
    // Windows "Animation effects" toggle (Win11: Settings > Accessibility >
    // Visual effects; older: "Show window contents while dragging" family
    // of client-area animation preferences).
    BOOL enabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0))
        return enabled != FALSE;
    return true;
}

void NativeWindowWin32::applyAlwaysOnTop()
{
    if (!m_window)
        return;
    const HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
    if (!hwnd)
        return;
    // QWindow::setFlag can rebuild a native window. Preserve the subclass,
    // resize border and DWM styling by changing only the native z-order.
    SetWindowPos(hwnd, m_alwaysOnTop || m_fullscreen ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void NativeWindowWin32::setFullscreenWindow(bool enabled,
                                             int restoreX, int restoreY,
                                             int restoreWidth, int restoreHeight)
{
    // Track fullscreen before any geometry work: while the 1px-extended
    // fullscreen geometry is up, WM_NCHITTEST must never claim a resize
    // zone (see nativeEventFilter).
    m_fullscreen = enabled;

    if (!m_window)
        return;

    const HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
    if (!hwnd)
        return;

    // A manually sized borderless window keeps mpv out of exclusive/game
    // fullscreen, but still needs native monitor geometry and z-order to
    // cover the taskbar. Rounded DWM corners would otherwise expose a few
    // pixels of the application underneath.
    DWM_WINDOW_CORNER_PREFERENCE corner = enabled
        ? DWMWCP_DONOTROUND
        : DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                          &corner, sizeof(corner));

    if (enabled) {
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (!GetMonitorInfoW(monitor, &monitorInfo))
            return;

        const RECT &area = monitorInfo.rcMonitor;
        // The swapchain must not match the display size exactly, or the
        // NVIDIA driver classifies the borderless window as a full-screen
        // surface and G-SYNC ("full screen mode only") power-cycles the
        // display on every toggle (CONTRACTS.md 2026-08-31 resolution
        // record). Two ways to break the exact match:
        //   - extend (below): cover the monitor fully and hang 1px off the
        //     bottom edge — seamless, no auxiliary window (user preference);
        //   - shrink: 1px gap at the top edge plus the black edge-cover
        //     overlay window (kept below as the fallback).
        // If the display ever blanks on toggle again, the driver started
        // classifying over-extended windows too; flip kExtendOnePixel.
        // Extend is user-confirmed flicker-free on this machine (RTX 3070,
        // G-SYNC "full screen mode only") 2026-09-02.
        constexpr bool kExtendOnePixel = true;
        if (kExtendOnePixel) {
            SetWindowPos(hwnd, HWND_TOPMOST,
                         area.left, area.top,
                         area.right - area.left,
                         (area.bottom - area.top) + 1,
                         SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
            if (m_edgeCover)
                ShowWindow(m_edgeCover, SW_HIDE);
        } else {
            SetWindowPos(hwnd, HWND_TOPMOST,
                         area.left, area.top + 1,
                         area.right - area.left, (area.bottom - area.top) - 1,
                         SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);

            // Hide the 1px seam: a plain topmost black strip over the
            // uncovered row, layered so it can be made click-through. No
            // swapchain, so the driver's classification of the main window
            // is untouched.
            if (ensureEdgeCoverClass()) {
                const int coverWidth = area.right - area.left;
                if (!m_edgeCover) {
                    m_edgeCover = CreateWindowExW(
                        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE
                            | WS_EX_LAYERED | WS_EX_TRANSPARENT,
                        kEdgeCoverClassName, L"", WS_POPUP,
                        area.left, area.top, coverWidth, 1,
                        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
                    if (m_edgeCover)
                        SetLayeredWindowAttributes(m_edgeCover, 0, 255, LWA_ALPHA);
                } else {
                    SetWindowPos(m_edgeCover, nullptr, area.left, area.top,
                                 coverWidth, 1, SWP_NOACTIVATE | SWP_NOOWNERZORDER);
                }
                if (m_edgeCover) {
                    ShowWindow(m_edgeCover, SW_SHOWNOACTIVATE);
                    // Deterministic z-order: the cover sits directly above
                    // the main topmost window.
                    SetWindowPos(hwnd, m_edgeCover, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
                                 | SWP_NOOWNERZORDER);
                }
            }
        }
    } else {
        if (m_edgeCover)
            ShowWindow(m_edgeCover, SW_HIDE);
        // Plain z-order drop: the frame is not actually changing on restore,
        // and forcing a frame recompute here (SWP_FRAMECHANGED) plus a
        // synchronous activation made some fullscreen exits stall 300-800 ms
        // in the middle of the QML click handler. The enter path keeps
        // SWP_FRAMECHANGED — there the window really does change shape.
        SetWindowPos(hwnd, m_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
        // QML window coordinates are device-independent; let Qt translate
        // them back to native pixels when restoring on a scaled display.
        m_window->setGeometry(restoreX, restoreY, restoreWidth, restoreHeight);
        // Activate after the geometry settles instead of inside the same
        // window-manager round trip.
        QTimer::singleShot(0, m_window, [window = m_window] {
            if (window)
                window->requestActivate();
        });
    }
}

QString NativeWindowWin32::openMediaFileDialog() const
{
    const QStringList paths = openMediaFilesDialog();
    return paths.isEmpty() ? QString() : paths.first();
}

QStringList NativeWindowWin32::openMediaFilesDialog() const
{
    UINT filterCount = 0;
    const COMDLG_FILTERSPEC *filters = mediaFilters(&filterCount);
    const HWND owner = m_window
        ? reinterpret_cast<HWND>(m_window->winId())
        : nullptr;
    QStringList paths = openFileDialog(owner, L"Open media", filters,
                                       filterCount, /*allowMultiple=*/true);

    // The shell hands results back in selection order, which is whatever the
    // user happened to click. Sort the way the playlist does (F16 sibling
    // enqueue) so picking ten episodes queues them 1,2,...,10 and not
    // 1,10,2 — same QCollator numeric mode, same case-insensitivity, same
    // "C" locale fallback (see PlayerController::siblingMediaFiles).
    if (paths.size() > 1) {
        QLocale sortLocale = QLocale::system();
        if (sortLocale.language() == QLocale::C)
            sortLocale = QLocale(QLocale::English);
        QCollator collator{sortLocale};
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        std::sort(paths.begin(), paths.end(),
                  [&collator](const QString &a, const QString &b) {
                      return collator.compare(a, b) < 0;
                  });
    }
    return paths;
}

QString NativeWindowWin32::openShaderFileDialog() const
{
    static const COMDLG_FILTERSPEC filters[] = {
        {L"Shaders", L"*.glsl;*.hook"},
        {L"All files", L"*.*"}
    };
    const HWND owner = m_window
        ? reinterpret_cast<HWND>(m_window->winId())
        : nullptr;
    const QStringList paths = openFileDialog(owner, L"Apply shader", filters,
                                             ARRAYSIZE(filters),
                                             /*allowMultiple=*/false);
    return paths.isEmpty() ? QString() : paths.first();
}

QString NativeWindowWin32::openSubtitleFileDialog() const
{
    static const COMDLG_FILTERSPEC filters[] = {
        {L"Subtitle files", L"*.srt;*.ass;*.ssa;*.vtt;*.sub;*.idx;*.sup;*.smi;*.txt"},
        {L"All files", L"*.*"}
    };
    const HWND owner = m_window
        ? reinterpret_cast<HWND>(m_window->winId())
        : nullptr;
    const QStringList paths = openFileDialog(owner, L"Load subtitles", filters,
                                             ARRAYSIZE(filters),
                                             /*allowMultiple=*/false);
    return paths.isEmpty() ? QString() : paths.first();
}

QString NativeWindowWin32::openFolderDialog() const
{
    const HRESULT initResult = CoInitializeEx(
        nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initResult) && initResult != RPC_E_CHANGED_MODE)
        return {};

    const bool uninitialize = SUCCEEDED(initResult);
    IFileOpenDialog *dialog = nullptr;
    const HRESULT createResult = CoCreateInstance(
        CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog));
    if (FAILED(createResult)) {
        if (uninitialize)
            CoUninitialize();
        return {};
    }

    FILEOPENDIALOGOPTIONS options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        options |= FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST
                   | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR | FOS_PICKFOLDERS;
        dialog->SetOptions(options);
    }
    dialog->SetTitle(L"Select folder");

    const HWND owner = m_window
        ? reinterpret_cast<HWND>(m_window->winId())
        : nullptr;
    QString path;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem *item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            path = pathOfShellItem(item);
            item->Release();
        }
    }

    dialog->Release();
    if (uninitialize)
        CoUninitialize();
    return path;
}


void NativeWindowWin32::setPlaybackActive(bool playing, bool hasVideo)
{
    // ES_CONTINUOUS makes the request stick until the next call from this
    // thread; always called from the GUI thread. Audio-only playback keeps
    // the machine awake but lets the display turn off.
    EXECUTION_STATE flags = ES_CONTINUOUS;
    if (playing) {
        flags |= ES_SYSTEM_REQUIRED;
        if (hasVideo)
            flags |= ES_DISPLAY_REQUIRED;
    }
    if (SetThreadExecutionState(flags) == 0)
        qWarning("native: SetThreadExecutionState failed (playing=%d)", playing);
}

void NativeWindowWin32::allowForegroundHandoff()
{
    // Windows only lets the foreground process hand focus to another one.
    AllowSetForegroundWindow(ASFW_ANY);
}

void NativeWindowWin32::raiseToFront()
{
    NativeWindowBase::raiseToFront();
    if (!m_window)
        return;
    const HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
    if (!hwnd)
        return;
    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
}

// A54. Explorer highlights the file itself; a folder that has gone missing
// falls back to the base class, which opens whatever parent still exists.
void NativeWindowWin32::revealInFileManager(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        NativeWindowBase::revealInFileManager(path);
        return;
    }
    // "/select," and the path travel as separate arguments: QProcess quotes
    // the path on its own, which is the one form Explorer parses reliably
    // for names with spaces.
    if (!QProcess::startDetached(QStringLiteral("explorer.exe"),
                                 {QStringLiteral("/select,"),
                                  QDir::toNativeSeparators(info.absoluteFilePath())}))
        NativeWindowBase::revealInFileManager(path);
}
