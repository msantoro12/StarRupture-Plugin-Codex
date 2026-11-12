#pragma once

#include "plugin_interface.h"

#include <cstdint>
#include <string>

namespace SDK { class UObject; }

// Item icons, drawn from the loader's image library (hooks->Images). The
// library reads each icon the first time it is drawn and keeps a small copy for
// the session; nothing is loaded or cached here.
//
// Where the loader has no image table (a server or generic build), nothing is
// drawn and every icon keeps its space.
namespace CodexIcons
{
	// Tells the library the item images are the ones Codex draws, so it can
	// have them ready. Call once from PluginInit.
	void Init(IPluginSelf* self);

	// Draws the item's icon at the cursor, size by size, and moves on by the gap
	// after it. An icon that is still loading, or an item with none, holds its
	// place, so the text after it does not move. Call from a render callback.
	void Draw(IModLoaderImGui* imgui, const std::string& uniqueItemName, float iconSize);

	// The image id of a texture, or 0 when the library cannot name it. Ids are
	// plain numbers and are equal for the same texture, so two icons can be
	// compared without holding either texture. Game thread; the texture is not
	// kept.
	uint32_t ImageOfTexture(SDK::UObject* texture);
}
