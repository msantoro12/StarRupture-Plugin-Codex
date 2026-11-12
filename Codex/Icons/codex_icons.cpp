#include "codex_icons.h"
#include "Plugin Core/Helpers/plugin_helpers.h"

#include "plugin_images.h"

#include "Engine_classes.hpp"

namespace CodexIcons
{
	namespace
	{
		constexpr float kIconGap = 6.0f;

		IPluginImages* ImageTable()
		{
			IPluginHooks* hooks = GetHooks();
			return hooks ? hooks->Images : nullptr;
		}
	}

	void Init(IPluginSelf* self)
	{
		if (IPluginImages* images = ImageTable())
			images->UseSets(self, PLUGIN_IMAGE_SET_ITEMS);
	}

	void Draw(IModLoaderImGui* imgui, const std::string& uniqueItemName, float iconSize)
	{
		if (!imgui)
			return;

		// Item only hands out an id; the library reads the image when Image
		// calls Get, and Image skips Get for a row that is scrolled out of view.
		IPluginImages* images = ImageTable();
		const PluginImageId id = images ? images->Item(GetSelf(), uniqueItemName.c_str()) : 0;

		PluginImages::Image(imgui, GetHooks(), id, iconSize, iconSize);
		imgui->SameLine(0.0f, kIconGap);
	}

	uint32_t ImageOfTexture(SDK::UObject* texture)
	{
		IPluginImages* images = ImageTable();
		if (!images || !texture || !SDK::UKismetSystemLibrary::IsValid(texture)
			|| !texture->IsA(SDK::UTexture2D::StaticClass()))
			return 0;

		return images->FromTexture(GetSelf(), static_cast<SDK::UTexture2D*>(texture));
	}

	uint32_t ItemImage(const std::string& uniqueItemName)
	{
		IPluginImages* images = ImageTable();
		return images ? images->Item(GetSelf(), uniqueItemName.c_str()) : 0;
	}

	bool SameImage(uint32_t itemImage, uint32_t textureImage)
	{
		IPluginImages* images = ImageTable();
		if (!images || itemImage == 0 || textureImage == 0)
			return false;
		return images->Canonical(itemImage) == textureImage;
	}
}
