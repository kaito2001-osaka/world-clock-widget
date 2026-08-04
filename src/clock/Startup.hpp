// Per-user autostart (HKCU\...\Run) management for the gadget.
#pragma once

// Reconcile the autostart entry with the desired state. When enabling, the
// running exe's own path is registered (a moved/renamed build re-registers
// itself on next launch). No-op safe when the entry is already absent.
void ApplyStartupRegistry(bool enable);
