#include "pch.h"
#include "TitleBarDoubleClick.h"

#include <array>
#include <cstdlib>

#include <common/debug_control.h>
#include <common/Display/dpi_aware.h>
#include <FancyZonesLib/FancyZonesWindowProperties.h>

bool DoubleClickDetector::OnButtonDown(HWND window, POINT pt, unsigned long time, unsigned long maxDelayMillis, int maxDx, int maxDy) noexcept
{
    // Unsigned subtraction keeps working when the tick count wraps around
    const bool isSecondClick = m_hasFirstClick &&
                               window == m_window &&
                               time - m_time <= maxDelayMillis &&
                               std::abs(pt.x - m_pt.x) <= maxDx &&
                               std::abs(pt.y - m_pt.y) <= maxDy;

    if (isSecondClick)
    {
        Reset();
        return true;
    }

    m_window = window;
    m_pt = pt;
    m_time = time;
    m_hasFirstClick = true;
    return false;
}

void DoubleClickDetector::Reset() noexcept
{
    m_hasFirstClick = false;
    m_window = nullptr;
}

ZoneIndexSet TitleBarDoubleClick::ZoneWithLargestOverlap(const ZonesMap& zones, const RECT& rect) noexcept
{
    long long bestArea = 0;
    ZoneIndexSet best;
    for (const auto& [index, zone] : zones)
    {
        const RECT zoneRect = zone.GetZoneRect();
        const long long width = static_cast<long long>((std::min)(rect.right, zoneRect.right)) - (std::max)(rect.left, zoneRect.left);
        const long long height = static_cast<long long>((std::min)(rect.bottom, zoneRect.bottom)) - (std::max)(rect.top, zoneRect.top);
        if (width <= 0 || height <= 0)
        {
            continue;
        }

        // Ties keep the lower zone index, as the map is ordered
        if (width * height > bestArea)
        {
            bestArea = width * height;
            best = { index };
        }
    }

    return best;
}

std::optional<RECT> TitleBarDoubleClick::RetrieveRestoreRect(HWND window) noexcept
{
    const auto sizeData = GetPropW(window, ZonedWindowProperties::PropertyRestoreSizeID);
    const auto originData = GetPropW(window, ZonedWindowProperties::PropertyRestoreOriginID);
    if (!sizeData || !originData)
    {
        return std::nullopt;
    }

    std::array<int, 2> size{};
    std::array<int, 2> origin{};
    memcpy(size.data(), &sizeData, sizeof size);
    memcpy(origin.data(), &originData, sizeof origin);

    float width = static_cast<float>(size[0]);
    float height = static_cast<float>(size[1]);
    float x = static_cast<float>(origin[0]);
    float y = static_cast<float>(origin[1]);
    const auto monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
    DPIAware::Convert(monitor, width, height);
    DPIAware::Convert(monitor, x, y);

    return RECT{
        .left = static_cast<long>(x),
        .top = static_cast<long>(y),
        .right = static_cast<long>(x + width),
        .bottom = static_cast<long>(y + height),
    };
}

void TitleBarDoubleClick::SaveRestoreRect(HWND window, const RECT& rect) noexcept
{
    float width = static_cast<float>(rect.right - rect.left);
    float height = static_cast<float>(rect.bottom - rect.top);
    float x = static_cast<float>(rect.left);
    float y = static_cast<float>(rect.top);
    const auto monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    DPIAware::InverseConvert(monitor, width, height);
    DPIAware::InverseConvert(monitor, x, y);

    std::array<int, 2> size = { static_cast<int>(width), static_cast<int>(height) };
    std::array<int, 2> origin = { static_cast<int>(x), static_cast<int>(y) };
    HANDLE rawData;
    memcpy(&rawData, size.data(), sizeof rawData);
    SetPropW(window, ZonedWindowProperties::PropertyRestoreSizeID, rawData);
    memcpy(&rawData, origin.data(), sizeof rawData);
    SetPropW(window, ZonedWindowProperties::PropertyRestoreOriginID, rawData);
}

