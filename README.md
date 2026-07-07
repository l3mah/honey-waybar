# w3ld-waybar

Waybar modules for [w3ld](../w3ld-wm), fed by its `subscribe` status stream.

w3ld already speaks the standard bar protocols — **`ext/workspaces`** (clickable
workspaces) and **`wlr/taskbar`** (windows) work in stock waybar with no extra
code. Use those if they're enough. w3ld-waybar exists for what the standard
modules *can't* express:

- **active / occupied / empty** per-workspace styling — `ext-workspace-v1` only
  has an `active` state, no "has-windows" signal. w3ld's stream carries
  `occupied[]`, so this plugin styles each workspace by all three states.
- a **focused-window-title** label — there's no generic `wlr/window` module.
- a **night-light control** for w3ld's integrated gamma (`w3ldctl gamma`) — a
  day/night toggle that stays in sync however gamma is driven.

## Three pieces

| Build | Kind | Provides |
|---|---|---|
| `w3ld-workspaces.so` (`make cffi`) | waybar **CFFI plugin** | one clickable, individually-styled button per workspace (active/occupied/empty) |
| `w3ld-gamma.so` (`make cffi`) | waybar **CFFI plugin** | day/night night-light toggle over `w3ldctl gamma`, live-synced to w3ld |
| `w3ld-waybar` (`make`) | **CLI adapter** for `custom/` modules | `window` (focused title), plus text `workspaces` / per-number `workspace N` |

All three read `$XDG_RUNTIME_DIR/w3ld-$WAYLAND_DISPLAY.sock`: the CFFI plugins
connect directly and auto-reconnect if w3ld restarts; the CLI adapter reads
`w3ldctl subscribe` on stdin.

The gamma plugin is a **live reflector**. w3ld owns the current gamma and
broadcasts every change, so the module mirrors the real state whether it was
changed here, by a hotkey, or by a direct `w3ldctl gamma`. It's asymmetric by
design: temperature is the mode's identity (day = neutral, night = warm, from
config, restored on every toggle); brightness is the ridable trim that scroll
adjusts and that's retained per mode until the bar restarts. The brightness
clamp lives in w3ld (`w3ldctl gamma min|max <pct>`), universal to scroll and
hotkeys.

## Build & install

    make                 # w3ld-waybar (CLI adapter)
    make cffi            # w3ld-workspaces.so + w3ld-gamma.so (needs gtk+-3.0)
    sudo make install    # CLI adapter + example config/style
    sudo make install-cffi

## Configure

See [`examples/waybar/config_w3ld.jsonc`](examples/waybar/config_w3ld.jsonc) and
[`examples/waybar/style.css`](examples/waybar/style.css). Minimal:

```jsonc
"cffi/w3ld-workspaces": {
    "module_path": "/usr/local/lib/w3ld-waybar/w3ld-workspaces.so",
    "output": "DP-1",   // omit to follow the focused output
    "count": 9
},
"custom/w3ld-window": {
    "exec": "w3ldctl subscribe | w3ld-waybar window",
    "return-type": "json", "escape": false
},
"cffi/w3ld-gamma": {
    "module_path": "/usr/local/lib/w3ld-waybar/w3ld-gamma.so",
    "temperature-day": 6500, "temperature-night": 4000,
    "brightness-day": 100, "brightness-night": 60, "step": 5,
    "icon-day": "☀", "icon-night": "☾",
    "format": "{icon} {temperature}K {brightness}%"
}
```

Workspace buttons are plain `button` children of `#w3ld-workspaces`, each
carrying an `active` / `occupied` / `empty` class — style them all at once with
`#w3ld-workspaces button.active` (etc.), no per-workspace selectors. The window
module is `#custom-w3ld-window`; gamma is `#w3ld-gamma` with class `day`/`night`
(and `override` when a manual temperature is in effect). `format` tokens
`{icon} {temperature} {brightness}` — omit any to hide it. Run `w3ldctl outputs`
for connector names.