#include "game_tooltip_pin.h"
#include "codex_ui.h"
#include "Icons/codex_icons.h"
#include "Recipes/codex_recipes.h"
#include "Planner/codex_planner.h"
#include "Plugin Core/Helpers/plugin_helpers.h"
#include "Plugin Core/Config/plugin_config.h"
#include "Plugin Core/codex_fields.h"

#include "ChimeraUI_classes.hpp"
#include "BP_CommonInput_KeyboardMouse_classes.hpp"
#include "WBP_ActionButton_classes.hpp"
#include "object_ref.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <mutex>
#include <string>
#include <vector>

namespace CodexTooltipPin
{
	namespace
	{
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


		// What one look at the screen found. The pointers are good for the tick
		// that made it and no longer.
		struct Observation
		{
			std::vector<SDK::UUserWidget*> all;   // every item tooltip the engine lists as alive
			std::vector<SDK::UUserWidget*> shown; // those on screen
		};

		// Every widget of a class (or below it) that the engine's own widget search
		// lists. That search leaves out objects the garbage collector has marked
		// unreachable, and the engine asserts, fatally, on a call to one (ProcessEvent
		// "marked unreachable"). A closed tooltip is exactly that: nothing holds it
		// once its popup is gone. So no widget is called through a pointer that did
		// not come out of this function in the same tick, and what is remembered
		// across ticks is only an ObjectRef, compared and never called.
		std::vector<SDK::UUserWidget*> LiveWidgets(SDK::UClass* widgetClass)
		{
			std::vector<SDK::UUserWidget*> live;
			SDK::UWorld* world = PluginSdkRuntime::GetWorld();
			if (!world || !widgetClass)
				return live;

			constexpr uint32_t kDeadFlags =
				static_cast<uint32_t>(SDK::EObjectFlags::BeginDestroyed) |
				static_cast<uint32_t>(SDK::EObjectFlags::FinishDestroyed) |
				static_cast<uint32_t>(SDK::EObjectFlags::MirroredGarbage);

			SDK::TArray<SDK::UUserWidget*> found;
			SDK::UWidgetBlueprintLibrary::GetAllWidgetsOfClass(world, &found, widgetClass, false);
			SDK::UUserWidget* const* data = found.GetDataPtr();
			for (int i = 0; i < found.Num(); ++i)
				if (data[i] && !(static_cast<uint32_t>(data[i]->Flags) & kDeadFlags))
					live.push_back(data[i]);

			// The engine allocated the array's buffer and the SDK's TArray does not
			// free it; this runs four times a second, so give it back.
			IPluginHooks* hooks = GetHooks();
			if (found.GetDataPtr() && hooks && hooks->Memory && hooks->Memory->Free)
				hooks->Memory->Free(const_cast<SDK::UUserWidget**>(found.GetDataPtr()));
			return live;
		}

		bool Lists(const std::vector<SDK::UUserWidget*>& live, const ObjectRef<SDK::UUserWidget>& ref)
		{
			return std::any_of(live.begin(), live.end(), [&](SDK::UUserWidget* w) { return ObjectRef<SDK::UUserWidget>(w) == ref; });
		}

		// The item tooltips alive now, and which of them are on screen (the Slate
		// widget of a tooltip whose popup closed is gone, and IsVisible is false).
		Observation Observe()
		{
			Observation seen;
			seen.all = LiveWidgets(SDK::UCr_UW_InventoryToolTip::StaticClass());
			for (SDK::UUserWidget* tooltip : seen.all)
				if (tooltip->IsVisible())
					seen.shown.push_back(tooltip);
			return seen;
		}

