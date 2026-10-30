# Codex -- StarRupture Plugin

A StarRupture plugin built on the StarRupture ModLoader that adds an in-game recipe codex:
search any item or recipe and jump straight to a detail view of what it's crafted from and
what it produces.

**Target:** Game client only

**Requires:** a mod loader with plugin interface v71 or newer. On an older
loader the plugin shows "Needs loader" and stays off.

---

## Features

- **Recipe search** -- press the configurable keybind (default `N`) to open a search box,
  type an item, recipe or building name, then use `Up`/`Down` to pick a result and `Enter`
  to open it (the top result is highlighted by default, and hovering a row highlights it
  too). Close with `Esc`. The box opens centred; drag it anywhere and it opens there next time.
- **Full Codex window** -- press `Shift+N` (configurable) to open the Codex straight to the
  last recipe you viewed, or the building browser if you haven't picked one yet.
- **Recipe detail window** -- shows a recipe's inputs and outputs side by side, each with
  icon, name, count, and craft rate (items/min). Click an input to jump to the recipe that
  produces it; click an output to see every recipe that consumes it. Click a building under
  "Made in" to browse everything that building makes.
- **Building browser** -- the Buildings tab lists every crafting building and the recipes it
  can produce. Tiered buildings are grouped under one name (e.g. "Fabricator") with a
  Tier 1 / Tier 2 tab for each tier. Click a recipe to open it, right-click to pin it.
- **Pinned recipes** -- pin up to 8 recipes to keep them on screen while you play. New pins
  cascade from the screen edge set by `PinLayout` (right, left or top), each one a title bar
  lower than the last. Pinned windows never block game input; drag them while the Codex
  (`Shift+N`), the mod loader overlay (`F2`) or the Escape menu is open, and use the Codex to
  unpin them. Each pin slot remembers where you dropped it, across sessions. The recipe
  window's **Arrange pins** button forgets those spots and puts the pins back in their cascade.
- **Launch planner** -- the recipe tab has a button that opens a production planner for the
  recipe's output in your browser. The StarRupture Planner is the default; set `Planner` to
  `Custom` and give `CustomUrlTemplate` an address with `{item}` for another planner.
- **Configurable** -- enable/disable the plugin, rebind the keys and choose the pin layout
  from the mod loader's config page or config file.

## Configuration

Settings are written to the plugin's config file on first run and can be edited there:

| Section | Key | Default | Description |
| --- | --- | --- | --- |
| `General` | `Enabled` | `true` | Enable or disable Codex |
| `Menu` | `SearchKey` | `N` | Key to open the recipe search box |
| `Menu` | `CodexKey` | `Shift+N` | Key to open (or close) the full Codex window |
| `Menu` | `PinLayout` | `Right` | Screen edge new pinned recipes cascade from: `Right`, `Left` or `Top` |
| `Menu` | `SearchBoxX` | `-1` | Search box horizontal centre as a fraction of the screen width (`-1` = centred). Set by dragging the box |
| `Menu` | `SearchBoxY` | `-1` | Search box top edge as a fraction of the screen height (`-1` = centred). Set by dragging the box |
| `Planner` | `Planner` | `StarRupture Planner` | Planner the Launch planner button opens: `StarRupture Planner` or `Custom` |
| `Planner` | `CustomUrlTemplate` | (empty) | Address used when `Planner` is `Custom`; `{item}` becomes the item's planner id, e.g. `https://example.com/plan?item={item}` |
| `Menu` | `PinPositions` | empty | Where each pin slot was last dropped, as `x,y` screen fractions separated by `;`. Set by dragging a pin; clear it to reset |

---

## Building

1. Clone this repo (with submodules, or set `GameSDKRoot` / `PluginSDKInclude` in `Shared.props`)
2. Open `StarRupture-Plugin-Codex.sln` in Visual Studio 2022
3. Build the `Client Release` configuration (x64)
4. The output DLL is written to `build\Client Release\Plugins\Codex.dll`
5. Drop it into `Binaries\Win64\Plugins\` alongside `dwmapi.dll` and launch the game

See [PluginDevelopment.md](PluginDevelopment.md) for the full plugin API reference.
