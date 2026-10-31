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
- **Pinned recipes** -- pin up to 8 recipes to keep them on screen while you play. The first
  pin starts at the screen edge set by `PinLayout` (right, left or top) and the rest sit under
  it, set by `PinArrangement`: `Tile` (the default) lines them up edge to edge -- down a
  column for right and left, along a row for top, starting a new column or row at the screen
  margin when one fills -- with a gap between them that `PinGap` can turn off, and `Cascade`
  steps each one a title bar down and in. Pinned windows never block game input. While the
  Codex (`Shift+N`), the mod loader overlay (`F2`) or the Escape menu is open, drag the first
  pin and the others follow it; the move cursor shows where it can be grabbed while the Codex
  or the overlay is open (over the Escape menu the game's own cursor stays). Tile and
  Cascade each remember where you dropped the first pin, across sessions. Only the first pin
  moves by hand: if you unpin it, the next oldest becomes the first and takes its place. The
  recipe window's **Arrange pins** button forgets the dropped spot for the current arrangement
  and puts the pins back on the edge. Clicking a pin brings it to the front without moving
  anything. Set `PinGrouping` to `Ungrouped` and every pin is moved by hand on its own,
  and dragging one never moves another; a new pin appears where the arrangement would put it
  next to the last pin when that spot is free and on screen, else at the next free spot of the
  arrangement, else at the default spot. Switching to `Ungrouped` leaves every pin where it is;
  switching back to `Grouped` puts the rest back under the first pin, which stays where it is. Pins are not
  restored after a reload: the first pin you pin again returns to where the first one was last left, and the
  rest are placed next to it. Clicking a pin raises that window; unpinning a pin lets the others
  take its place in the stack, so the pin in front can change. Pins always stay fully on screen: a column or row that fills wraps to the side of the pins that has room, a cascade takes shorter steps when the first pin is near the bottom, and a saved spot is kept on screen when the resolution or window size changes. Use the Codex to unpin them.
- **Launch planner** -- the recipe tab and every pinned window (while the Codex is open, like
  Open and Unpin) have a button that opens a production planner for the recipe's output in
  your browser. Choose the planner with `Planner`: the StarRupture Planner
  (the default) adds the item at one machine's output, starruptureplanner.com and
  starrupturecalc.com open their calculator for the item (starruptureplanner.com opens the
  item's own page for the items its calculator has no recipe for), and starrupture-planner.com
  opens its planner page (it has no item link). Items a planner does not list open it without
  an item. For any other planner, set `Planner` to `Custom` and give `CustomUrlTemplate` an
  address with `{item}`, `{name}` or `{game}`.
- **Configurable** -- enable/disable the plugin, rebind the keys and choose the pin layout
  from the mod loader's config page or config file. `Planner`, `PinLayout`, `PinArrangement`
  and `PinGrouping` are dropdowns there.

## Configuration

Settings are written to the plugin's config file on first run and can be edited there.
On the mod loader's config page, `Planner`, `PinLayout`, `PinArrangement` and `PinGrouping` are dropdowns. The search
box and pin positions have no field: the **Saved state** row at the end of the `Menu`
section has a **Clear** button that puts the search box back in the centre and the pins back
on their edge at once, even with the search box open. Values already in your config file
keep working, and choices match ignoring case. The dropdowns and the hidden entries need a
loader that has the Choice and Hidden config types (v71); on an older one the page shows
them as plain text fields.


| Section | Key | Default | Description |
| --- | --- | --- | --- |
| `General` | `Enabled` | `true` | Enable or disable Codex |
| `Menu` | `SearchKey` | `N` | Key to open the recipe search box |
| `Menu` | `CodexKey` | `Shift+N` | Key to open (or close) the full Codex window |
| `Menu` | `PinLayout` | `Right` | Screen edge pinned recipes start from: `Right`, `Left` or `Top` (dropdown) |
| `Menu` | `PinArrangement` | `Tile` | How pins sit under the first one: `Tile` or `Cascade` (dropdown) |
| `Menu` | `PinGrouping` | `Grouped` | Whether pins move together under the first one or each on its own: `Grouped` or `Ungrouped` (dropdown) |
| `Menu` | `PinGap` | `true` | Leave a gap between tiled pins; off packs them edge to edge |
| `Menu` | `SearchBoxX` | `-1` | Search box horizontal centre as a fraction of the screen width (`-1` = centred). Set by dragging the box. No field on the config page; Clear resets it |
| `Menu` | `SearchBoxY` | `-1` | Search box top edge as a fraction of the screen height (`-1` = centred). Set by dragging the box. No field on the config page; Clear resets it |
| `Planner` | `Planner` | `StarRupture Planner` | Planner the Launch planner button opens: `StarRupture Planner`, `starruptureplanner.com`, `starrupturecalc.com`, `starrupture-planner.com` or `Custom` (dropdown) |
| `Planner` | `CustomUrlTemplate` | (empty) | Address used when `Planner` is `Custom`. `{item}` becomes the StarRupture Planner's item id, `{name}` the item's display name and `{game}` the game's internal item name, all URL-encoded, e.g. `https://example.com/plan?item={item}` |
| `Menu` | `PinTileOffset` | empty | Where the first pin was last dropped while tiling, as an `x,y` screen fraction. Set by dragging it. No field on the config page; Clear resets it |
| `Menu` | `PinFreeOffsets` | empty | Where each pin was last placed while ungrouped, as `x,y` screen fractions separated by `;`, in pin order. Set by dragging a pin or pinning one; the first pin's spot is used again for the next first pin. No field on the config page; Clear resets it and puts the pins where they would go if just pinned |
| `Menu` | `PinCascadeOffset` | empty | Where the first pin was last dropped while cascading, as an `x,y` screen fraction. Set by dragging it. No field on the config page; Clear resets it |

---

## Building

1. Clone this repo (with submodules, or set `GameSDKRoot` / `PluginSDKInclude` in `Shared.props`)
2. Open `StarRupture-Plugin-Codex.sln` in Visual Studio 2022
3. Build the `Client Release` configuration (x64)
4. The output DLL is written to `build\Client Release\Plugins\Codex.dll`
5. Drop it into `Binaries\Win64\Plugins\` alongside `dwmapi.dll` and launch the game

See [PluginDevelopment.md](PluginDevelopment.md) for the full plugin API reference.