		// The class of the inventory slot whose tooltip is open, or empty when
		// none is. Right-click is the game's own action on an inventory slot, and
		// the Codex's pin and its hint stay off those tooltips; a recipe list or a
		// machine's crafting entries are not slots of these classes. Only the open
		// popup counts, so this reads the slots' own TooltipAnchor, not the screen.
		std::string OpenInventorySlot(IPluginObjectProperties* properties)
		{
			for (SDK::UClass* slotClass : { SDK::UCrUW_InventorySlot::StaticClass(), SDK::UCrUW_WeaponSlot::StaticClass() })
			{
				for (SDK::UUserWidget* slot : LiveWidgets(slotClass))
				{
					SDK::UMenuAnchor** anchor = CodexFields::SlotTooltipAnchor.In(properties, slot);
					if (anchor && *anchor && (*anchor)->IsOpen())
						return slot->Class ? slot->Class->GetName() : std::string("inventory slot");
				}
			}
			return {};
		}

		// What the pin or the planner key would act on: the product's unique name
		// and the name the tooltip shows for it. Empty when there is none.
		struct Target
		{
			std::string item;
			std::string title;
		};

		// The item the pin would act on given what is open, or empty when it
		// would do nothing: no tooltip open, it belongs to an inventory slot, or
		// what it shows is not something a recipe makes.
		Target PinTarget(IPluginObjectProperties* properties, const Observation& seen)
		{
			if (seen.shown.empty())
			{
				LOG_DEBUG("CodexTooltipPin: no item tooltip is open.");
				return {};
			}

			const std::string slot = OpenInventorySlot(properties);
			if (!slot.empty())
			{
				LOG_INFO("CodexTooltipPin: the tooltip is an inventory slot's ('%s'), where right-click is the game's own; nothing pinned.", slot.c_str());
				return {};
			}

			Target found;
			for (SDK::UUserWidget* tooltip : seen.shown)
			{
				const std::string title = TooltipTitle(properties, tooltip);
				const std::string item = MatchProduct(title, TooltipTexture(properties, tooltip));
				if (item.empty())
				{
					LOG_INFO("CodexTooltipPin: the tooltip's item '%s' is not one a recipe makes; nothing pinned.", title.c_str());
					continue;
				}
				if (!found.item.empty() && found.item != item)
				{
					LOG_INFO("CodexTooltipPin: %d item tooltips are up at once; nothing pinned.", static_cast<int>(seen.shown.size()));
					return {};
				}
				found = { item, title };
			}
			return found;
		}

		// The item in the game's open item tooltip, or empty.
		Target ReadOpenTarget()
		{
			IPluginHooks* hooks = GetHooks();
			IPluginObjectProperties* properties = hooks ? hooks->ObjectProperties : nullptr;
			if (!properties)
				return {};

			return PinTarget(properties, Observe());
		}

		// ---- The hint: "Pin to Codex" and "Open in Planner" rows in the tooltip's body ----
		//
		// The tooltip (WBP_ToolTipWidget_C) lays its text out in a vertical list
		// that holds the header, the description and "Crafted in". The row is the
		// game's own WBP_ActionButton_C, appended to that list, so it sits at the
		// foot of the text with the list's own spacing and the tooltip grows to
		// hold it. The game's action prompt, which hangs below the tooltip and
		// has its own native rows, is left as it is. The key's icon comes from the
		// game's keyboard-and-mouse input data. One row per tooltip, taken out
		// again on unload.

		constexpr float kWatchSeconds = 0.25f; // between looks for a new tooltip

		struct HintRow
		{
			ObjectRef<SDK::UUserWidget> tooltip; // compared, never called
			ObjectRef<SDK::UUserWidget> row;        // "Pin to Codex"
			ObjectRef<SDK::UUserWidget> plannerRow; // "Open in Planner"
		};

		std::vector<HintRow> g_rows;
		float g_sinceLook = 0.0f;
		int g_problemLogs = 0;
		int g_rowLogs = 0;
		int g_slotLogs = 0;
		bool g_cursorLogged = false;
		std::atomic<bool> g_cursorUp{ false }; // for the render thread

		void HintProblem(const std::string& what)
		{
			if (g_problemLogs++ < 5)
				LOG_INFO("CodexTooltipHint: no row added: %s.", what.c_str());
			else
				LOG_DEBUG("CodexTooltipHint: no row added: %s.", what.c_str());
		}

