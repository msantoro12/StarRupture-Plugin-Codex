#include "game_tooltip_pin.h"
#include "codex_ui.h"
#include "Icons/codex_icons.h"
#include "Recipes/codex_recipes.h"
#include "Plugin Core/Helpers/plugin_helpers.h"
#include "Plugin Core/Config/plugin_config.h"
#include "Plugin Core/codex_fields.h"

#include "ChimeraUI_classes.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace CodexTooltipPin
{
	namespace
	{
		// The anchor every game widget with an item tooltip opens it from.
		constexpr const char* kTooltipAnchorName = "TooltipAnchor";

		bool EqualsNoCase(const std::string& a, const std::string& b)
		{
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
				[](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
		}

		void AddOnce(std::vector<std::string>& names, const std::string& name)
		{
			if (std::find(names.begin(), names.end(), name) == names.end())
				names.push_back(name);
		}

		// The item name in the tooltip's header, or empty.
		std::string TooltipTitle(IPluginObjectProperties* properties, SDK::UUserWidget* tooltip)
		{
			SDK::UUserWidget** header = CodexFields::TooltipInfoWidget.In(properties, tooltip);
			if (!header || !*header)
				return {};
			SDK::UTextBlock** name = CodexFields::TooltipItemName.In(properties, *header);
			if (!name || !*name || !(*name)->IsA(SDK::UTextBlock::StaticClass()))
				return {};
			const SDK::FText text = (*name)->GetText();
			return SDK::UKismetTextLibrary::Conv_TextToString(text).ToString();
		}

		// The texture the tooltip's icon shows, or null. Compared, never dereferenced.
		const void* TooltipTexture(IPluginObjectProperties* properties, SDK::UUserWidget* tooltip)
		{
			SDK::UImage** icon = CodexFields::TooltipIcon.In(properties, tooltip);
			if (!icon || !*icon)
				return nullptr;
			const SDK::FSlateBrush* brush = CodexFields::ImageBrush.In(properties, *icon);
			return brush ? brush->ResourceObject : nullptr;
		}

		// The item the tooltip shows, among the items a recipe makes: by its
		// title, and by its icon where the title is unreadable or names more than
		// one item. A title naming no product (a raw resource) gives nothing, so
		// an icon it shares with a product cannot pin that product. Empty when
		// that leaves anything but exactly one.
		std::string MatchProduct(const std::string& title, const void* texture)
		{
			std::vector<std::string> byName;
			std::vector<std::string> byIcon;
			for (const CodexRecipes::RecipeInfo& recipe : CodexRecipes::GetAll())
			{
				const std::string& item = recipe.output.uniqueItemName;
				if (!title.empty() && EqualsNoCase(recipe.output.displayName, title))
					AddOnce(byName, item);
				if (texture && CodexIcons::GetIconTexture(item) == texture)
					AddOnce(byIcon, item);
			}

			if (byName.size() == 1)
				return byName.front();

			std::vector<std::string> both;
			for (const std::string& item : byName)
				if (std::find(byIcon.begin(), byIcon.end(), item) != byIcon.end())
					both.push_back(item);

			const std::vector<std::string>& picked = title.empty() ? byIcon : both;
			return picked.size() == 1 ? picked.front() : std::string();
		}

		// The item in the game's open item tooltip, or empty when none is open
		// or what it shows is not something a recipe makes, or the player's own
		// inventory is open. One pass over
		// GObjects per press, nothing kept: the tooltip only lives while open.
		std::string ReadOpenTooltipItem()
		{
			IPluginHooks* hooks = GetHooks();
			IPluginObjectProperties* properties = hooks ? hooks->ObjectProperties : nullptr;
			SDK::UClass* tooltipClass = SDK::UCr_UW_InventoryToolTip::StaticClass();
			SDK::UClass* anchorClass = SDK::UMenuAnchor::StaticClass();
			SDK::UClass* screenClass = SDK::UCrUW_InventoryScreen::StaticClass();
			if (!properties || !tooltipClass || !anchorClass || !screenClass)
				return {};

			constexpr auto kSkipFlags =
				static_cast<uint32_t>(SDK::EObjectFlags::ClassDefaultObject) |
				static_cast<uint32_t>(SDK::EObjectFlags::ArchetypeObject) |
				static_cast<uint32_t>(SDK::EObjectFlags::BeginDestroyed) |
				static_cast<uint32_t>(SDK::EObjectFlags::FinishDestroyed) |
				static_cast<uint32_t>(SDK::EObjectFlags::MirroredGarbage);

			// A tooltip widget that is on screen. One whose menu has closed has
			// lost its Slate widget, and IsVisible is false for it.
			std::vector<SDK::UUserWidget*> shown;
			bool anchorOpen = false;
			bool playerInventoryOpen = false;

			const int count = SDK::UObject::GObjects->Num();
			for (int i = 0; i < count; ++i)
			{
				SDK::UObject* obj = SDK::UObject::GObjects->GetByIndex(i);
				if (!obj || (static_cast<uint32_t>(obj->Flags) & kSkipFlags))
					continue;

				if (obj->IsA(anchorClass))
				{
					if (!anchorOpen && obj->GetName() == kTooltipAnchorName &&
						static_cast<SDK::UMenuAnchor*>(obj)->IsOpen())
						anchorOpen = true;
				}
				else if (obj->IsA(screenClass))
				{
					// The inventory screen also hosts machine menus, which set
					// the actor being used; the player's own inventory has none.
					auto* screen = static_cast<SDK::UUserWidget*>(obj);
					bool* active = CodexFields::InventoryActive.In(properties, screen);
					SDK::AActor** used = CodexFields::InventoryUsedActor.In(properties, screen);
					if (screen->IsVisible() && (!active || *active) && (!used || !*used))
						playerInventoryOpen = true;
				}
				else if (obj->IsA(tooltipClass))
				{
					auto* tooltip = static_cast<SDK::UUserWidget*>(obj);
					if (tooltip->IsVisible())
						shown.push_back(tooltip);
				}
			}

			// Right-click is the game's own action on inventory slots.
			if (playerInventoryOpen)
			{
				LOG_DEBUG("CodexTooltipPin: the player's inventory is open; nothing pinned.");
				return {};
			}

			if (!anchorOpen || shown.empty())
			{
				LOG_DEBUG("CodexTooltipPin: no item tooltip is open (anchor open: %d, tooltips shown: %d).",
					anchorOpen ? 1 : 0, static_cast<int>(shown.size()));
				return {};
			}

			std::string found;
			for (SDK::UUserWidget* tooltip : shown)
			{
				const std::string title = TooltipTitle(properties, tooltip);
				const std::string item = MatchProduct(title, TooltipTexture(properties, tooltip));
				if (item.empty())
				{
					LOG_INFO("CodexTooltipPin: the tooltip's item '%s' is not one a recipe makes; nothing pinned.", title.c_str());
					continue;
				}
				if (!found.empty() && found != item)
				{
					LOG_INFO("CodexTooltipPin: %d item tooltips are up at once; nothing pinned.", static_cast<int>(shown.size()));
					return {};
				}
				found = item;
			}
			return found;
		}

		// Runs from the game's window messages, on the game thread and before
		// the game handles the click, which may close the tooltip. So the item
		// is read here, and only its name is handed on.
		void OnPinKeyPressed(EModKey /*key*/, EModKeyEvent event)
		{
			if (event != EModKeyEvent::Pressed || !CodexRecipes::IsReady())
				return;

			std::string item;
			try
			{
				item = ReadOpenTooltipItem();
			}
			catch (...)
			{
				LOG_WARN("CodexTooltipPin: reading the open tooltip failed.");
				return;
			}

			if (item.empty())
				return;

			LOG_INFO("CodexTooltipPin: pin asked for '%s' from the game's tooltip (skipped if a pin already makes it).", item.c_str());
			CodexUI::PinProductOf(item);
		}
	}

	void Init(IPluginSelf* self)
	{
		if (!self->hooks->Input || !self->hooks->UI)
			return;

		const char* key = CodexConfig::Config::GetGamePinKey();
		LOG_DEBUG("CodexTooltipPin: registering game-menu pin keybind '%s'", key);
		self->hooks->Input->RegisterKeybindByName(key, EModKeyEvent::Pressed, &OnPinKeyPressed);
	}

	void Shutdown(IPluginSelf* self)
	{
		if (self && self->hooks->Input && self->hooks->UI)
			self->hooks->Input->UnregisterKeybindByName(CodexConfig::Config::GetGamePinKey(), EModKeyEvent::Pressed, &OnPinKeyPressed);
	}
}
