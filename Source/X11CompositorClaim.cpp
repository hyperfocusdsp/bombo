// Linux-only helper isolated in its own TU so X11 macros (KeyPress, Status,
// Bool, None, …) never leak into the rest of the codebase.
//
// Hyprland (a Wayland compositor) does not own the legacy X11 _NET_WM_CM_S0
// selection that JUCE's canUseSemiTransparentWindows() checks. Without an
// owner, JUCE silently disables ARGB visuals — even with JUCE_USE_XRENDER=1
// and setOpaque(false). We claim the selection ourselves with a hidden 1×1
// window so JUCE's compositor check passes. Hyprland *is* compositing the
// X content under XWayland, so this isn't a lie — it just bridges the X11
// ICCCM gap. The Display + Window are intentionally retained for process
// lifetime so the selection ownership doesn't drop.

#include <juce_core/juce_core.h>  // pulls JUCE_LINUX

#if JUCE_LINUX

#include <X11/Xlib.h>
#include <X11/Xatom.h>

namespace bombo {

bool claimCompositorSelectionOnce()
{
    Display* d = XOpenDisplay(nullptr);
    if (d == nullptr) return false;

    Atom cm = XInternAtom(d, "_NET_WM_CM_S0", False);
    if (XGetSelectionOwner(d, cm) != 0)
    {
        XCloseDisplay(d);
        return true; // already owned — nothing to do
    }

    Window owner = XCreateSimpleWindow(d, DefaultRootWindow(d),
                                       -100, -100, 1, 1, 0, 0, 0);
    XSetSelectionOwner(d, cm, owner, CurrentTime);
    XFlush(d);
    // d + owner intentionally kept open for process lifetime
    return true;
}

} // namespace bombo

#else

namespace bombo {
bool claimCompositorSelectionOnce() { return false; }
} // namespace bombo

#endif