		// A key binding as a row names it: its game key when it is a single key,
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

		KeyLabel LabelOf(const std::string& binding)
		{
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
		// Common UI action widgets read their key icons from: the per-key list
		// first, then the lists of keys that share a brush, the order Common UI
		// itself looks them up in. Null when the game has none for it, and why.
		const SDK::FSlateBrush* GameKeyBrush(const std::string& gameKey, std::string& why)
		{
			if (gameKey.empty())
			{
				why = "the binding is more than one key";
				return nullptr;
			}
			SDK::UBP_CommonInput_KeyboardMouse_C* data = SDK::UBP_CommonInput_KeyboardMouse_C::GetDefaultObj();
			if (!data)
			{
				why = "the keyboard-and-mouse input data is not loaded";
				return nullptr;
			}
			for (int i = 0; i < data->InputBrushDataMap.Num(); ++i)
			{
				const SDK::FCommonInputKeyBrushConfiguration& entry = data->InputBrushDataMap[i];
				if (EqualsNoCase(entry.Key.KeyName.ToString(), gameKey))
					return &entry.KeyBrush;
			}
			for (int i = 0; i < data->InputBrushKeySets.Num(); ++i)
			{
				const SDK::FCommonInputKeySetBrushConfiguration& set = data->InputBrushKeySets[i];
				for (int k = 0; k < set.Keys.Num(); ++k)
					if (EqualsNoCase(set.Keys[k].KeyName.ToString(), gameKey))
						return &set.KeyBrush;
			}
			why = "the input data lists no brush for '" + gameKey + "' (" +
				std::to_string(data->InputBrushDataMap.Num()) + " single keys, " +
				std::to_string(data->InputBrushKeySets.Num()) + " key sets)";
			return nullptr;
		}

		// The vertical list the tooltip's text is laid out in: the parent of its
		// header line, else of its "Crafted in" box. Null when neither is one.
		SDK::UPanelWidget* TooltipBody(IPluginObjectProperties* properties, SDK::UUserWidget* tooltip, std::string& seen)
		{
			SDK::UWidget* anchors[2] = {};
			SDK::UUserWidget** header = CodexFields::TooltipInfoWidget.In(properties, tooltip);
			anchors[0] = header ? *header : nullptr;
			SDK::USizeBox** crafted = CodexFields::TooltipCraftedIn.In(properties, tooltip);
			anchors[1] = crafted ? *crafted : nullptr;

			for (SDK::UWidget* anchor : anchors)
			{
				SDK::UPanelWidget* parent = anchor ? anchor->GetParent() : nullptr;
				if (!parent)
					continue;
				if (parent->IsA(SDK::UVerticalBox::StaticClass()))
					return parent;
				seen += (seen.empty() ? "" : ", ") + (parent->Class ? parent->Class->GetName() : std::string("?"));
			}
			return nullptr;
		}

		// Adds one action row to the tooltip's text list, set up like the text
		// rows around it; empty on success, else why it could not.
		std::string AddOneRow(IPluginObjectProperties* properties, SDK::UUserWidget* tooltip, SDK::UPanelWidget* body,
			SDK::UWidget* sibling, const std::string& binding, const char* action, ObjectRef<SDK::UUserWidget>& out)
		{
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

			// Attached first: the game's own construct code runs on attach and
			// would otherwise overwrite what is set below.
			row->SetVisibility(SDK::ESlateVisibility::HitTestInvisible);
			SDK::UPanelSlot* placed = body->AddChild(row);
			out.Set(row);

			// Laid out as the text rows beside it are: the slot's padding, size and
			// alignment are the neighbour's own.
			std::string slotNote = "no neighbouring slot to copy";
			SDK::UPanelSlot* theirs = sibling ? sibling->Slot : nullptr;
			if (placed && theirs && placed->IsA(SDK::UVerticalBoxSlot::StaticClass()) && theirs->IsA(SDK::UVerticalBoxSlot::StaticClass()))
			{
				SDK::UVerticalBoxSlot* mine = static_cast<SDK::UVerticalBoxSlot*>(placed);
				SDK::UVerticalBoxSlot* neighbour = static_cast<SDK::UVerticalBoxSlot*>(theirs);
				mine->SetPadding(neighbour->Padding);
				mine->SetSize(neighbour->Size);
				mine->SetHorizontalAlignment(neighbour->HorizontalAlignment);
				mine->SetVerticalAlignment(neighbour->VerticalAlignment);
				char padding[96];
				snprintf(padding, sizeof(padding), "slot copied (padding %g/%g/%g/%g)",
					neighbour->Padding.Left, neighbour->Padding.Top, neighbour->Padding.Right, neighbour->Padding.Bottom);
				slotNote = padding;
			}

			// The key's own icon where the binding is one key the game has an icon
			// for, set through the button's own SetActionIcon; where the button
			// shows no icon image (or the game has none) the label names the binding.
			const KeyLabel key = LabelOf(binding);
			std::string why;
			const SDK::FSlateBrush* brush = GameKeyBrush(key.gameKey, why);
			SDK::UImage** iconSlot = CodexFields::ButtonActionIcon.In(properties, row);
			const bool iconShown = brush && iconSlot && *iconSlot;
			if (brush && !iconShown)
				why = "the action button has no icon image";

			std::string label = action;
			if (!iconShown && !key.spelled.empty())
				label = key.spelled + ": " + label;
			const std::wstring wide(label.begin(), label.end());
			name->SetText(SDK::UKismetTextLibrary::Conv_StringToText(SDK::FString(wide.c_str())));

			if (brush)
			{
				SDK::UCrUW_ActionButton* button = static_cast<SDK::UCrUW_ActionButton*>(row);
				button->SetActionIcon(*brush);
				button->SetInputActionWidgetVisibility(false);
			}

			// The first few rows are logged at INFO for the in-game check, the rest
			// at DEBUG: rows are added per hover.
			const char* shape = iconShown ? "game icon" : why.c_str();
			const std::string bodyClass = body->Class ? body->Class->GetName() : std::string("?");
			if (g_rowLogs++ < 6)
				LOG_INFO("CodexTooltipHint: row '%s' added (key '%s', %s, %s, in a %s).", action, key.spelled.c_str(), shape, slotNote.c_str(), bodyClass.c_str());
			else
				LOG_DEBUG("CodexTooltipHint: row '%s' added (key '%s', %s).", action, key.spelled.c_str(), shape);
			return {};
		}

		// Adds the pin row, and the planner row when that key is set, to one tooltip;
		// empty on success, else why the pin row could not be.
		std::string AddRows(IPluginObjectProperties* properties, SDK::UUserWidget* tooltip, HintRow& entry)
		{
			std::string parents;
			SDK::UPanelWidget* body = TooltipBody(properties, tooltip, parents);
			if (!body)
				return "the tooltip's text is not in a vertical list (found: " + (parents.empty() ? std::string("nothing") : parents) + ")";

			// The text row the new ones are laid out like: "Crafted in", else the header.
			SDK::USizeBox** crafted = CodexFields::TooltipCraftedIn.In(properties, tooltip);
			SDK::UUserWidget** header = CodexFields::TooltipInfoWidget.In(properties, tooltip);
			SDK::UWidget* sibling = (crafted && *crafted && (*crafted)->GetParent() == body) ? static_cast<SDK::UWidget*>(*crafted)
				: (header ? static_cast<SDK::UWidget*>(*header) : nullptr);

			const std::string pin = AddOneRow(properties, tooltip, body, sibling, CodexConfig::Config::GetGamePinKey(), "Pin to Codex", entry.row);
			if (!pin.empty())
				return pin;

			const std::string plannerKey = CodexConfig::Config::GetGamePlannerKey();
			if (!plannerKey.empty())
			{
				const std::string why = AddOneRow(properties, tooltip, body, sibling, plannerKey, "Open in Planner", entry.plannerRow);
				if (!why.empty())
					HintProblem("planner row: " + why);
			}
			return {};
		}

		// Whether the player's cursor is up: the only time a tooltip can be open.
		bool CursorUp()
		{
			SDK::UWorld* world = PluginSdkRuntime::GetWorld();
			SDK::APlayerController* controller = world ? SDK::UGameplayStatics::GetPlayerController(world, 0) : nullptr;
			return controller && controller->bShowMouseCursor;
		}

		void WatchTooltips(bool cursorUp)
		{
			IPluginHooks* hooks = GetHooks();
			IPluginObjectProperties* properties = hooks ? hooks->ObjectProperties : nullptr;
			if (!properties || !CodexRecipes::IsReady())
				return;

			if (!cursorUp && g_rows.empty())
				return;
			if (cursorUp && !g_cursorLogged)
			{
				g_cursorLogged = true;
				LOG_INFO("CodexTooltipHint: the cursor is up; watching for item tooltips.");
			}

			const Observation seen = Observe();

			// A row whose tooltip the engine no longer lists is forgotten as it is:
			// the tooltip may already be marked unreachable, and is never called.
			g_rows.erase(std::remove_if(g_rows.begin(), g_rows.end(),
				[&](const HintRow& r) { return !Lists(seen.all, r.tooltip); }), g_rows.end());

			if (!cursorUp)
				return;

			std::string slot;
			bool slotRead = false;
			for (SDK::UUserWidget* tooltip : seen.shown)
			{
				const ObjectRef<SDK::UUserWidget> ref(tooltip);
				if (std::any_of(g_rows.begin(), g_rows.end(), [&](const HintRow& r) { return r.tooltip == ref; }))
					continue;

				HintRow entry;
				entry.tooltip = ref;
				if (!slotRead)
				{
					slot = OpenInventorySlot(properties);
					slotRead = true;
				}

				if (!slot.empty())
				{
					if (g_slotLogs++ < 3)
						LOG_INFO("CodexTooltipHint: no row on a tooltip of the inventory slot '%s' (the game's own right-click).", slot.c_str());
				}
				else if (!MatchProduct(TooltipTitle(properties, tooltip), TooltipTexture(properties, tooltip)).empty())
				{
					const std::string why = AddRows(properties, tooltip, entry);
					if (!why.empty())
						HintProblem(why);
				}
				g_rows.push_back(entry); // kept even without a row, so this tooltip is not looked at again
			}
		}

		// Takes the rows out of the tooltips that are still alive. Game thread only.
		void RemoveRows()
		{
			const Observation seen = Observe();
			for (HintRow& entry : g_rows)
			{
				if (!Lists(seen.all, entry.tooltip))
					continue;
				if (SDK::UUserWidget* row = entry.row.Get())
					row->RemoveFromParent();
				if (SDK::UUserWidget* row = entry.plannerRow.Get())
					row->RemoveFromParent();
			}
			g_rows.clear();
		}

		// Runs from the game's window messages, on the game thread and before
		// the game handles the click, which may close the tooltip. So the item
		// is read here, and only its name is handed on.
		Target ReadTarget()
		{
			if (!CodexRecipes::IsReady())
				return {};
			try
			{
				return ReadOpenTarget();
			}
			catch (...)
			{
				LOG_WARN("CodexTooltipPin: reading the open tooltip failed.");
				return {};
			}
		}

		// The key that opens the planner, when set; a pin key without modifiers
		// also fires while another key is held, so the pin steps aside for it.
		bool PlannerKeyHeld()
		{
			IPluginHooks* hooks = GetHooks();
			const std::string key = CodexConfig::Config::GetGamePlannerKey();
			return !key.empty() && hooks && hooks->Input && hooks->Input->IsKeyHeld && hooks->Input->IsKeyHeld(key.c_str());
		}

		void OnPinKeyPressed(EModKey /*key*/, EModKeyEvent event)
		{
			if (event != EModKeyEvent::Pressed || PlannerKeyHeld())
				return;

			const Target target = ReadTarget();
			if (target.item.empty())
				return;

			LOG_INFO("CodexTooltipPin: pin asked for '%s' from the game's tooltip (skipped if a pin already makes it).", target.item.c_str());
			CodexUI::PinProductOf(target.item);
		}

		// The item the planner key was asked to open; the browser is started from
		// the tick, not from inside the window message. A keybind callback and the
		// tick may not share a thread, so the string is behind a mutex.
		std::mutex  g_plannerAskLock;
		std::string g_plannerAskItem;

		std::string TakePlannerAsk()
		{
			std::lock_guard<std::mutex> lock(g_plannerAskLock);
			std::string item;
			item.swap(g_plannerAskItem);
			return item;
		}

		void OnPlannerKeyPressed(EModKey /*key*/, EModKeyEvent event)
		{
			if (event != EModKeyEvent::Pressed)
				return;

			const Target target = ReadTarget();
			if (target.item.empty())
				return;

			LOG_INFO("CodexTooltipPin: planner asked for '%s' from the game's tooltip.", target.item.c_str());
			std::lock_guard<std::mutex> lock(g_plannerAskLock);
			g_plannerAskItem = target.item;
		}
	}

