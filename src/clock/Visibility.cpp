#include "Visibility.hpp"

bool IsVisible(const Visibility& v) {
    return !v.displayOff && !v.locked && !v.disconnected;
}

VisibilityTransition ApplyVisibilityEvent(Visibility& v, VisibilityEvent e) {
    const bool before = IsVisible(v);
    switch (e) {
        case VisibilityEvent::DisplayOff:        v.displayOff   = true;  break;
        case VisibilityEvent::DisplayOn:         v.displayOff   = false; break;
        case VisibilityEvent::DisplayDimmed:     v.displayOff   = false; break;
        case VisibilityEvent::SessionLock:       v.locked       = true;  break;
        case VisibilityEvent::SessionUnlock:     v.locked       = false; break;
        case VisibilityEvent::SessionDisconnect: v.disconnected = true;  break;
        case VisibilityEvent::SessionConnect:    v.disconnected = false; break;
    }
    const bool after = IsVisible(v);
    if (before == after) return VisibilityTransition::None;
    return after ? VisibilityTransition::Shown : VisibilityTransition::Hidden;
}

std::optional<VisibilityEvent> DisplayStatusEvent(DWORD status) {
    switch (status) {
        case 0: return VisibilityEvent::DisplayOff;
        case 1: return VisibilityEvent::DisplayOn;
        case 2: return VisibilityEvent::DisplayDimmed;
    }
    return std::nullopt;
}

std::optional<VisibilityEvent> SessionChangeEvent(WPARAM code) {
    switch (code) {
        case WTS_SESSION_LOCK:       return VisibilityEvent::SessionLock;
        case WTS_SESSION_UNLOCK:     return VisibilityEvent::SessionUnlock;
        case WTS_CONSOLE_DISCONNECT:
        case WTS_REMOTE_DISCONNECT:  return VisibilityEvent::SessionDisconnect;
        case WTS_CONSOLE_CONNECT:
        case WTS_REMOTE_CONNECT:     return VisibilityEvent::SessionConnect;
    }
    return std::nullopt;
}
