#pragma once

#include "plugin_interface.h"

// Pinning from the game's own menus (recipe station, company delivery panel,
// machine menus). While the game's item tooltip is open, the pin key
// (right-click by default, set in the config) pins the recipe that makes the
// item the tooltip shows, the same pin a right-click on that item gives in a
// Codex window. The tooltip ends with a "Pin to Codex" row, in the game's own
// action-button style, while that pin would work.
namespace CodexTooltipPin
{
	void Init(IPluginSelf* self);
	void Tick(float deltaSeconds);
	void Shutdown(IPluginSelf* self);
}