	bool GameCursorUp()
	{
		return g_cursorUp.load(std::memory_order_relaxed);
	}

	void Init(IPluginSelf* self)
	{
		if (!self->hooks->Input || !self->hooks->UI)
			return;

		const char* key = CodexConfig::Config::GetGamePinKey();
		LOG_DEBUG("CodexTooltipPin: registering game-menu pin keybind '%s'", key);
		self->hooks->Input->RegisterKeybindByName(key, EModKeyEvent::Pressed, &OnPinKeyPressed);

		const std::string plannerKey = CodexConfig::Config::GetGamePlannerKey();
		if (!plannerKey.empty())
		{
			LOG_DEBUG("CodexTooltipPin: registering game-menu planner keybind '%s'", plannerKey.c_str());
			self->hooks->Input->RegisterKeybindByName(plannerKey.c_str(), EModKeyEvent::Pressed, &OnPlannerKeyPressed);
		}
	}

	// On the game thread, from the engine tick.
	void Tick(float deltaSeconds)
	{
		// The cursor flag is read here for the render thread (the pinned cards
		// are editable while a game screen has the cursor up); the objects are
		// only touched on this thread.
		bool cursorUp = false;
		try
		{
			cursorUp = CursorUp();
		}
		catch (...)
		{
			// A failed read counts as no cursor; the watch below logs its own failures.
		}
		g_cursorUp.store(cursorUp, std::memory_order_relaxed);

		const std::string askedItem = TakePlannerAsk();
		if (!askedItem.empty())
		{
			// The recipe's own display name, as the Planner button passes it.
			for (const CodexRecipes::RecipeInfo& recipe : CodexRecipes::GetAll())
			{
				if (recipe.output.uniqueItemName == askedItem)
				{
					CodexPlanner::Launch(askedItem, recipe.output.displayName);
					break;
				}
			}
		}

		g_sinceLook += deltaSeconds;
		if (g_sinceLook < kWatchSeconds)
			return;
		g_sinceLook = 0.0f;

		try
		{
			WatchTooltips(cursorUp);
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

		g_cursorUp.store(false, std::memory_order_relaxed);
		TakePlannerAsk();

		if (self && self->hooks->Input && self->hooks->UI)
		{
			self->hooks->Input->UnregisterKeybindByName(CodexConfig::Config::GetGamePinKey(), EModKeyEvent::Pressed, &OnPinKeyPressed);
			const std::string plannerKey = CodexConfig::Config::GetGamePlannerKey();
			if (!plannerKey.empty())
				self->hooks->Input->UnregisterKeybindByName(plannerKey.c_str(), EModKeyEvent::Pressed, &OnPlannerKeyPressed);
		}
	}
}
