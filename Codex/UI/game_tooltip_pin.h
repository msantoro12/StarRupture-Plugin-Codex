#pragma once

#include "plugin_interface.h"

// Pinning from the game's own menus (recipe station, company delivery panel,
// machine menus). While the game's item tooltip is open, the pin key
// (Alt+right-click by default, set in the config) pins the recipe that makes
// the item the tooltip shows, the same pin a right-click on that item gives
// in a Codex window.
namespace CodexTooltipPin
{
	void Init(IPluginSelf* self);
	void Shutdown(IPluginSelf* self);
}
