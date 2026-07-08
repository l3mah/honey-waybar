# honey-waybar

Native [waybar](https://github.com/Alexays/Waybar) modules for
[honey](https://github.com/l3mah/honey-wm), fed by the compositor's live
status stream.

honey already speaks the standard bar protocols, so stock waybar's
`ext/workspaces` and `wlr/taskbar` modules work with no extra software.
honey-waybar exists for the state the standard modules cannot express:

- per-workspace **active / occupied / empty** styling (`ext-workspace` has no
  "has windows" signal)
- a **focused window title** label
- a **night light control** for honey's built-in gamma

It ships three CFFI plugins (real GTK widgets loaded into waybar, not scripts
polling text) plus a small CLI adapter for setups without CFFI. All of them
talk to honey's control socket directly and reconnect automatically if the
compositor restarts.

## Installation

On Void Linux (x86_64 glibc), signed packages:

```sh
echo 'repository=https://l3mah.github.io/void-repo/x86_64' | sudo tee /etc/xbps.d/20-l3mah.conf
sudo xbps-install -S honey-waybar
```

From source (needs a C compiler, pkg-config, and gtk+3 headers):

```sh
make            # the CLI adapter
make cffi       # the three plugins
make install install-cffi    # PREFIX defaults to /usr/local
```

Plugins install to `$PREFIX/lib/honey-waybar/*.so`; that is the path waybar's
`module_path` option must point at. The Void package puts them in
`/usr/lib/honey-waybar/`. Waybar must be built with the CFFI feature (distro
builds normally are).

A ready-made module block and stylesheet live in
[examples/waybar/](examples/waybar/).

## cffi/honey-workspaces

One clickable button per workspace, styled by state. Clicking a button runs
`honeyctl workspace N`. Labels show the workspace number, or its name when one
is set (`honeyctl workspace-name`).

| Option | Default | Meaning |
|---|---|---|
| `module_path` | | path to `honey-workspaces.so` |
| `output` | *(follow focus)* | pin to one monitor by connector name (`DP-1`); omit to follow the focused output |
| `count` | `10` | number of buttons to show (1 to 32) |

Styling: the module is the `#honey-workspaces` box; every button is a plain
`button` child carrying one of the classes `active`, `occupied`, or `empty`,
so one generic rule set covers all workspaces:

```css
#honey-workspaces button          { color: #6272a4; }
#honey-workspaces button.occupied { color: #f8f8f2; }
#honey-workspaces button.active   { color: #bd93f9; }
```

## cffi/honey-window

The focused window's title, falling back to its app-id when the title is
empty, with the app-id as tooltip. Follows focus across all outputs unless
pinned.

| Option | Default | Meaning |
|---|---|---|
| `module_path` | | path to `honey-window.so` |
| `output` | *(follow focus)* | pin to one monitor; omit to follow the focused window |
| `max-length` | `0` | ellipsize the title past this many characters (`0` = no limit) |

Styling: the label is `#honey-window`, with class `active` while a window is
focused and `empty` otherwise.

## cffi/honey-gamma

A day/night toggle for honey's integrated night light. Left click flips
between the day and night presets; scrolling adjusts brightness through
honey's relative op, so the server-side clamp (`honeyctl gamma min|max`)
always applies. honey broadcasts every gamma change, so the module mirrors the
real state no matter what changed it: this module, a hotkey, or a direct
`honeyctl gamma`.

| Option | Default | Meaning |
|---|---|---|
| `module_path` | | path to `honey-gamma.so` |
| `temperature-day` | `6500` | day temperature (Kelvin) |
| `temperature-night` | `4000` | night temperature (Kelvin) |
| `brightness-day` | `100` | starting day brightness (percent) |
| `brightness-night` | `60` | starting night brightness (percent) |
| `step` | `5` | scroll step (percent) |
| `icon-day` | `☀` | glyph substituted for `{icon}` in day mode |
| `icon-night` | `☾` | glyph substituted for `{icon}` in night mode |
| `format` | `{icon} {temperature}K {brightness}%` | display template; tokens `{icon}`, `{temperature}`, `{brightness}`; omit a token to hide it |

Two design points worth knowing:

- **Temperature is the mode's identity, brightness is a trim.** Each toggle
  restores the mode's configured temperature, while brightness changes from
  any source are adopted into the active mode and survive toggles (until the
  bar restarts, which resets to the configured values).
- If the live temperature differs from the active mode's preset (someone ran
  `honeyctl gamma temperature ...`), the module gains an `override` class.

Styling: the module is `#honey-gamma` (classes `day`, `night`, `override`)
holding two independently styleable labels, `#honey-gamma-icon` and
`#honey-gamma-value`:

```css
#honey-gamma-icon                  { color: #bd93f9; }
#honey-gamma.night #honey-gamma-icon { color: #ffb86c; }
```

Example module:

```jsonc
"cffi/honey-gamma": {
    "module_path": "/usr/lib/honey-waybar/honey-gamma.so",
    "temperature-night": 4000,
    "brightness-night": 60,
    "format": "{icon} {brightness}%"
}
```

## The CLI adapter (no CFFI needed)

`honey-waybar` reads `honeyctl subscribe` on stdin and prints waybar
custom-module JSON. Three modes, each optionally pinned to an output:

| Mode | What it renders |
|---|---|
| `workspaces [output]` | one text module listing occupied workspaces, the active one bold |
| `workspace N [output]` | one module for workspace N with class `active`/`occupied`/`empty`; use several for separate buttons |
| `window [output]` | the focused window's title (app-id fallback) |

```jsonc
"custom/honey-workspaces": {
    "exec": "honeyctl subscribe | honey-waybar workspaces DP-1",
    "return-type": "json",
    "escape": false,
    "on-scroll-up": "honeyctl workspace-next",
    "on-scroll-down": "honeyctl workspace-prev"
}
```

The CFFI plugins are the better experience where available; the adapter covers
text-only setups.

## License

[GPL-3.0-or-later](LICENSE).
