#pragma once

#include "plugin_interface.h"

// The Codex search box (opened via a configurable keybind, closed with Esc)
// and the recipe detail window it opens into.
namespace CodexUI
{
	void Init(IPluginSelf* self);

	// Once per engine tick: puts the saved pins back as soon as the recipe list is ready.
	void Tick();
	void Shutdown(IPluginSelf* self);
}
