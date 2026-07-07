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
- a **night-light control** for w3ld's integrated gamma (`w3ldctl gamma`) —
  click to toggle warm/neutral, scroll to adjust brightness.

## Three pieces

| Build | Kind | Provides |
|---|---|---|
| `w3ld-workspaces.so` (`make cffi`) | waybar **CFFI plugin** | one clickable, individually-styled button per workspace (active/occupied/empty) |
| `w3ld-waybar` (`make`) | **CLI adapter** for `custom/` modules | `window` (focused title), plus text `workspaces` / per-number `workspace N` |
| `w3ld-gamma` (script) | **`custom/` module** helper | night-light toggle / brightness driving `w3ldctl gamma` |

The plugin and CLI adapter read `$XDG_RUNTIME_DIR/w3ld-$WAYLAND_DISPLAY.sock` (the
CFFI plugin connects directly and auto-reconnects if w3ld restarts; the CLI
adapter reads `w3ldctl subscribe` on stdin). `w3ld-gamma` tracks its own on/off
state and calls `w3ldctl gamma`.

## Build & install

    make                 # w3ld-waybar (CLI adapter)
    make cffi            # w3ld-workspaces.so (needs gtk+-3.0)
    sudo make install    # CLI adapter + w3ld-gamma + example config/style
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
"custom/w3ld-gamma": {
    "exec": "w3ld-gamma", "return-type": "json", "interval": 2,
    "on-click": "w3ld-gamma toggle",
    "on-scroll-up": "w3ld-gamma gamma-up",
    "on-scroll-down": "w3ld-gamma gamma-down"
}
```

Workspace buttons are plain `button` children of `#w3ld-workspaces`, each
carrying an `active` / `occupied` / `empty` class — style them all at once with
`#w3ld-workspaces button.active` (etc.), no per-workspace selectors. The window
module is `#custom-w3ld-window`; gamma is `#custom-w3ld-gamma` with class
`on`/`off`. Run `w3ldctl outputs` for connector names.