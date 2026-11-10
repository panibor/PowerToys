#pragma once

#include <functional>
#include <optional>

#include <FancyZonesLib/LayoutConfigurator.h> // ZonesMap

// Double-clicking a window's title bar fills the window's zone instead of maximizing it to the whole
// screen. Windows maximizes the window itself, so FancyZones watches for a double click that turns a
// normal window into a maximized one and then moves that window into its zone instead.

// Recognizes the second button press of a double click from low-level mouse events.
class DoubleClickDetector
{
public:
    // Returns true when this press completes a double click on the same window: within maxDelayMillis
    // of the previous press and within maxDx/maxDy pixels of it. A third press starts a new double click.
    bool OnButtonDown(HWND window, POINT pt, unsigned long time, unsigned long maxDelayMillis, int maxDx, int maxDy) noexcept;
    void Reset() noexcept;

private:
    HWND m_window{};
    POINT m_pt{};
    unsigned long m_time{};
    bool m_hasFirstClick = false;
};

// Swallows the button release that belongs to a swallowed press, so the window never sees half a click.
class SwallowedClick
{
public:
    void OnButtonDown(bool swallowed) noexcept { m_swallowNextUp = swallowed; }
    // Returns true when this release must be swallowed too
    bool OnButtonUp() noexcept
    {
        const bool swallow = m_swallowNextUp;
        m_swallowNextUp = false;
        return swallow;
    }

private:
    bool m_swallowNextUp = false;
};

namespace TitleBarDoubleClick
{
    // Whether a WM_NCHITTEST result is a place where a double click maximizes the window
    constexpr bool IsCaptionHit(LRESULT hitTest) noexcept { return hitTest == HTCAPTION; }

    // The zone that holds the largest part of the rect, or an empty set when the rect overlaps no zone.
    // The rect and the zones must use the same coordinates.
    ZoneIndexSet ZoneWithLargestOverlap(const ZonesMap& zones, const RECT& rect) noexcept;

    // Screen rect from the FancyZones restore-size and restore-origin properties, as saved when the
    // window was first snapped. These are stored DPI-independent, so they're scaled for the monitor.
    std::optional<RECT> RetrieveRestoreRect(HWND window) noexcept;

    // Overwrites the FancyZones restore properties with the given screen rect, so that dragging the
    // window out of its zone later brings back this size and not the maximized one.
    void SaveRestoreRect(HWND window, const RECT& rect) noexcept;
    void RemoveRestoreRect(HWND window) noexcept;
}

// Low-level mouse hook that reports title bar double clicks on windows that are not maximized.
// When the second click is on the title bar, it is swallowed so that Windows does not maximize the
// window first, and the callback gets swallowed=true. When the hook can't tell, the click goes through
// and the callback gets swallowed=false. It's only installed while the setting is on.
class TitleBarDoubleClickHook
{
public:
    explicit TitleBarDoubleClickHook(std::function<void(HWND window, bool swallowed)> doubleClickCallback);
    void enable();
    void disable();

private:
    static HHOOK hHook;
    static std::function<void(HWND, bool)> callback;
    static DoubleClickDetector detector;
    static SwallowedClick swallowedClick;
    static bool IsOnTitleBar(HWND window, POINT pt) noexcept;
    static LRESULT CALLBACK MouseProc(int, WPARAM, LPARAM);
};
