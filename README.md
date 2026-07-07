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

## Two pieces

| Build | Kind | Provides |
|---|---|---|
| `w3ld-workspaces.so` (`make cffi`) | waybar **CFFI plugin** | one clickable, individually-styled button per workspace (active/occupied/empty) |
| `w3ld-waybar` (`make`) | **CLI adapter** for `custom/` modules | `window` (focused title), plus text `workspaces` / per-number `workspace N` |

Both read `$XDG_RUNTIME_DIR/w3ld-$WAYLAND_DISPLAY.sock` (the CFFI plugin connects
directly and auto-reconnects if w3ld restarts; the CLI adapter reads
`w3ldctl subscribe` on stdin).

## Build & install

    make                 # w3ld-waybar (CLI adapter)
    make cffi            # w3ld-workspaces.so (needs gtk+-3.0)
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
}
```

Workspace buttons are named `#wsN` and carry an `active` / `occupied` / `empty`
class; the window module is `#custom-w3ld-window`. Run `w3ldctl outputs` for
connector names.