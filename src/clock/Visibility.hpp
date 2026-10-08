// Whether anyone can see the gadget right now.
//
// With the display off, the session locked, or the session disconnected (a
// remote desktop client went away), every frame drawn is a frame nobody sees.
// The gadget pauses its tick while hidden and redraws the moment it is shown
// again, so the first visible frame is never stale.
//
// The three causes are tracked separately because they overlap: the display
// can go off while the session is locked, and turning it back on must not
// resume rendering behind the lock screen.
//
// The bookkeeping is pure so it can be unit tested without a session; the
// gadget feeds it WM_POWERBROADCAST / WM_WTSSESSION_CHANGE notifications.
#pragma once
#include <windows.h>
#include <optional>

struct Visibility {
    bool displayOff   = false;
    bool locked       = false;
    bool disconnected = false;
};

enum class VisibilityEvent {
    DisplayOff,
    DisplayOn,
    DisplayDimmed,       // still visible, just darker
    SessionLock,
    SessionUnlock,
    SessionDisconnect,
    SessionConnect,
};

enum class VisibilityTransition {
    None,     // still visible, or still hidden
    Hidden,   // was visible, now nobody can see it: stop the tick
    Shown,    // was hidden, now visible: redraw, then re-arm the tick
};

bool IsVisible(const Visibility& v);

// Folds `e` into `v` and reports whether visibility flipped. A repeated
// notification, or one that changes a cause while another still hides the
// window, is None.
VisibilityTransition ApplyVisibilityEvent(Visibility& v, VisibilityEvent e);

// The event for a GUID_SESSION_DISPLAY_STATUS value: 0 off, 1 on, 2 dimmed.
// Anything else is nullopt -- an unknown state must not stop the clock.
std::optional<VisibilityEvent> DisplayStatusEvent(DWORD status);

// The event for a WM_WTSSESSION_CHANGE code, or nullopt for codes that do not
// change visibility (logon, logoff, remote control, ...).
std::optional<VisibilityEvent> SessionChangeEvent(WPARAM code);
