<div align="center">

<img src="icons/worldclock-256.png" width="120" alt="World Clock Gadget">

# World Clock Gadget

**A lightweight Windows desktop gadget that shows multiple time zones at a glance**

Inspired by the Windows 7 desktop gadgets: a translucent panel listing the time in
cities around the world, with automatic and always-correct DST handling.

[日本語](README.md) · [Install](#install) · [Usage](#usage) · [Settings](#settings) · [Build](#building-from-source)

<img src="docs/digital-horizontal.png" width="620" alt="Digital, horizontal layout">

</div>

---

## Features

- 🌍 **Multiple cities at once** — backed by IANA time zones, so **DST is always correct** (recomputed every tick)
- 🕘 **Digital or analog** — analog faces go **light for AM, dark for PM**, so day/night reads instantly
- ↔️ **Vertical or horizontal** layout
- 📅 **Fully customizable date format** — from `Tue, Aug 4` to `2026-08-04`
- 🪶 **Tiny resident footprint** — the always-on process is C++/Win32/Direct2D (400 KB exe, no .NET)
- 🎨 **Translucent and frameless** — drag to move, adjustable opacity
- 🪟 **Sits behind other windows** by default, and stays out of the taskbar and Alt-Tab
- ⚙️ **Windows 11-style settings app** with Fluent design and OK / Cancel / Apply
- 🚀 **Launch at Windows startup** (no admin rights required)

<div align="center">
<img src="docs/digital-vertical.png" height="300" alt="Digital, vertical">
&nbsp;&nbsp;
<img src="docs/analog-horizontal.png" height="300" alt="Analog, horizontal">
</div>

---

## Install

### Installer (recommended)

1. **[Download `WorldClockGadget-Setup.exe` from Releases](../../releases/latest)**
2. Double-click to run

That's it. Note that:

- **No admin rights needed** — it installs per-user into `%LOCALAPPDATA%\Programs\WorldClockGadget`
- **No .NET install needed** — the runtime is bundled with the settings app
- The installer optionally creates a desktop icon and enables launch-at-startup

> [!NOTE]
> The app is unsigned (a personal project), so **Microsoft Defender SmartScreen** may warn you
> on first run. Choose "More info" → "Run anyway" to continue.

### Requirements

| Item | Requirement |
|---|---|
| OS | Windows 10 (1809+) / **Windows 11 recommended** |
| Architecture | x64 |
| Runtime | **None** — the gadget is dependency-free and .NET ships with the settings app |

> On Windows 11 22H2+ the settings app uses the Mica backdrop. On Windows 10 it falls back to a
> plain background; all functionality is identical.

### Uninstall

Use any of these:

1. **Settings > Apps > Installed apps** → "World Clock Gadget"
2. **Start menu** → "World Clock Gadget をアンインストールする"
3. `unins000.exe` in the install folder

The uninstaller closes the running app automatically. At the end it asks whether to also delete
your settings — answer "No" to keep your cities and preferences for a future reinstall.

---

## Usage

| Action | Result |
|---|---|
| **Drag** | Move the gadget (position is saved and restored on next launch) |
| **Right-click** | Menu: Settings / Always on top / Lock position / Exit |

By default the gadget **can be covered by other windows**, matching the original Windows 7 gadget
behavior. Enable "Always on top" from the right-click menu or the settings app if you want it
permanently visible.

It never appears in the taskbar or Alt-Tab. To close it, right-click → Exit.

---

## Settings

**Right-click the gadget → "設定…" (Settings)** to open the settings app.

<div align="center">
<img src="docs/settings.png" width="460" alt="Settings app">
</div>

### How the buttons work

Settings are written to disk, and the gadget watches that file and reloads instantly. So
"apply", "save", and "take effect" all happen at the same moment.

| Button | Behavior |
|---|---|
| **OK** | Save, apply to the gadget, and close |
| **Cancel** | Close without saving (already-applied changes stay applied) |
| **Apply** | Save and apply, but keep the window open |

This makes **Apply** double as a preview: nudge the opacity or size, hit Apply, look at the real
gadget, and fine-tune.

### What you can configure

- **Cities** — add, remove, reorder. Pick from a list of major cities, or type any IANA name (e.g. `Asia/Tokyo`)
- **Display mode** — digital or analog
- **Layout** — vertical or horizontal
- **12/24-hour**, **date and weekday**, **seconds**
- **Date format** — presets plus free-form input (see below)
- **Size** — small / medium / large
- **Opacity** — 10–100%
- **Always on top**, **lock position**, **launch at Windows startup**

### Date format

Pick a preset or write your own; the settings app previews it live.

| Token | Meaning | Example |
|---|---|---|
| `yyyy` / `yy` | Year (4- or 2-digit) | `2026` / `26` |
| `MMMM` / `MMM` | Month name (full / short) | `August` / `Aug` |
| `MM` / `M` | Month number (padded / plain) | `08` / `8` |
| `dddd` / `ddd` | Weekday (full / short) | `Tuesday` / `Tue` |
| `dd` / `d` | Day (padded / plain) | `04` / `4` |
| `aaaa` / `aaa` | Japanese weekday | `火曜日` / `火` |

Every other character passes through literally.

```
"ddd, MMM d"          →  Tue, Aug 4
"yyyy-MM-dd"          →  2026-08-04
"yyyy年M月d日 (aaa)"  →  2026年8月4日 (火)
```

### Config file location

Everything lives in `%APPDATA%\WorldClockGadget\`. Editing it by hand works too — changes apply
immediately.

```jsonc
// config.json — owned by the settings app
{
  "cities": [
    { "label": "Tokyo",    "tz": "Asia/Tokyo" },
    { "label": "New York", "tz": "America/New_York" }
  ],
  "displayMode": "digital",   // "digital" | "analog"
  "layout": "vertical",       // "vertical" | "horizontal"
  "hourFormat": 24,           // 12 | 24
  "showDate": true,
  "dateFormat": "ddd, MMM d",
  "showSeconds": false,
  "size": "medium",           // "small" | "medium" | "large"
  "opacity": 85,              // 0-100
  "alwaysOnTop": false,
  "lockPosition": false,
  "launchAtStartup": false,
  "theme": "dark"
}
```

`state.json` holds the window position and is owned by the gadget itself.

---

## Building from source

### Prerequisites

- **Visual Studio 2022 / 2026** with the C++ desktop workload (MSVC v14.3x+, Windows SDK)
- **[.NET 8 SDK](https://dotnet.microsoft.com/download/dotnet/8.0)** — `winget install Microsoft.DotNet.SDK.8`
- **[Inno Setup 6](https://jrsoftware.org/isdl.php)** (only to build the installer) — `winget install JRSoftware.InnoSetup`

### Build

```bat
build_all.cmd
```

Puts both executables side by side in `dist\` — that layout is what lets the gadget's
"Settings…" menu launch the settings app.

```bat
make_installer.cmd
```

Produces `dist_installer\WorldClockGadget-Setup.exe` (single file, ~50 MB).

Individual targets:

```bat
build_clock.cmd                        REM C++ gadget only
dotnet build src\settings -c Release   REM settings app only
```

---

## Architecture

The project is **deliberately split into two processes**. This is its central design decision.

| Component | Stack | Role | Resident |
|---|---|---|---|
| **WorldClockGadget** | C++20 / Win32 / Direct2D / DirectWrite | The only always-running process; draws the clocks | Yes |
| **WorldClockSettings** | C# / .NET 8 / WPF + [WPF-UI](https://github.com/lepoco/wpfui) | Edits settings on demand, exits when closed | No |

### Why split it

"Minimal always-on resource usage" and "a modern Windows 11 Settings-grade look" cannot be
satisfied by one framework: the first demands raw C++/Win32, the second demands WinUI/Fluent.

Since only the gadget is resident, keeping **the gadget lean in C++ and isolating the modern UI in
a separate, occasionally-opened process** satisfies both. The settings app costs tens of megabytes
only while it is open — never as a standing cost.

### How the processes talk

There is no fast IPC. They communicate **through files**:

```
Settings app ──writes──> config.json ──file watch──> Gadget (reloads, applies instantly)
Gadget ──writes──> state.json (window position)
```

The files are **split by owner** so the two writers never conflict (`config.json` belongs to the
settings app, `state.json` to the gadget). Writes are **atomic** via a temp file plus rename, and
the gadget watches the directory with `ReadDirectoryChangesW`.

### Implementation notes

- **Translucency, rounded corners, clean edges** — Direct2D renders into a **premultiplied-BGRA WIC
  bitmap**, which is then presented with `UpdateLayeredWindow` for true per-pixel alpha.
  (Going through `ID2D1DCRenderTarget` + a GDI DC **drops the alpha channel**, which renders the
  window fully transparent — hence the WIC hop.)
- **DST correctness** — no cached UTC offsets. Cities are stored as IANA identifiers and
  recomputed from UTC **every tick** with C++20 `std::chrono::zoned_time`, so DST transitions are
  handled automatically.
- **Stable layout** — text is measured using the widest digit glyph, so the panel does not jitter
  as the seconds tick over.
- **Per-Monitor DPI v2** in both processes.
- **Update cadence** — minute resolution by default, per-second only when seconds are shown.
- **Statically linked CRT** — a dynamic CRT would require the VC++ redistributable (which **needs
  admin rights**) on target machines, so the gadget links `/MT` and ships dependency-free.

### Project layout

```
src/clock/                  C++ gadget
  main.cpp                    entry point (wWinMain only)
  GadgetWindow.{hpp,cpp}      window, menu, dragging, timer, file watching
  Renderer.{hpp,cpp}          Direct2D rendering (digital / analog)
  TimeEngine.{hpp,cpp}        IANA time zone / DST computation
  Config.{hpp,cpp}            config.json / state.json I/O
  Startup.{hpp,cpp}           launch-at-startup registration
  json.hpp                    minimal dependency-free JSON parser
src/settings/               C# WPF settings app
installer/                  Inno Setup script
icons/                      application icons
```

---

## Roadmap

Possible future additions:

- [ ] Custom colors and themes (currently a single dark translucent theme)
- [ ] Multiple gadget instances (currently one panel with many cities)
- [ ] "Revert" in the settings app (restore the snapshot taken when it opened)

## License

[MIT License](LICENSE) © 2026 Kaito Ito

Icon designed with [Claude](https://claude.ai) Design.
