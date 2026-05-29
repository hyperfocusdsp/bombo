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
#include <cstdio>

namespace bombo {

namespace {
    // Xlib's *default* error handler calls exit() on any unhandled X11
    // protocol error. In Bitwig's out-of-process host this is fatal: closing
    // and reopening the editor swaps the native window, and the OpenGLContext's
    // GLX teardown then touches a now-destroyed drawable → BadDrawable → the
    // default handler exit()s the whole plugin host. JUCE installs its own
    // non-fatal handler while a window is alive but the default one is active
    // during the close→reopen teardown gap. Install a benign handler that logs
    // and continues so an X error can never kill the host.
    int nonFatalXErrorHandler(Display* d, XErrorEvent* e)
    {
        char buf[256] = {};
        if (d != nullptr && e != nullptr)
            XGetErrorText(d, e->error_code, buf, sizeof(buf) - 1);
        std::fprintf(stderr,
                     "[Bombo] non-fatal X11 error swallowed: code=%d (%s) "
                     "request=%d minor=%d\n",
                     e ? e->error_code   : -1, buf,
                     e ? e->request_code : -1,
                     e ? e->minor_code   : -1);
        return 0; // never chain to the default (exit()) handler
    }
}

// Install our non-fatal handler as the process-global X error handler.
// Called from the editor ctor before any JUCE window/peer is created, so JUCE's
// XWindowSystem saves OURS as the "previous" handler and restores a non-fatal
// one on teardown. Idempotent — safe to call on every editor open.
void installNonFatalXErrorHandler()
{
    XSetErrorHandler(nonFatalXErrorHandler);
}

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
void installNonFatalXErrorHandler() {}
} // namespace bombo

#endif
