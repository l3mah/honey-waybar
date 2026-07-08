# honey-waybar

Waybar modules for [honey](../honey-wm), fed by its `subscribe` status stream.

honey already speaks the standard bar protocols — **`ext/workspaces`** (clickable
workspaces) and **`wlr/taskbar`** (windows) work in stock waybar with no extra
code. Use those if they're enough. honey-waybar exists for what the standard
modules *can't* express:

- **active / occupied / empty** per-workspace styling — `ext-workspace-v1` only
  has an `active` state, no "has-windows" signal. honey's stream carries
  `occupied[]`, so this plugin styles each workspace by all three states.
- a **focused-window-title** label — there's no generic `wlr/window` module.
- a **night-light control** for honey's integrated gamma (`honeyctl gamma`) — a
  day/night toggle that stays in sync however gamma is driven.

## The pieces

Three CFFI plugins — the primary modules, each a native GTK widget connecting to
the status socket directly:

| Build (`make cffi`) | Provides |
|---|---|
| `honey-workspaces.so` | one clickable, individually-styled button per workspace (active/occupied/empty) |
| `honey-window.so` | the focused window's title (app-id fallback + tooltip), `active`/`empty` class |
| `honey-gamma.so` | day/night night-light toggle over `honeyctl gamma`, live-synced to honey |

Plus a CLI adapter (`make`) — `honey-waybar`, for `custom/` modules — offering
text alternatives the plugins don't: a single-label `workspaces` and a
per-number `workspace N` (and a `window` mode, superseded by the plugin).

All read `$XDG_RUNTIME_DIR/honey-$WAYLAND_DISPLAY.sock`: the CFFI plugins connect
directly and auto-reconnect if honey restarts; the CLI adapter reads
`honeyctl subscribe` on stdin.

The gamma plugin is a **live reflector**. honey owns the current gamma and
broadcasts every change, so the module mirrors the real state whether it was
changed here, by a hotkey, or by a direct `honeyctl gamma`. It's asymmetric by
design: temperature is the mode's identity (day = neutral, night = warm, from
config, restored on every toggle); brightness is the ridable trim that scroll
adjusts and that's retained per mode until the bar restarts. The brightness
clamp lives in honey (`honeyctl gamma min|max <pct>`), universal to scroll and
hotkeys.

## Build & install

    make                 # honey-waybar (CLI adapter)
    make cffi            # honey-workspaces.so + honey-window.so + honey-gamma.so
    sudo make install    # CLI adapter + example config/style
    sudo make install-cffi

## Configure

See [`examples/waybar/config_honey.jsonc`](examples/waybar/config_honey.jsonc) and
[`examples/waybar/style.css`](examples/waybar/style.css). Minimal:

```jsonc
"cffi/honey-workspaces": {
    "module_path": "/usr/local/lib/honey-waybar/honey-workspaces.so",
    "output": "DP-1",   // omit to follow the focused output
    "count": 9
},
"cffi/honey-window": {
    "module_path": "/usr/local/lib/honey-waybar/honey-window.so",
    "max-length": 80   // omit "output" to follow the focused window
},
"cffi/honey-gamma": {
    "module_path": "/usr/local/lib/honey-waybar/honey-gamma.so",
    "temperature-day": 6500, "temperature-night": 4000,
    "brightness-day": 100, "brightness-night": 60, "step": 5,
    "icon-day": "☀", "icon-night": "☾",
    "format": "{icon} {temperature}K {brightness}%"
}
```

Workspace buttons are plain `button` children of `#honey-workspaces`, each
carrying an `active` / `occupied` / `empty` class — style them all at once with
`#honey-workspaces button.active` (etc.), no per-workspace selectors. The window
module is `#honey-window` (class `active`/`empty`); gamma is `#honey-gamma` with class `day`/`night`
(and `override` when a manual temperature is in effect). `format` tokens
`{icon} {temperature} {brightness}` — omit any to hide it. Run `honeyctl outputs`
for connector names.