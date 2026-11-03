#include "game_tooltip_pin.h"
#include "codex_ui.h"
#include "Icons/codex_icons.h"
#include "Recipes/codex_recipes.h"
#include "Plugin Core/Helpers/plugin_helpers.h"
#include "Plugin Core/Config/plugin_config.h"
#include "Plugin Core/codex_fields.h"

#include "ChimeraUI_classes.hpp"
#include "BP_CommonInput_KeyboardMouse_classes.hpp"
#include "WBP_ActionButton_classes.hpp"
#include "object_ref.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
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


		// What one pass over GObjects found. The pointers are good for the tick
		// that made it and no longer.
		struct Observation
		{
			std::vector<SDK::UUserWidget*> shown; // item tooltips that are on screen
			bool anchorOpen = false;              // a TooltipAnchor is open
			bool screenUp = false;                // an inventory screen is visible and active
			bool machineInUse = false;            // ...and it has a machine open
			bool playerInventoryOpen = false;     // right-click is the game's own action
		};

		enum class Kind : uint8_t { Other, Anchor, Screen, Tooltip };

		// One pass over GObjects. A class's kind is worked out once, from its
		// first object, so each further object costs one lookup and no IsA walk.
		// A tooltip widget on screen is one whose Slate widget is alive: a closed
		// menu's tooltip has lost it, and IsVisible is false for it.
		Observation Observe(IPluginObjectProperties* properties)
		{
			Observation seen;
			SDK::UClass* tooltipClass = SDK::UCr_UW_InventoryToolTip::StaticClass();
			SDK::UClass* anchorClass = SDK::UMenuAnchor::StaticClass();
			SDK::UClass* screenClass = SDK::UCrUW_InventoryScreen::StaticClass();
			if (!properties || !tooltipClass || !anchorClass || !screenClass)
				return seen;

			constexpr auto kSkipFlags =
				static_cast<uint32_t>(SDK::EObjectFlags::ClassDefaultObject) |
				static_cast<uint32_t>(SDK::EObjectFlags::ArchetypeObject) |
				static_cast<uint32_t>(SDK::EObjectFlags::BeginDestroyed) |
				static_cast<uint32_t>(SDK::EObjectFlags::FinishDestroyed) |
				static_cast<uint32_t>(SDK::EObjectFlags::MirroredGarbage);

			static std::unordered_map<const SDK::UClass*, Kind> kinds;

			const int count = SDK::UObject::GObjects->Num();
			for (int i = 0; i < count; ++i)
			{
				SDK::UObject* obj = SDK::UObject::GObjects->GetByIndex(i);
				if (!obj || (static_cast<uint32_t>(obj->Flags) & kSkipFlags) || !obj->Class)
					continue;

				auto known = kinds.find(obj->Class);
				if (known == kinds.end())
				{
					Kind kind = Kind::Other;
					if (obj->IsA(anchorClass))
						kind = Kind::Anchor;
					else if (obj->IsA(screenClass))
						kind = Kind::Screen;
					else if (obj->IsA(tooltipClass))
						kind = Kind::Tooltip;
					known = kinds.emplace(obj->Class, kind).first;
				}

				switch (known->second)
				{
				case Kind::Anchor:
					if (!seen.anchorOpen && obj->GetName() == kTooltipAnchorName &&
						static_cast<SDK::UMenuAnchor*>(obj)->IsOpen())
						seen.anchorOpen = true;
					break;
				case Kind::Screen:
				{
					// The inventory screen also hosts machine menus, which set
					// the actor being used; the player's own inventory has none.
					auto* screen = static_cast<SDK::UUserWidget*>(obj);
					bool* active = CodexFields::InventoryActive.In(properties, screen);
					SDK::AActor** used = CodexFields::InventoryUsedActor.In(properties, screen);
					if (screen->IsVisible() && (!active || *active) && used)
					{
						seen.screenUp = true;
						seen.machineInUse = *used != nullptr;
						seen.playerInventoryOpen = !seen.machineInUse;
					}
					break;
				}
				case Kind::Tooltip:
				{
					auto* tooltip = static_cast<SDK::UUserWidget*>(obj);
					if (tooltip->IsVisible())
						seen.shown.push_back(tooltip);
					break;
				}
				default:
					break;
				}
			}
			return seen;
		}

		// The item the pin would act on given what is open, or empty when it
		// would do nothing: no tooltip open, what it shows is not something a
		// recipe makes, or the player's own inventory is open. Right-click is the
		// game's own action on inventory slots.
		std::string PinTarget(IPluginObjectProperties* properties, const Observation& seen, bool log)
		{
			if (seen.playerInventoryOpen)
			{
				if (log)
					LOG_DEBUG("CodexTooltipPin: the player's inventory is open; nothing pinned.");
				return {};
			}

			if (!seen.anchorOpen || seen.shown.empty())
			{
				if (log)
					LOG_DEBUG("CodexTooltipPin: no item tooltip is open (anchor open: %d, tooltips shown: %d).",
						seen.anchorOpen ? 1 : 0, static_cast<int>(seen.shown.size()));
				return {};
			}

			std::string found;
			for (SDK::UUserWidget* tooltip : seen.shown)
			{
				const std::string title = TooltipTitle(properties, tooltip);
				const std::string item = MatchProduct(title, TooltipTexture(properties, tooltip));
				if (item.empty())
				{
					if (log)
						LOG_INFO("CodexTooltipPin: the tooltip's item '%s' is not one a recipe makes; nothing pinned.", title.c_str());
					continue;
				}
				if (!found.empty() && found != item)
				{
					if (log)
						LOG_INFO("CodexTooltipPin: %d item tooltips are up at once; nothing pinned.", static_cast<int>(seen.shown.size()));
					return {};
				}
				found = item;
			}
			return found;
		}

		// The item in the game's open item tooltip, or empty. One pass over
		// GObjects per press, nothing kept: the tooltip only lives while open.
		std::string ReadOpenTooltipItem()
		{
			IPluginHooks* hooks = GetHooks();
			IPluginObjectProperties* properties = hooks ? hooks->ObjectProperties : nullptr;
			if (!properties)
				return {};

			const Observation seen = Observe(properties);
			if (seen.screenUp)
				LOG_INFO("CodexTooltipPin: the inventory screen is up (machine in use: %d).", seen.machineInUse ? 1 : 0);
			return PinTarget(properties, seen, true);
		}

		// ---- The hint: a "Pin to Codex" row in the tooltip's own action list ----
		//
		// The tooltip (WBP_ToolTipWidget_C) holds a WBP_TooltipPrompt_C whose
		// ActionsBox is the vertical list of the game's WBP_ActionButton_C hints.
		// The row is one more of those, with the key's icon from the game's own
		// keyboard-and-mouse input data. It is added once per tooltip, follows what
		// the tooltip shows, and is taken out again on unload.

		constexpr float kWatchSeconds = 0.25f; // between looks for a new tooltip
		constexpr float kIdleSeconds = 1.0f;   // after one that shows nothing pinnable

		struct HintRow
		{
			ObjectRef<SDK::UUserWidget> tooltip;
			ObjectRef<SDK::UUserWidget> row;
			ObjectRef<SDK::UUserWidget> prompt;
			ObjectRef<SDK::UWidget> overlay;
			SDK::ESlateVisibility promptWas = SDK::ESlateVisibility::Visible;
			SDK::ESlateVisibility overlayWas = SDK::ESlateVisibility::Visible;
			bool restorePrompt = false;
			bool restoreOverlay = false;
		};

		std::vector<HintRow> g_rows;
		float g_sinceLook = 0.0f;
		float g_lookEvery = kWatchSeconds;
		int g_problemLogs = 0;
		bool g_cursorLogged = false;

		void HintProblem(const char* what)
		{
			if (g_problemLogs++ < 5)
				LOG_INFO("CodexTooltipHint: no row added: %s.", what);
			else
				LOG_DEBUG("CodexTooltipHint: no row added: %s.", what);
		}

		bool IsHidden(SDK::ESlateVisibility v)
		{
			return v == SDK::ESlateVisibility::Collapsed || v == SDK::ESlateVisibility::Hidden;
		}

		// The pin key as the row names it: its game key when it is a single key,
		// else spelled out ("Alt+Right-click").
		struct KeyLabel
		{
			std::string gameKey; // the game's name for the key, when single
			std::string spelled; // the whole binding for a person
		};

		std::string FriendlyKey(const std::string& token)
		{
			if (EqualsNoCase(token, "RightMouseButton")) return "Right-click";
			if (EqualsNoCase(token, "LeftMouseButton")) return "Left-click";
			if (EqualsNoCase(token, "MiddleMouseButton")) return "Middle-click";
			return token;
		}

		KeyLabel LabelOfPinKey()
		{
			const std::string binding = CodexConfig::Config::GetGamePinKey();
			std::vector<std::string> tokens;
			size_t from = 0;
			while (from <= binding.size())
			{
				size_t plus = binding.find('+', from);
				if (plus == std::string::npos)
					plus = binding.size();
				if (plus > from)
					tokens.push_back(binding.substr(from, plus - from));
				from = plus + 1;
			}

			KeyLabel label;
			for (size_t i = 0; i < tokens.size(); ++i)
				label.spelled += (i ? "+" : "") + FriendlyKey(tokens[i]);
			if (tokens.size() == 1)
				label.gameKey = tokens.front();
			return label;
		}

		// The game's brush for a key, from the keyboard-and-mouse input data the
		// Common UI action widgets read their key icons from. Null when the game
		// has none for it.
		const SDK::FSlateBrush* GameKeyBrush(const std::string& gameKey)
		{
			if (gameKey.empty())
				return nullptr;
			SDK::UBP_CommonInput_KeyboardMouse_C* data = SDK::UBP_CommonInput_KeyboardMouse_C::GetDefaultObj();
			if (!data)
				return nullptr;
			for (int i = 0; i < data->InputBrushDataMap.Num(); ++i)
			{
				const SDK::FCommonInputKeyBrushConfiguration& entry = data->InputBrushDataMap[i];
				if (EqualsNoCase(entry.Key.KeyName.ToString(), gameKey))
					return &entry.KeyBrush;
			}
			return nullptr;
		}

		// Adds the row to one tooltip; null on success, else why it could not.
		const char* AddRow(IPluginObjectProperties* properties, SDK::UUserWidget* tooltip, HintRow& entry)
		{
			SDK::UUserWidget** promptSlot = CodexFields::TooltipPrompt.In(properties, tooltip);
			SDK::UUserWidget* prompt = promptSlot ? *promptSlot : nullptr;
			if (!prompt)
				return "the tooltip has no action prompt";

			SDK::UVerticalBox** boxSlot = CodexFields::PromptActionsBox.In(properties, prompt);
			SDK::UVerticalBox* box = boxSlot ? *boxSlot : nullptr;
			if (!box)
				return "the action prompt has no actions list";

			SDK::UClass* buttonClass = SDK::UWBP_ActionButton_C::StaticClass();
			if (!buttonClass)
				return "the game's action button class is not loaded";

			SDK::UUserWidget* row = SDK::UWidgetBlueprintLibrary::Create(tooltip, buttonClass, tooltip->GetOwningPlayer());
			if (!row)
				return "the game would not create an action button";

			SDK::UTextBlock** nameSlot = CodexFields::ButtonActionName.In(properties, row);
			SDK::UTextBlock* name = nameSlot ? *nameSlot : nullptr;
			if (!name)
				return "the action button has no label";

			// The key's own icon where the pin key is one key the game has an icon
			// for; otherwise the label names the binding.
			const KeyLabel key = LabelOfPinKey();
			const SDK::FSlateBrush* brush = GameKeyBrush(key.gameKey);
			SDK::UImage** iconSlot = CodexFields::ButtonActionIcon.In(properties, row);
			SDK::UImage* icon = iconSlot ? *iconSlot : nullptr;
			const bool withIcon = brush && icon;

			const std::string label = withIcon ? "Pin to Codex" : key.spelled + ": Pin to Codex";
			const std::wstring wide(label.begin(), label.end());
			name->SetText(SDK::UKismetTextLibrary::Conv_StringToText(SDK::FString(wide.c_str())));

			if (withIcon)
			{
				icon->SetBrush(*brush);
				icon->SetVisibility(SDK::ESlateVisibility::HitTestInvisible);
				SDK::USizeBox** sizeSlot = CodexFields::ButtonIconSizeBox.In(properties, row);
				if (sizeSlot && *sizeSlot)
					(*sizeSlot)->SetVisibility(SDK::ESlateVisibility::HitTestInvisible);
				SDK::UWidget** inputSlot = CodexFields::ButtonInputBox.In(properties, row);
				if (inputSlot && *inputSlot)
					(*inputSlot)->SetVisibility(SDK::ESlateVisibility::Collapsed);
			}

			// A tooltip with nothing else to do keeps its prompt hidden.
			entry.prompt.Set(prompt);
			entry.promptWas = prompt->GetVisibility();
			if (IsHidden(entry.promptWas))
			{
				entry.restorePrompt = true;
				prompt->SetVisibility(SDK::ESlateVisibility::SelfHitTestInvisible);
			}
			SDK::UWidget** overlaySlot = CodexFields::TooltipActionsOverlay.In(properties, tooltip);
			if (overlaySlot && *overlaySlot)
			{
				SDK::UWidget* overlay = *overlaySlot;
				entry.overlay.Set(overlay);
				entry.overlayWas = overlay->GetVisibility();
				if (IsHidden(entry.overlayWas))
				{
					entry.restoreOverlay = true;
					overlay->SetVisibility(SDK::ESlateVisibility::SelfHitTestInvisible);
				}
			}

			row->SetVisibility(SDK::ESlateVisibility::HitTestInvisible);
			box->AddChild(row);
			entry.row.Set(row);

			LOG_INFO("CodexTooltipHint: row added (key '%s', %s, prompt was %d, overlay was %d).",
				key.spelled.c_str(),
				withIcon ? "game icon" : "no game icon for it, so the label names the key",
				static_cast<int>(entry.promptWas), static_cast<int>(entry.overlayWas));
			return nullptr;
		}

		// Whether the player's cursor is up: the only time a tooltip can be open.
		bool CursorUp()
		{
			SDK::UWorld* world = PluginSdkRuntime::GetWorld();
			SDK::APlayerController* controller = world ? SDK::UGameplayStatics::GetPlayerController(world, 0) : nullptr;
			return controller && controller->bShowMouseCursor;
		}

		// The rows on tooltips already seen follow what their tooltip shows. True
		// while one of those tooltips is still on screen.
		bool FollowRows(IPluginObjectProperties* properties)
		{
			bool onScreen = false;
			for (auto it = g_rows.begin(); it != g_rows.end();)
			{
				SDK::UUserWidget* tooltip = it->tooltip.Get();
				if (!tooltip)
				{
					it = g_rows.erase(it); // the tooltip is gone and its row with it
					continue;
				}
				if (tooltip->IsVisible())
				{
					onScreen = true;
					if (SDK::UUserWidget* row = it->row.Get())
					{
						const bool pinnable = !MatchProduct(TooltipTitle(properties, tooltip), TooltipTexture(properties, tooltip)).empty();
						row->SetVisibility(pinnable ? SDK::ESlateVisibility::HitTestInvisible : SDK::ESlateVisibility::Collapsed);
					}
				}
				++it;
			}
			return onScreen;
		}

		void WatchTooltips()
		{
			IPluginHooks* hooks = GetHooks();
			IPluginObjectProperties* properties = hooks ? hooks->ObjectProperties : nullptr;
			if (!properties || !CodexRecipes::IsReady())
				return;

			if (FollowRows(properties))
				return;

			if (!CursorUp())
				return;
			if (!g_cursorLogged)
			{
				g_cursorLogged = true;
				LOG_INFO("CodexTooltipHint: the cursor is up; watching for item tooltips.");
			}

			const Observation seen = Observe(properties);
			g_lookEvery = seen.shown.empty() ? kWatchSeconds : kIdleSeconds;
			if (seen.shown.empty() || PinTarget(properties, seen, false).empty())
				return;
			g_lookEvery = kWatchSeconds;

			for (SDK::UUserWidget* tooltip : seen.shown)
			{
				const ObjectRef<SDK::UUserWidget> ref(tooltip);
				if (std::any_of(g_rows.begin(), g_rows.end(), [&](const HintRow& r) { return r.tooltip == ref; }))
					continue;

				HintRow entry;
				entry.tooltip = ref;
				if (const char* why = AddRow(properties, tooltip, entry))
					HintProblem(why);
				g_rows.push_back(entry); // kept even when it failed, so this tooltip is not tried again
			}
		}

		// Takes the rows out of tooltips that are still alive and gives their
		// prompts back the visibility they had. Game thread only.
		void RemoveRows()
		{
			for (HintRow& entry : g_rows)
			{
				if (SDK::UUserWidget* row = entry.row.Get())
					row->RemoveFromParent();
				if (entry.restorePrompt)
					if (SDK::UUserWidget* prompt = entry.prompt.Get())
						prompt->SetVisibility(entry.promptWas);
				if (entry.restoreOverlay)
					if (SDK::UWidget* overlay = entry.overlay.Get())
						overlay->SetVisibility(entry.overlayWas);
			}
			g_rows.clear();
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

	// On the game thread, from the engine tick.
	void Tick(float deltaSeconds)
	{
		g_sinceLook += deltaSeconds;
		if (g_sinceLook < g_lookEvery)
			return;
		g_sinceLook = 0.0f;

		try
		{
			WatchTooltips();
		}
		catch (...)
		{
			LOG_WARN("CodexTooltipHint: watching the tooltips failed.");
		}
	}

	void Shutdown(IPluginSelf* self)
	{
		// A RELOAD runs this on the game thread. At process exit it may not, and
		// then the rows go with the game's own widgets.
		if (self && self->hooks->Engine && self->hooks->Engine->IsGameThread && self->hooks->Engine->IsGameThread())
		{
			try
			{
				RemoveRows();
			}
			catch (...)
			{
				LOG_WARN("CodexTooltipHint: taking the rows out failed.");
			}
		}

		if (self && self->hooks->Input && self->hooks->UI)
			self->hooks->Input->UnregisterKeybindByName(CodexConfig::Config::GetGamePinKey(), EModKeyEvent::Pressed, &OnPinKeyPressed);
	}
}