void TitleBarDoubleClick::RemoveRestoreRect(HWND window) noexcept
{
    RemovePropW(window, ZonedWindowProperties::PropertyRestoreSizeID);
    RemovePropW(window, ZonedWindowProperties::PropertyRestoreOriginID);
}

HHOOK TitleBarDoubleClickHook::hHook = {};
std::function<void(HWND, bool)> TitleBarDoubleClickHook::callback = {};
DoubleClickDetector TitleBarDoubleClickHook::detector = {};
SwallowedClick TitleBarDoubleClickHook::swallowedClick = {};

TitleBarDoubleClickHook::TitleBarDoubleClickHook(std::function<void(HWND, bool)> doubleClickCallback)
{
    callback = std::move(doubleClickCallback);
}

void TitleBarDoubleClickHook::enable()
{
#if defined(DISABLE_LOWLEVEL_HOOKS_WHEN_DEBUGGED)
    if (IsDebuggerPresent())
    {
        return;
    }
#endif
    if (!hHook)
    {
        detector.Reset();
        swallowedClick.OnButtonDown(false);
        hHook = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, GetModuleHandle(NULL), 0);
    }
}

void TitleBarDoubleClickHook::disable()
{
    if (hHook)
    {
        UnhookWindowsHookEx(hHook);
        hHook = NULL;
    }
}

bool TitleBarDoubleClickHook::IsOnTitleBar(HWND window, POINT pt) noexcept
{
    // Windows maximizes on a double click only when the window has a maximize box
    if ((GetWindowLong(window, GWL_STYLE) & WS_MAXIMIZEBOX) == 0)
    {
        return false;
    }

    // Ask the window under the cursor what is there, the way Windows does: a child that answers
    // HTTRANSPARENT passes the question on to its parent. The timeout keeps a hung window from
    // stalling the mouse, it then counts as not a title bar and the click goes through.
    constexpr UINT HitTestTimeoutMillis = 50;
    constexpr int MaxDepth = 4;
    HWND target = WindowFromPoint(pt);
    for (int depth = 0; target && depth < MaxDepth; ++depth)
    {
        // Hit test points are in the target window's own coordinates, which differ from the physical
        // ones the hook gets for windows that are not per-monitor DPI aware
        POINT logical = pt;
        PhysicalToLogicalPointForPerMonitorDPI(target, &logical);

        DWORD_PTR hitTest = HTNOWHERE;
        if (!SendMessageTimeoutW(target, WM_NCHITTEST, 0, MAKELPARAM(logical.x, logical.y), SMTO_ABORTIFHUNG | SMTO_BLOCK, HitTestTimeoutMillis, &hitTest))
        {
            return false;
        }

        if (static_cast<LRESULT>(hitTest) != HTTRANSPARENT || target == window)
        {
            return TitleBarDoubleClick::IsCaptionHit(static_cast<LRESULT>(hitTest));
        }

        target = GetAncestor(target, GA_PARENT);
    }

    return false;
}

LRESULT CALLBACK TitleBarDoubleClickHook::MouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    // Runs for every mouse event in the system, so it only does cheap checks until a double click
    // happens, and leaves the zoning to the main thread
    if (nCode == HC_ACTION && wParam == WM_LBUTTONUP && swallowedClick.OnButtonUp())
    {
        return 1;
    }

    if (nCode == HC_ACTION && wParam == WM_LBUTTONDOWN)
    {
        swallowedClick.OnButtonDown(false);
        const auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        const HWND window = GetAncestor(WindowFromPoint(info->pt), GA_ROOT);
        if (window && detector.OnButtonDown(window, info->pt, info->time, GetDoubleClickTime(), GetSystemMetrics(SM_CXDOUBLECLK) / 2, GetSystemMetrics(SM_CYDOUBLECLK) / 2))
        {
            // A double click on an already maximized title bar restores the window, leave that alone
            if (!IsZoomed(window) && callback)
            {
                const bool swallow = IsOnTitleBar(window, info->pt);
                callback(window, swallow);
                if (swallow)
                {
                    swallowedClick.OnButtonDown(true);
                    return 1;
                }
            }
        }
    }

    return CallNextHookEx(hHook, nCode, wParam, lParam);
}
