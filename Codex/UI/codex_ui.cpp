#include "codex_ui.h"
#include "Recipes/codex_recipes.h"
#include "Planner/codex_planner.h"
#include "Icons/codex_icons.h"
#include "Plugin Core/Helpers/plugin_helpers.h"
#include "Plugin Core/Config/plugin_config.h"

#include "Engine_classes.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace CodexUI
{
	namespace
	{
		IPluginSelf* g_self = nullptr;

		WidgetHandle g_searchWidget = nullptr;
		PanelHandle  g_detailPanel = nullptr;

		PluginWindowHints g_searchHints{};
		PluginWidgetDesc  g_searchDesc{};
		PluginPanelDesc   g_detailDesc{};

		// Invisible one-frame widget that performs the detail panel's close -
		// see RequestCloseDetail.
		WidgetHandle      g_closerWidget = nullptr;
		PluginWindowHints g_closerHints{};
		PluginWidgetDesc  g_closerDesc{};

		void* g_inputCaptureToken = nullptr;
		bool  g_escapeRegistered = false;

		bool  g_searchVisible = false;
		bool  g_detailVisible = false;
		bool  g_justOpenedSearch = false;
		char  g_searchBuffer[256] = "";

		// Search box placeholder, naming the full-Codex key. Rebuilt each time
		// the search opens so a rebind in the config is picked up.
		std::string g_searchHint = "Search recipes and buildings...";
		void* g_selectedNativeRecipe = nullptr;

		// ImGui frame the detail panel last rendered on - lets pinned windows
		// tell whether the Codex UI (and so the mouse) is currently up.
		int   g_detailLastFrame = -1000;

		// Row of the search dropdown that Enter opens, counting recipes first,
		// then buildings. Reset to the top result by every new search; moved
		// by the Up/Down keys and by the mouse.
		int g_searchHighlight = 0;

		// Last mouse position the search box saw, so a resting cursor doesn't
		// fight the keyboard for the highlight.
		float g_searchMouseX = 0.0f;
		float g_searchMouseY = 0.0f;

		// Search box placement. The loader re-applies a widget's position
		// hints every frame, which would pin the box in place, so the box is
		// placed for a frame after opening and then left to ImGui so it can
		// be dragged. g_searchPos* is the top-left corner it was last placed
		// or dropped at; a drop that changes it is saved to the config.
		// g_searchSaved* is that saved spot as screen fractions (negative =
		// centred), kept here so a Clear on the config page applies at once.
		int   g_searchPlaceFrames = 0;
		bool  g_searchPlaced = false;
		float g_searchPosX = 0.0f;
		float g_searchPosY = 0.0f;
		float g_searchSavedX = -1.0f;
		float g_searchSavedY = -1.0f;

		// Last display size a Codex window rendered on - places the search
		// box and pins on the frame they appear, before they can query it.
		float g_displayW = 1920.0f;
		float g_displayH = 1080.0f;

		// Set when the user clicks an output item; drives the "used in..."
		// popup listing every recipe that consumes that item as an input.
		std::string g_consumersItemName;
		std::string g_consumersItemDisplayName;
		bool        g_openConsumersPopup = false;

		// Detail panel tabs. g_requestedTab forces a tab on the next frame
		// (e.g. clicking a recipe in the building browser jumps to Recipe).
		enum class DetailTab { None, Recipe, Buildings };
		DetailTab g_requestedTab = DetailTab::None;

		// Building browser selection, by BuildingGroup::displayName so it
		// survives a recipe rescan. g_requestedTier forces a tier tab.
		std::string g_selectedBuildingGroup;
		int         g_requestedTier = 0;

		// ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap
		// Used for the (single-column) search result rows only.
		constexpr int kSelectableFlags = (1 << 1) | (1 << 4);

		// ImGuiSelectableFlags_AllowOverlap - deliberately WITHOUT
		// SpanAllColumns, since these rows live inside multi-column tables and
		// SpanAllColumns would make a click in either column's row cover the
		// whole table width, triggering whichever item happened to be drawn
		// underneath in the other column.
		constexpr int kItemSelectableFlags = (1 << 4);

		constexpr int kColumnFlagsWidthStretch = 1 << 3;
		constexpr int kTabItemFlagsSetSelected = 1 << 1;
		constexpr int kWindowFlagsAlwaysAutoResize = 1 << 6;
		constexpr int kWindowFlagsNoBringToFrontOnFocus = 1 << 13;
		constexpr int kMouseCursorResizeAll = 2;

		void OnEscapePressed(EModKey key, EModKeyEvent event);
		void OnSearchKeyPressed(EModKey key, EModKeyEvent event);
		void OnCodexKeyPressed(EModKey key, EModKeyEvent event);
		void CloseSearch();
		void CloseDetail();
		void OnDetailPanelClosed(PanelHandle handle);
		void PlaceSearchBox(float fontScale);
		void PinRecipe(IModLoaderImGui* imgui, void* nativeRecipe);
		void ArrangePins(IModLoaderImGui* imgui);
		bool IsPinned(void* nativeRecipe);
		bool HasFreePinSlot();
		bool AnyPinned();

		std::string ToLower(const std::string& s)
		{
			std::string out = s;
			std::transform(out.begin(), out.end(), out.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		// Rows are keyed by native recipe pointer rather than display name:
		// tiered recipes (e.g. Titanium Rod in Fabricator Tier 1 and Tier 2)
		// share a name, and ImGui would otherwise see duplicate IDs.
		void PushIDPtr(IModLoaderImGui* imgui, const void* ptr)
		{
			const auto bits = reinterpret_cast<uintptr_t>(ptr);
			imgui->PushIDInt(static_cast<int>(bits ^ (bits >> 32)));
		}

		bool InChimeraMain()
		{
			SDK::UWorld* world = SDK::UWorld::GetWorld();
			return world && world->GetName() == "ChimeraMain";
		}

		void AcquireCapture()
		{
			if (g_inputCaptureToken)
				return;
			IPluginHooks* hooks = GetHooks();
			if (hooks && hooks->UI)
				g_inputCaptureToken = hooks->UI->AcquireInputCapture();
		}

		void ReleaseCaptureIfIdle()
		{
			if (g_searchVisible || g_detailVisible)
				return;
			if (!g_inputCaptureToken)
				return;
			IPluginHooks* hooks = GetHooks();
			if (hooks && hooks->UI)
				hooks->UI->ReleaseInputCapture(g_inputCaptureToken);
			g_inputCaptureToken = nullptr;
		}

		// Escape should close whichever Codex window is open, so both
		// OpenSearch and OpenDetail register it - reference-counted via
		// g_escapeRegistered so the first one in / last one out owns it.
		// Pinned windows deliberately don't: they stay up until unpinned.
		void AcquireEscape()
		{
			if (g_escapeRegistered)
				return;
			if (g_self && g_self->hooks->Input)
				g_self->hooks->Input->RegisterKeybindByName("Escape", EModKeyEvent::Pressed, &OnEscapePressed);
			g_escapeRegistered = true;
		}

		void ReleaseEscapeIfIdle()
		{
			if (g_searchVisible || g_detailVisible)
				return;
			if (!g_escapeRegistered)
				return;
			if (g_self && g_self->hooks->Input)
				g_self->hooks->Input->UnregisterKeybindByName("Escape", EModKeyEvent::Pressed, &OnEscapePressed);
			g_escapeRegistered = false;
		}

		void OpenSearch()
		{
			if (g_searchVisible)
				return;

			g_searchVisible = true;
			g_searchBuffer[0] = '\0';
			g_justOpenedSearch = true;
			g_searchHighlight = 0;
			g_searchPlaceFrames = 1;
			g_searchPlaced = false;
			PlaceSearchBox(1.0f);
			g_searchHint = std::string("Search recipes and buildings (") +
				CodexConfig::Config::GetCodexKey() + " opens full Codex)";

			if (g_self && g_self->hooks->UI && g_searchWidget)
				g_self->hooks->UI->SetWidgetVisible(g_searchWidget, true);

			AcquireEscape();
			AcquireCapture();
		}

		void CloseSearch()
		{
			if (!g_searchVisible)
				return;

			g_searchVisible = false;

			if (g_self && g_self->hooks->UI && g_searchWidget)
				g_self->hooks->UI->SetWidgetVisible(g_searchWidget, false);

			ReleaseEscapeIfIdle();
			ReleaseCaptureIfIdle();
		}

		void ShowDetailPanel()
		{
			if (g_detailVisible)
				return;

			g_detailVisible = true;
			if (g_self && g_self->hooks->UI && g_detailPanel)
				g_self->hooks->UI->SetPanelOpen(g_detailPanel);
			AcquireEscape();
			AcquireCapture();
		}

		void OpenDetail(void* nativeRecipe)
		{
			g_selectedNativeRecipe = nativeRecipe;
			g_requestedTab = DetailTab::Recipe;
			ShowDetailPanel();
		}

		// Opens the building browser on a group, optionally jumping to a
		// specific tier tab (0 = leave the tier selection alone).
		void OpenBuilding(const std::string& groupName, int tier)
		{
			g_selectedBuildingGroup = groupName;
			g_requestedTier = tier;
			g_requestedTab = DetailTab::Buildings;
			ShowDetailPanel();
		}

		// Requests the host close the panel right now. This runs the shared
		// cleanup via OnDetailPanelClosed (fired synchronously by
		// SetPanelClose), the same path taken when the user clicks the
		// window's own X button - there is only one place that resets our
		// state.
		//
		// Must NOT be called from inside the panel's own render callback (or
		// from another thread while it may be rendering): the host snapshots
		// the panel's open flag before calling renderFn and writes it back
		// afterwards, silently undoing the close. Use CloseDetail() instead.
		void CloseDetailNow()
		{
			if (!g_detailVisible)
				return;

			if (g_self && g_self->hooks->UI && g_detailPanel)
				g_self->hooks->UI->SetPanelClose(g_detailPanel);
			else
				OnDetailPanelClosed(g_detailPanel);
		}

		// Defers the close to the closer widget, which the host renders after
		// every panel on the render thread - so its SetPanelClose can't be
		// overwritten by the panel render loop.
		void CloseDetail()
		{
			if (!g_detailVisible)
				return;

			if (g_self && g_self->hooks->UI && g_closerWidget)
				g_self->hooks->UI->SetWidgetVisible(g_closerWidget, true);
			else
				CloseDetailNow();
		}

		void RenderCloserWidget(IModLoaderImGui* /*imgui*/)
		{
			if (g_self && g_self->hooks->UI && g_closerWidget)
				g_self->hooks->UI->SetWidgetVisible(g_closerWidget, false);
			CloseDetailNow();
		}

		// Fired by the host whenever the panel closes, whether from our own
		// CloseDetail() (via SetPanelClose) or the user clicking the
		// window's titlebar X directly.
		void OnDetailPanelClosed(PanelHandle handle)
		{
			if (handle != g_detailPanel || !g_detailVisible)
				return;

			g_detailVisible = false;
			g_consumersItemName.clear();
			g_detailLastFrame = -1000;

			ReleaseEscapeIfIdle();
			ReleaseCaptureIfIdle();
		}

		void OnEscapePressed(EModKey /*key*/, EModKeyEvent event)
		{
			if (event != EModKeyEvent::Pressed)
				return;
			if (g_searchVisible)
				CloseSearch();
			else if (g_detailVisible)
				CloseDetail();
		}

		void OnSearchKeyPressed(EModKey /*key*/, EModKeyEvent event)
		{
			if (event != EModKeyEvent::Pressed)
				return;
			if (g_searchVisible)
				return;

			if (!InChimeraMain())
			{
				LOG_DEBUG("CodexUI: search key ignored (not in ChimeraMain)");
				return;
			}

			OpenSearch();
		}

		// Toggles the full Codex window. Opens on the last recipe viewed, or
		// the Buildings tab if nothing has been picked yet. The mod loader
		// gives an exact modifier match (Shift+N) priority over a plain bind
		// on the same key (N), so this never also opens the search box.
		void OnCodexKeyPressed(EModKey /*key*/, EModKeyEvent event)
		{
			if (event != EModKeyEvent::Pressed)
				return;

			if (g_detailVisible)
			{
				CloseDetail();
				return;
			}

			if (!InChimeraMain())
			{
				LOG_DEBUG("CodexUI: codex key ignored (not in ChimeraMain)");
				return;
			}

			CloseSearch();
			g_requestedTab = g_selectedNativeRecipe ? DetailTab::Recipe : DetailTab::Buildings;
			ShowDetailPanel();
		}

		// Height of a row showing an icon beside a name + sub-line (the
		// sub-line is pulled up 4px, see RenderItemRow). Sized from whichever
		// is taller, so larger UI font scales don't push the sub-line past the
		// row's clickable area.
		float TwoLineRowHeight(IModLoaderImGui* imgui, float iconSize)
		{
			const float textHeight = imgui->GetTextLineHeightWithSpacing() + imgui->GetTextLineHeight() - 4.0f;
			return (std::max)(iconSize, textHeight) + 4.0f;
		}

		// Icon (if loaded) + name, as one line. Shared by every recipe list.
		void RenderRecipeLabel(IModLoaderImGui* imgui, IPluginImGuiTextures* textures,
			const CodexRecipes::RecipeInfo& recipe, float iconSize)
		{
			PluginTextureHandle icon = CodexIcons::GetIcon(recipe.output.uniqueItemName);
			if (icon && textures)
			{
				textures->Image(icon, iconSize, iconSize);
				imgui->SameLine(0.0f, 6.0f);
			}
			imgui->Text(recipe.displayName.c_str());
		}

		// Renders one ingredient/output row: icon + name + count (and rate,
		// if known). Clicking an input row jumps to the recipe that
		// produces that input, if one is known. Clicking the output row
		// instead opens a popup listing every recipe that consumes it, so
		// the user can browse "what uses this?" before navigating.
		void RenderItemRow(IModLoaderImGui* imgui, IPluginImGuiTextures* textures,
			const CodexRecipes::RecipeItemRef& item, float iconSize, float ratePerMinute, bool isOutput)
		{
			imgui->PushIDStr(item.uniqueItemName.c_str());

			// Look the icon up live (rather than trusting item.icon, which was
			// snapshotted at recipe-scan time and may have been null if the
			// icon scan hadn't finished loading textures yet).
			PluginTextureHandle icon = CodexIcons::GetIcon(item.uniqueItemName);

			bool clicked = imgui->SelectableFull("##item_row", false, kItemSelectableFlags, 0.0f, TwoLineRowHeight(imgui, iconSize));
			imgui->SameLine(0.0f, 0.0f);

			if (icon && textures)
			{
				textures->Image(icon, iconSize, iconSize);
				imgui->SameLine(0.0f, 6.0f);
			}

			imgui->BeginGroup();
			imgui->Text(item.displayName.c_str());

			char sub[64];
			if (ratePerMinute > 0.0f)
				snprintf(sub, sizeof(sub), "x%d  (%.1f/min)", item.count, ratePerMinute);
			else
				snprintf(sub, sizeof(sub), "x%d", item.count);

			// Pull the sub-line up closer to the name; the default item
			// spacing leaves it too low, bleeding past the row's bottom edge.
			imgui->SetCursorPosY(imgui->GetCursorPosY() - 4.0f);
			imgui->TextDisabled(sub);
			imgui->EndGroup();

			imgui->PopID();

			if (clicked)
			{
				if (isOutput)
				{
					g_consumersItemName        = item.uniqueItemName;
					g_consumersItemDisplayName = item.displayName;
					g_openConsumersPopup       = true;
				}
				else
				{
					CodexRecipes::RecipeInfo producer;
					if (CodexRecipes::FindProducerOfItem(item.uniqueItemName, producer) &&
						producer.nativeRecipe != g_selectedNativeRecipe)
					{
						OpenDetail(producer.nativeRecipe);
					}
				}
			}
		}

		// Renders the "used in..." popup opened from clicking an output
		// item: a scrollable list of every recipe that consumes it, letting
		// the user jump straight to one.
		void RenderConsumersPopup(IModLoaderImGui* imgui, IPluginImGuiTextures* textures)
		{
			if (g_openConsumersPopup)
			{
				imgui->OpenPopup("##consumers_popup", 0);
				g_openConsumersPopup = false;
			}

			if (!imgui->BeginPopup("##consumers_popup", 0))
				return;

			char title[128];
			snprintf(title, sizeof(title), "Used in (%s)", g_consumersItemDisplayName.c_str());
			imgui->Text(title);
			imgui->Separator();

			std::vector<CodexRecipes::RecipeInfo> consumers = CodexRecipes::FindConsumersOfItem(g_consumersItemName);
			if (consumers.empty())
			{
				imgui->TextDisabled("Not used by any known recipe.");
			}
			else if (imgui->BeginChild("##consumers_list", 320.0f, 240.0f, false))
			{
				for (const CodexRecipes::RecipeInfo& consumer : consumers)
				{
					PushIDPtr(imgui, consumer.nativeRecipe);
					bool clicked = imgui->SelectableFull("##consumer_row", false, kSelectableFlags, 0.0f, 22.0f);
					imgui->SameLine(0.0f, 0.0f);
					RenderRecipeLabel(imgui, textures, consumer, 18.0f);
					if (consumer.hasTierVariants)
					{
						char tierLabel[32];
						snprintf(tierLabel, sizeof(tierLabel), "(Tier %d)", consumer.minTier);
						imgui->SameLine(0.0f, 6.0f);
						imgui->TextDisabled(tierLabel);
					}
					imgui->PopID();

					if (clicked)
					{
						imgui->CloseCurrentPopup();
						OpenDetail(consumer.nativeRecipe);
					}
				}
				imgui->EndChild();
			}

			imgui->EndPopup();
		}

		// Lets the search box double as a calculator (à la Satisfactory's
		// search bar): typing "12 * 250" shows "= 3000" beneath the box
		// instead of running a recipe search.
		namespace Calculator
		{
			struct Parser
			{
				const char* p;
				bool ok = true;

				void SkipSpace() { while (*p == ' ' || *p == '\t') ++p; }

				double ParseExpr()
				{
					double lhs = ParseTerm();
					for (;;)
					{
						SkipSpace();
						if (*p == '+') { ++p; lhs += ParseTerm(); }
						else if (*p == '-') { ++p; lhs -= ParseTerm(); }
						else break;
					}
					return lhs;
				}

				double ParseTerm()
				{
					double lhs = ParseUnary();
					for (;;)
					{
						SkipSpace();
						if (*p == '*') { ++p; lhs *= ParseUnary(); }
						else if (*p == '/')
						{
							++p;
							double rhs = ParseUnary();
							if (!ok || rhs == 0.0) { ok = false; return 0.0; }
							lhs /= rhs;
						}
						else break;
					}
					return lhs;
				}

				double ParseUnary()
				{
					SkipSpace();
					if (*p == '-') { ++p; return -ParseUnary(); }
					if (*p == '+') { ++p; return ParseUnary(); }
					return ParseAtom();
				}

				double ParseAtom()
				{
					SkipSpace();
					if (*p == '(')
					{
						++p;
						double v = ParseExpr();
						SkipSpace();
						if (*p != ')') { ok = false; return 0.0; }
						++p;
						return v;
					}

					const char* start = p;
					while ((*p >= '0' && *p <= '9') || *p == '.')
						++p;
					if (p == start) { ok = false; return 0.0; }

					return std::strtod(start, nullptr);
				}
			};

			// Cheap pre-check before running the parser: only digits,
			// arithmetic operators, parens, '.' and whitespace are allowed,
			// and there must be at least one digit - otherwise a plain
			// recipe search term like "Iron Plate" would never reach here,
			// but this keeps the rejection explicit and free of parser cost.
			bool TryEvaluate(const std::string& expr, double& outResult)
			{
				bool hasDigit = false;
				for (char c : expr)
				{
					if (c >= '0' && c <= '9') { hasDigit = true; continue; }
					if (c == '+' || c == '-' || c == '*' || c == '/' ||
						c == '(' || c == ')' || c == '.' || c == ' ' || c == '\t')
						continue;
					return false;
				}
				if (!hasDigit)
					return false;

				Parser parser{ expr.c_str() };
				double result = parser.ParseExpr();
				parser.SkipSpace();
				if (!parser.ok || *parser.p != '\0')
					return false;

				outResult = result;
				return true;
			}

			std::string FormatResult(double value)
			{
				char buf[64];
				if (std::fabs(value - std::round(value)) < 1e-9 && std::fabs(value) < 1e15)
				{
					snprintf(buf, sizeof(buf), "%.0f", value);
					return buf;
				}

				snprintf(buf, sizeof(buf), "%.4f", value);
				std::string s = buf;
				size_t last = s.find_last_not_of('0');
				if (s[last] == '.')
					--last;
				s.erase(last + 1);
				return s;
			}
		}

		// Approximate height of the search box with no results/calc output
		// shown - used to anchor the window's top edge at the same place it
		// would sit if vertically centered while collapsed. The window is
		// then pinned there by its top-left corner (pivot_y = 0), so any
		// growth from results or a calculator answer expands downward only,
		// instead of the box itself drifting as height changes.
		constexpr float kSearchBoxCollapsedHeight = 60.0f;

		// The search box is intentionally non-resizable (it auto-sizes to
		// its content every frame), so at large ImGui text-scale settings
		// its fixed 440px width would clip the input text / result rows.
		// Instead, scale the width (and the collapsed-height estimate used
		// to centre it) against the user's current font size, relative to
		// ImGui's default 13px font.
		constexpr float kBaselineFontSize = 13.0f;
		constexpr float kSearchBoxBaseWidth = 440.0f;

		// Puts the search box where the player last dropped it, or centred
		// (as if it were collapsed) when it has never been moved. The
		// position is saved as screen fractions, so it survives a resolution
		// change.
		void PlaceSearchBox(float fontScale)
		{
			const float x = g_searchSavedX, y = g_searchSavedY;
			const bool saved = x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f;

			g_searchHints.pos_x = (saved ? x : 0.5f) * g_displayW;
			g_searchHints.pos_y = saved
				? y * g_displayH
				: g_displayH * 0.5f - kSearchBoxCollapsedHeight * fontScale * 0.5f;
		}

		// Down presses minus Up presses this frame, repeating while a key is
		// held. Read through ImGui rather than registered as keybinds: the
		// modloader withholds keybinds while an ImGui text field has focus,
		// which the search field does for as long as the player is typing.
		int SearchArrowSteps(IModLoaderImGui* imgui)
		{
			if (!imgui->IsWindowFocused(0))
				return 0;
			return (imgui->IsKeyPressed(VK_DOWN, true) ? 1 : 0) - (imgui->IsKeyPressed(VK_UP, true) ? 1 : 0);
		}

		// Called straight after a result row's Selectable: a moving mouse
		// pulls the highlight to the row under it, and a row reached with
		// the arrow keys is scrolled into view.
		void SyncSearchHighlight(IModLoaderImGui* imgui, int row, bool mouseMoved, bool keyMoved)
		{
			if (mouseMoved && imgui->IsItemHovered())
				g_searchHighlight = row;
			if (keyMoved && row == g_searchHighlight)
				imgui->SetScrollHereY(0.5f);
		}

		// Shows the move cursor over the parts of a window the player can grab
		// and drag: anywhere that is not a control.
		void ShowDragCursor(IModLoaderImGui* imgui)
		{
			if (imgui->IsWindowHovered(0) && !imgui->IsAnyItemHovered())
				imgui->SetMouseCursor(kMouseCursorResizeAll);
		}

		void RenderSearchWidget(IModLoaderImGui* imgui)
		{
			const float fontScale = (std::max)(1.0f, imgui->GetFontSize() / kBaselineFontSize);
			g_searchHints.width = kSearchBoxBaseWidth * fontScale;

			imgui->GetDisplaySize(&g_displayW, &g_displayH);

			if (g_searchPlaceFrames > 0)
			{
				// The opening frame used the last known display size and font
				// scale; this one has the real ones.
				--g_searchPlaceFrames;
				PlaceSearchBox(fontScale);
			}
			else
			{
				g_searchHints.pos_x = -1.0f;
				g_searchHints.pos_y = -1.0f;

				float posX = 0.0f, posY = 0.0f, sizeX = 0.0f, sizeY = 0.0f;
				imgui->GetWindowPos(&posX, &posY);
				imgui->GetWindowSize(&sizeX, &sizeY);
				if (!g_searchPlaced)
				{
					g_searchPosX = posX;
					g_searchPosY = posY;
					g_searchPlaced = true;
				}
				else if (imgui->IsMouseReleased(0) &&
					(std::fabs(posX - g_searchPosX) > 0.5f || std::fabs(posY - g_searchPosY) > 0.5f))
				{
					g_searchPosX = posX;
					g_searchPosY = posY;
					g_searchSavedX = (posX + sizeX * 0.5f) / g_displayW;
					g_searchSavedY = posY / g_displayH;
					CodexConfig::Config::SetSearchBoxPos(g_searchSavedX, g_searchSavedY);

					// Dragging the box takes focus off the field; give it back so
					// typing and Enter keep working.
					g_justOpenedSearch = true;
				}
			}

			ShowDragCursor(imgui);

			if (g_justOpenedSearch)
			{
				imgui->SetKeyboardFocusHere(0);
				g_justOpenedSearch = false;
			}

			imgui->SetNextItemWidth(-1.0f);
			if (imgui->InputTextWithHint("##Codex_Search_Input", g_searchHint.c_str(), g_searchBuffer, sizeof(g_searchBuffer)))
				g_searchHighlight = 0;

			// Detected directly off the InputText widget rather than a
			// global "Enter" keybind: the modloader's hotkey system only
			// forwards keys once no ImGui widget has keyboard focus, and
			// typing Enter into a focused field defocuses it - so a global
			// keybind would only fire on a *second* Enter press. Reading
			// ImGui's own deactivated-after-edit flag (true the instant the
			// field submits) sidesteps that entirely.
			//
			// That flag also fires when a *click* steals focus from the box,
			// which happens on the mouse-press frame - one frame before the
			// clicked result row (a Selectable, which reports on release)
			// can return true. Left unguarded, clicking any row would close
			// the search and jump to the highlighted result instead. So
			// ignore a deactivation caused by a mouse press and let the
			// row's own click handle it.
			const bool deactivated = imgui->IsItemDeactivatedAfterEdit();
			const bool mousePressed = imgui->IsMouseClicked(0, false) || imgui->IsMouseClicked(1, false);
			const bool submitted = deactivated && !mousePressed;

			float mouseX = 0.0f, mouseY = 0.0f;
			imgui->GetMousePos(&mouseX, &mouseY);
			const bool mouseMoved = mouseX != g_searchMouseX || mouseY != g_searchMouseY;
			g_searchMouseX = mouseX;
			g_searchMouseY = mouseY;

			const int arrowSteps = SearchArrowSteps(imgui);

			// Nothing typed yet - just the search box, nothing else.
			const std::string term = ToLower(g_searchBuffer);
			if (term.empty())
				return;

			// If the box holds a valid arithmetic expression, show its
			// result instead of running a recipe search.
			double calcResult = 0.0;
			if (Calculator::TryEvaluate(g_searchBuffer, calcResult))
			{
				imgui->Separator();
				char line[64];
				snprintf(line, sizeof(line), "= %s", Calculator::FormatResult(calcResult).c_str());
				imgui->Text(line);
				return;
			}

			if (!CodexRecipes::IsReady())
				return;

			std::vector<CodexRecipes::RecipeInfo> all = CodexRecipes::GetAll();
			if (all.empty())
				return;
			std::vector<CodexRecipes::BuildingGroup> groups = CodexRecipes::GetBuildingGroups();

			IPluginImGuiTextures* textures = GetHooks() ? GetHooks()->ImGuiTextures : nullptr;

			constexpr size_t kMaxResults = 10;
			constexpr size_t kMaxBuildingResults = 3;

			std::vector<const CodexRecipes::RecipeInfo*> recipes;
			for (const CodexRecipes::RecipeInfo& recipe : all)
			{
				if (recipes.size() >= kMaxResults)
					break;
				if (ToLower(recipe.displayName).find(term) != std::string::npos)
					recipes.push_back(&recipe);
			}

			std::vector<const CodexRecipes::BuildingGroup*> buildings;
			for (const CodexRecipes::BuildingGroup& group : groups)
			{
				if (buildings.size() >= kMaxBuildingResults)
					break;
				if (ToLower(group.displayName).find(term) != std::string::npos)
					buildings.push_back(&group);
			}

			const int rowCount = static_cast<int>(recipes.size() + buildings.size());
			if (rowCount == 0)
				return;

			g_searchHighlight = std::clamp(g_searchHighlight + arrowSteps, 0, rowCount - 1);

			imgui->Separator();

			int row = 0;
			for (const CodexRecipes::RecipeInfo* recipe : recipes)
			{
				PushIDPtr(imgui, recipe->nativeRecipe);
				bool clicked = imgui->SelectableFull("##recipe_row", row == g_searchHighlight, kSelectableFlags, 0.0f, 22.0f);
				SyncSearchHighlight(imgui, row, mouseMoved, arrowSteps != 0);
				imgui->SameLine(0.0f, 0.0f);
				RenderRecipeLabel(imgui, textures, *recipe, 18.0f);

				// Tiered recipes share a name - the building tells them apart.
				imgui->SameLine(0.0f, 10.0f);
				imgui->TextDisabled(recipe->buildingName.c_str());
				imgui->PopID();

				if (clicked)
				{
					void* target = recipe->nativeRecipe;
					CloseSearch();
					OpenDetail(target);
					return;
				}

				++row;
			}

			if (!recipes.empty() && !buildings.empty())
				imgui->Separator();

			for (const CodexRecipes::BuildingGroup* group : buildings)
			{
				imgui->PushIDStr(group->displayName.c_str());
				bool clicked = imgui->SelectableFull("##building_row", row == g_searchHighlight, kSelectableFlags, 0.0f, 22.0f);
				SyncSearchHighlight(imgui, row, mouseMoved, arrowSteps != 0);
				imgui->SameLine(0.0f, 0.0f);
				imgui->Text(group->displayName.c_str());
				imgui->SameLine(0.0f, 10.0f);
				imgui->TextDisabled("Building");
				imgui->PopID();

				if (clicked)
				{
					std::string target = group->displayName;
					CloseSearch();
					OpenBuilding(target, 0);
					return;
				}

				++row;
			}

			if (submitted)
			{
				const size_t highlighted = static_cast<size_t>(g_searchHighlight);
				if (highlighted < recipes.size())
				{
					void* target = recipes[highlighted]->nativeRecipe;
					CloseSearch();
					OpenDetail(target);
				}
				else
				{
					std::string target = buildings[highlighted - recipes.size()]->displayName;
					CloseSearch();
					OpenBuilding(target, 0);
				}
			}
		}

		// The Launch planner button, shared by the recipe tab and the pinned
		// windows.
		void RenderLaunchPlannerButton(IModLoaderImGui* imgui, const CodexRecipes::RecipeInfo& info)
		{
			if (imgui->SmallButton("Launch planner"))
				CodexPlanner::Launch(info.output.uniqueItemName, info.output.displayName);
			imgui->SetItemTooltip("Open a production planner for this item in your browser.");
		}

		void RenderRecipeTab(IModLoaderImGui* imgui, IPluginImGuiTextures* textures)
		{
			CodexRecipes::RecipeInfo info;
			if (!g_selectedNativeRecipe || !CodexRecipes::FindByNativeRecipe(g_selectedNativeRecipe, info))
			{
				imgui->TextDisabled("No recipe selected. Search for one, or pick a building in the Buildings tab.");
				return;
			}

			const bool pinned = IsPinned(info.nativeRecipe);
			const bool canPin = !pinned && HasFreePinSlot();
			imgui->BeginDisabled(!canPin);
			if (imgui->SmallButton(pinned ? "Pinned" : "Pin"))
				PinRecipe(imgui, info.nativeRecipe);
			imgui->EndDisabled();
			if (!pinned && !canPin)
				imgui->SetItemTooltip("All pin slots are in use - unpin a recipe first.");
			else if (!pinned)
				imgui->SetItemTooltip("Keep this recipe on screen while you play.");

			imgui->SameLine(0.0f, -1.0f);
			RenderLaunchPlannerButton(imgui, info);

			imgui->SameLine(0.0f, 12.0f);
			imgui->SeparatorText(info.displayName.c_str());

			// "Made in: Fabricator (Tier 1), Fabricator (Tier 2)" - each
			// building is a link into the building browser.
			imgui->Text("Made in:");
			for (size_t i = 0; i < info.buildings.size(); ++i)
			{
				const CodexRecipes::BuildingRef& building = info.buildings[i];
				imgui->SameLine(0.0f, i == 0 ? 6.0f : 0.0f);
				imgui->PushIDInt(static_cast<int>(i));
				if (imgui->TextLink(building.label.c_str()))
					OpenBuilding(building.groupName, building.tier);
				imgui->PopID();
				if (i + 1 < info.buildings.size())
				{
					imgui->SameLine(0.0f, 0.0f);
					imgui->Text(", ");
				}
			}

			char line[256];
			if (info.buildTimeSeconds > 0.0f)
				snprintf(line, sizeof(line), "Craft time: %.1fs  (%.1f/min)", info.buildTimeSeconds, info.outputsPerMinute);
			else
				snprintf(line, sizeof(line), "Craft time: unknown");
			imgui->Text(line);

			imgui->Separator();

			constexpr float kIconSize = 32.0f;

			if (imgui->BeginTable("##Codex_Detail_Columns", 2, 0))
			{
				imgui->TableSetupColumn("Inputs", kColumnFlagsWidthStretch, 1.0f);
				imgui->TableSetupColumn("Outputs", kColumnFlagsWidthStretch, 1.0f);
				imgui->TableHeadersRow();
				imgui->TableNextRow(0, 0.0f);

				imgui->TableNextColumn();
				if (info.inputs.empty())
					imgui->TextDisabled("(none)");
				for (const CodexRecipes::RecipeItemRef& input : info.inputs)
				{
					const float rate = info.buildTimeSeconds > 0.0f
						? (static_cast<float>(input.count) / info.buildTimeSeconds) * 60.0f
						: 0.0f;
					RenderItemRow(imgui, textures, input, kIconSize, rate, false);
				}

				imgui->TableNextColumn();
				RenderItemRow(imgui, textures, info.output, kIconSize, info.outputsPerMinute, true);

				imgui->EndTable();
			}

			RenderConsumersPopup(imgui, textures);
		}

		// The recipes one building tier can craft, as a grid of icon rows.
		// Left-click opens the recipe; right-click pins it.
		void RenderTierRecipes(IModLoaderImGui* imgui, IPluginImGuiTextures* textures,
			const CodexRecipes::BuildingTier& tier)
		{
			if (tier.recipes.empty())
			{
				imgui->TextDisabled("This building has no known recipes.");
				return;
			}

			constexpr float kIconSize = 28.0f;
			constexpr float kMinColumnWidth = 240.0f;

			float availW = 0.0f, availH = 0.0f;
			imgui->GetContentRegionAvail(&availW, &availH);
			const float fontScale = (std::max)(1.0f, imgui->GetFontSize() / kBaselineFontSize);
			const int columns = (std::max)(1, static_cast<int>(availW / (kMinColumnWidth * fontScale)));

			if (!imgui->BeginTable("##tier_recipes", columns, 0))
				return;

			for (int c = 0; c < columns; ++c)
				imgui->TableSetupColumn(nullptr, kColumnFlagsWidthStretch, 1.0f);

			for (void* nativeRecipe : tier.recipes)
			{
				CodexRecipes::RecipeInfo recipe;
				if (!CodexRecipes::FindByNativeRecipe(nativeRecipe, recipe))
					continue;

				imgui->TableNextColumn();
				PushIDPtr(imgui, nativeRecipe);

				bool clicked = imgui->SelectableFull("##tier_recipe", recipe.nativeRecipe == g_selectedNativeRecipe,
					kItemSelectableFlags, 0.0f, TwoLineRowHeight(imgui, kIconSize));
				const bool rightClicked = imgui->IsItemClicked(1);
				if (imgui->IsItemHovered())
					imgui->SetTooltip(IsPinned(nativeRecipe)
						? "Click to view (pinned)"
						: "Click to view, right-click to pin");
				imgui->SameLine(0.0f, 0.0f);

				PluginTextureHandle icon = CodexIcons::GetIcon(recipe.output.uniqueItemName);
				if (icon && textures)
				{
					textures->Image(icon, kIconSize, kIconSize);
					imgui->SameLine(0.0f, 6.0f);
				}

				imgui->BeginGroup();
				imgui->Text(recipe.displayName.c_str());
				char sub[64];
				if (recipe.outputsPerMinute > 0.0f)
					snprintf(sub, sizeof(sub), "%.1f/min", recipe.outputsPerMinute);
				else
					snprintf(sub, sizeof(sub), "x%d", recipe.output.count);
				imgui->SetCursorPosY(imgui->GetCursorPosY() - 4.0f);
				imgui->TextDisabled(sub);
				imgui->EndGroup();

				imgui->PopID();

				if (rightClicked && !IsPinned(nativeRecipe))
					PinRecipe(imgui, nativeRecipe);
				else if (clicked)
					OpenDetail(nativeRecipe);
			}

			imgui->EndTable();
		}

		void RenderBuildingsTab(IModLoaderImGui* imgui, IPluginImGuiTextures* textures)
		{
			if (!CodexRecipes::IsReady())
			{
				imgui->TextDisabled("Recipes are still loading...");
				return;
			}

			std::vector<CodexRecipes::BuildingGroup> groups = CodexRecipes::GetBuildingGroups();
			if (groups.empty())
			{
				imgui->TextDisabled("No crafting buildings found.");
				return;
			}

			const CodexRecipes::BuildingGroup* selected = nullptr;
			for (const CodexRecipes::BuildingGroup& group : groups)
			{
				if (group.displayName == g_selectedBuildingGroup)
					selected = &group;
			}
			if (!selected)
			{
				selected = &groups.front();
				g_selectedBuildingGroup = selected->displayName;
			}

			const float fontScale = (std::max)(1.0f, imgui->GetFontSize() / kBaselineFontSize);

			if (imgui->BeginChild("##building_list", 190.0f * fontScale, 0.0f, true))
			{
				for (const CodexRecipes::BuildingGroup& group : groups)
				{
					imgui->PushIDStr(group.displayName.c_str());
					if (imgui->Selectable(group.displayName.c_str(), &group == selected))
					{
						g_selectedBuildingGroup = group.displayName;
						g_requestedTier = 0;
					}
					imgui->PopID();
				}
			}
			imgui->EndChild();

			imgui->SameLine(0.0f, 8.0f);

			if (imgui->BeginChild("##building_recipes", 0.0f, 0.0f, false))
			{
				imgui->SeparatorText(selected->displayName.c_str());

				if (selected->tiers.size() == 1)
				{
					RenderTierRecipes(imgui, textures, selected->tiers.front());
				}
				else if (imgui->BeginTabBar("##building_tiers", 0))
				{
					for (const CodexRecipes::BuildingTier& tier : selected->tiers)
					{
						char label[32];
						snprintf(label, sizeof(label), "Tier %d", tier.tier);
						const int flags = g_requestedTier == tier.tier ? kTabItemFlagsSetSelected : 0;
						if (imgui->BeginTabItem(label, nullptr, flags))
						{
							RenderTierRecipes(imgui, textures, tier);
							imgui->EndTabItem();
						}
					}
					imgui->EndTabBar();
				}
				g_requestedTier = 0;
			}
			imgui->EndChild();
		}

		void RenderDetailWidget(IModLoaderImGui* imgui)
		{
			// Opened from the mod loader's own panel list rather than via
			// OpenDetail - adopt it so Escape and our state track it too.
			if (!g_detailVisible)
			{
				g_detailVisible = true;
				AcquireEscape();
				AcquireCapture();
				if (!g_selectedNativeRecipe && g_requestedTab == DetailTab::None)
					g_requestedTab = DetailTab::Buildings;
			}
			g_detailLastFrame = imgui->GetFrameCount();

			// Opens the search box over this window; picking a result then
			// navigates here instead of opening a new window.
			if (imgui->Button("Search"))
				OpenSearch();

			if (AnyPinned())
			{
				imgui->SameLine(0.0f, 6.0f);
				if (imgui->Button("Arrange pins"))
					ArrangePins(imgui);
			}

			IPluginImGuiTextures* textures = GetHooks() ? GetHooks()->ImGuiTextures : nullptr;

			const DetailTab requested = g_requestedTab;
			g_requestedTab = DetailTab::None;

			if (imgui->BeginTabBar("##codex_tabs", 0))
			{
				if (imgui->BeginTabItem("Recipe", nullptr, requested == DetailTab::Recipe ? kTabItemFlagsSetSelected : 0))
				{
					RenderRecipeTab(imgui, textures);
					imgui->EndTabItem();
				}
				if (imgui->BeginTabItem("Buildings", nullptr, requested == DetailTab::Buildings ? kTabItemFlagsSetSelected : 0))
				{
					RenderBuildingsTab(imgui, textures);
					imgui->EndTabItem();
				}
				imgui->EndTabBar();
			}
		}

		// ------------------------------------------------------------------
		// Pinned recipes
		//
		// Each pin is its own always-visible widget window. Widgets never
		// acquire input capture on their own, and the modloader only feeds
		// ImGui the mouse while something holds capture or the Escape menu is
		// open (there only to windows that opt in, as the pins do) - so with
		// only pins on screen, the game keeps every mouse/keyboard input.
		// Whenever one of those is up (the Codex search/detail, the
		// modloader's F2 overlay, the Escape menu) the first pin can be
		// dragged and the others follow it, tiled or cascaded; the
		// Open/Launch planner/Unpin buttons only show while the Codex itself
		// is open.
		//
		// Widget render callbacks carry no user data, so a fixed pool of
		// slots each gets its own template-instantiated callback.
		// ------------------------------------------------------------------
		constexpr int kMaxPins = 8;

		// Where a pinned window sits on screen: top-left corner and size, in
		// pixels. Measured from the window every frame; valid once it has
		// rendered.
		struct PinRect
		{
			float x = 0.0f;
			float y = 0.0f;
			float w = 0.0f;
			float h = 0.0f;
			bool  valid = false;
		};

		struct PinSlot
		{
			void*             nativeRecipe = nullptr;
			WidgetHandle      widget = nullptr;
			PluginWidgetDesc  desc{};
			PluginWindowHints hints{};
			PinRect           rect;
			unsigned          order = 0; // counts up with each pin; the lowest is the lead
			char              title[192] = "";
			bool              dragging = false;
			float             dragFromX = 0.0f;
			float             dragFromY = 0.0f;
		};

		PinSlot  g_pins[kMaxPins];
		unsigned g_pinOrder = 0;

		// How the pins are laid out, from the config. The lead pin (the one
		// pinned first) is the only one the player moves; the rest follow it.
		// Each arrangement remembers where the lead was last dropped, as a
		// fraction of the screen (its top-left corner) so it survives a
		// resolution change. Written to the config when a drag ends, never
		// per frame.
		struct PinAnchor
		{
			float x = 0.0f;
			float y = 0.0f;
			bool  set = false;
		};

		enum class PinEdge { Right, Left, Top };

		struct PinSettings
		{
			bool      cascade = false;
			PinEdge   edge = PinEdge::Right;
			bool      gap = true;
			PinAnchor anchors[2]; // indexed by `cascade`
		};

		PinSettings g_pinSettings;

		constexpr int kPinWindowFlags = kWindowFlagsAlwaysAutoResize | PluginWindowFlags_NoSavedSettings |
			PluginWindowFlags_PauseMenuMouseInputs;

		// Set when the lead pin has to be put back at its anchor: the layout
		// was changed in the config, the old lead was unpinned, the screen
		// size changed, or the player asked for Arrange pins.
		bool  g_pinReanchor = false;
		float g_pinAnchoredW = 0.0f;
		float g_pinAnchoredH = 0.0f;
		float g_pinAnchoredLeadW = 0.0f; // the lead's width when it was last placed

		// One "0.1234,0.5678" entry, with room to spare.
		constexpr int kPinOffsetBufferSize = 32;

		// A drop that moves the window less than this is a click, not a drag.
		constexpr float kPinDragThreshold = 0.5f;

		PinAnchor ParsePinAnchor(const char* text)
		{
			PinAnchor anchor;
			if (sscanf_s(text, "%f,%f", &anchor.x, &anchor.y) == 2)
				anchor.set = anchor.x >= 0.0f && anchor.x <= 1.0f && anchor.y >= 0.0f && anchor.y <= 1.0f;
			return anchor;
		}

		PinAnchor ReadPinAnchor(bool cascade)
		{
			char text[kPinOffsetBufferSize];
			CodexConfig::Config::GetPinOffset(cascade, text, sizeof(text));
			return ParsePinAnchor(text);
		}

		void WritePinAnchor(bool cascade, const PinAnchor& anchor)
		{
			char text[kPinOffsetBufferSize] = "";
			if (anchor.set)
				snprintf(text, sizeof(text), "%.4f,%.4f", anchor.x, anchor.y);
			CodexConfig::Config::SetPinOffset(cascade, text);
		}

		PinEdge ParsePinEdge(const char* layout)
		{
			return _stricmp(layout, "Left") == 0 ? PinEdge::Left
				: _stricmp(layout, "Top") == 0 ? PinEdge::Top
				: PinEdge::Right;
		}

		// Takes up new pin options. A change to the arrangement, the edge or
		// the current arrangement's anchor moves the lead pin to match.
		void ApplyPinSettings(const PinSettings& next)
		{
			const PinAnchor& before = g_pinSettings.anchors[next.cascade ? 1 : 0];
			const PinAnchor& after = next.anchors[next.cascade ? 1 : 0];
			if (next.cascade != g_pinSettings.cascade || next.edge != g_pinSettings.edge ||
				after.set != before.set || after.x != before.x || after.y != before.y)
				g_pinReanchor = true;

			g_pinSettings = next;
		}

		// Reads every pin option from the config, at startup.
		void LoadPinSettings()
		{
			PinSettings next;
			next.cascade = _stricmp(CodexConfig::Config::GetPinArrangement(), "Cascade") == 0;
			next.edge = ParsePinEdge(CodexConfig::Config::GetPinLayout());
			next.gap = CodexConfig::Config::GetPinGap();
			next.anchors[0] = ReadPinAnchor(false);
			next.anchors[1] = ReadPinAnchor(true);
			ApplyPinSettings(next);
		}

		// Live edits arrive here with the new value; the config file itself
		// is only written afterwards.
		void OnConfigChanged(const char* section, const char* key, const char* newValue)
		{
			if (!section || !key || !newValue || strcmp(section, "Menu") != 0)
				return;

			// A cleared or hand-edited search box spot: re-place an open box.
			const bool searchX = strcmp(key, "SearchBoxX") == 0;
			if (searchX || strcmp(key, "SearchBoxY") == 0)
			{
				(searchX ? g_searchSavedX : g_searchSavedY) = static_cast<float>(atof(newValue));
				if (g_searchVisible)
				{
					g_searchPlaceFrames = 1;
					g_searchPlaced = false;
				}
				return;
			}

			PinSettings next = g_pinSettings;
			if (strcmp(key, "PinArrangement") == 0)
				next.cascade = _stricmp(newValue, "Cascade") == 0;
			else if (strcmp(key, "PinLayout") == 0)
				next.edge = ParsePinEdge(newValue);
			else if (strcmp(key, "PinGap") == 0)
				next.gap = _stricmp(newValue, "true") == 0 || strcmp(newValue, "1") == 0;
			else if (strcmp(key, "PinTileOffset") == 0)
				next.anchors[0] = ParsePinAnchor(newValue);
			else if (strcmp(key, "PinCascadeOffset") == 0)
				next.anchors[1] = ParsePinAnchor(newValue);
			else
				return;

			ApplyPinSettings(next);
		}

		bool CodexUIActive(IModLoaderImGui* imgui)
		{
			return g_searchVisible || imgui->GetFrameCount() - g_detailLastFrame <= 1;
		}

		bool IsPinned(void* nativeRecipe)
		{
			for (const PinSlot& slot : g_pins)
			{
				if (slot.nativeRecipe && slot.nativeRecipe == nativeRecipe)
					return true;
			}
			return false;
		}

		bool HasFreePinSlot()
		{
			for (const PinSlot& slot : g_pins)
			{
				if (!slot.nativeRecipe && slot.widget)
					return true;
			}
			return false;
		}

		bool AnyPinned()
		{
			for (const PinSlot& slot : g_pins)
			{
				if (slot.nativeRecipe)
					return true;
			}
			return false;
		}

		// The pinned slots in the order they were pinned; returns how many.
		int PinnedInOrder(int (&slots)[kMaxPins])
		{
			int count = 0;
			for (int i = 0; i < kMaxPins; ++i)
			{
				if (!g_pins[i].nativeRecipe)
					continue;

				int at = count++;
				while (at > 0 && g_pins[slots[at - 1]].order > g_pins[i].order)
				{
					slots[at] = slots[at - 1];
					--at;
				}
				slots[at] = i;
			}
			return count;
		}

		// The slot of the lead pin - the first one pinned - or -1 when
		// nothing is pinned.
		int LeadPin()
		{
			int slots[kMaxPins];
			return PinnedInOrder(slots) > 0 ? slots[0] : -1;
		}

		// Puts the lead pin at its anchor: where the player last dropped it
		// for the current arrangement, or else on the configured screen edge -
		// Right and Left at that edge's top corner, Top at the top-left. Only
		// requests the position - the slot's render applies it once, after
		// which the player's drag sticks.
		void AnchorLead(IModLoaderImGui* imgui, PinSlot& slot)
		{
			imgui->GetDisplaySize(&g_displayW, &g_displayH);
			g_pinAnchoredW = g_displayW;
			g_pinAnchoredH = g_displayH;
			g_pinAnchoredLeadW = slot.rect.w;
			g_pinReanchor = false;

			slot.hints.pivot_y = 0.0f;
			slot.hints.pos_cond = 0;

			const PinAnchor& anchor = g_pinSettings.anchors[g_pinSettings.cascade ? 1 : 0];
			if (anchor.set)
			{
				slot.hints.pivot_x = 0.0f;
				slot.hints.pos_x   = anchor.x * g_displayW;
				slot.hints.pos_y   = anchor.y * g_displayH;
				return;
			}

			const float step = imgui->GetFrameHeightWithSpacing();
			const bool fromRight = g_pinSettings.edge == PinEdge::Right;
			slot.hints.pivot_x = fromRight ? 1.0f : 0.0f;
			slot.hints.pos_x   = fromRight ? g_displayW - step : step;
			slot.hints.pos_y   = step;
		}

		// Works out where every pinned window other than the lead belongs,
		// from the lead's position and each window's measured size. Tile
		// stacks right below the lead - Right and Left in a column along that
		// edge, Top in a row along the top - and starts the next column or row
		// at the screen margin when the screen runs out. Cascade steps each
		// pin one title bar (the frame height with spacing) down and in,
		// towards the screen's middle. Returns false until the lead has
		// rendered.
		bool PinTargets(IModLoaderImGui* imgui, float (&x)[kMaxPins], float (&y)[kMaxPins])
		{
			int slots[kMaxPins];
			const int count = PinnedInOrder(slots);
			if (count == 0 || !g_pins[slots[0]].rect.valid)
				return false;

			const PinRect& lead = g_pins[slots[0]].rect;
			const float step = imgui->GetFrameHeightWithSpacing();
			const float margin = step;
			// The theme's item spacing: the frame height with spacing less the
			// frame height.
			const float gap = g_pinSettings.gap ? step - imgui->GetFrameHeight() : 0.0f;
			const PinEdge edge = g_pinSettings.edge;

			if (g_pinSettings.cascade)
			{
				const float inward = edge == PinEdge::Right ? -step : step;
				for (int k = 0; k < count; ++k)
				{
					x[slots[k]] = lead.x + inward * static_cast<float>(k);
					y[slots[k]] = lead.y + step * static_cast<float>(k);
				}
			}
			else if (edge == PinEdge::Top)
			{
				float left = lead.x;
				float top = lead.y;
				float rowHeight = 0.0f;
				for (int k = 0; k < count; ++k)
				{
					const PinRect& pin = g_pins[slots[k]].rect;
					if (k > 0 && left + pin.w > g_displayW - margin)
					{
						left = margin;
						top += rowHeight + gap;
						rowHeight = 0.0f;
					}
					x[slots[k]] = left;
					y[slots[k]] = top;
					left += pin.w + gap;
					rowHeight = (std::max)(rowHeight, pin.h);
				}
			}
			else
			{
				const bool fromRight = edge == PinEdge::Right;
				float side = fromRight ? lead.x + lead.w : lead.x; // the edge the column lines up on
				float top = lead.y;
				float columnWidth = 0.0f;
				for (int k = 0; k < count; ++k)
				{
					const PinRect& pin = g_pins[slots[k]].rect;
					if (k > 0 && top + pin.h > g_displayH - margin)
					{
						side += (fromRight ? -1.0f : 1.0f) * (columnWidth + gap);
						top = margin;
						columnWidth = 0.0f;
					}
					x[slots[k]] = fromRight ? side - pin.w : side;
					y[slots[k]] = top;
					top += pin.h + gap;
					columnWidth = (std::max)(columnWidth, pin.w);
				}
			}
			return true;
		}

		// Puts the pins back on the configured edge: forgets where the lead
		// was dropped for the current arrangement.
		void ArrangePins(IModLoaderImGui* imgui)
		{
			const bool cascade = g_pinSettings.cascade;
			g_pinSettings.anchors[cascade ? 1 : 0] = PinAnchor{};
			WritePinAnchor(cascade, g_pinSettings.anchors[cascade ? 1 : 0]);

			const int lead = LeadPin();
			if (lead >= 0)
				AnchorLead(imgui, g_pins[lead]);
		}

		void PinRecipe(IModLoaderImGui* imgui, void* nativeRecipe)
		{
			if (!nativeRecipe || IsPinned(nativeRecipe))
				return;

			CodexRecipes::RecipeInfo info;
			if (!CodexRecipes::FindByNativeRecipe(nativeRecipe, info))
				return;

			for (int i = 0; i < kMaxPins; ++i)
			{
				PinSlot& slot = g_pins[i];
				if (slot.nativeRecipe || !slot.widget)
					continue;

				const bool first = !AnyPinned();
				slot.nativeRecipe = nativeRecipe;
				slot.order = ++g_pinOrder;
				snprintf(slot.title, sizeof(slot.title), "%s###CodexPin%d", info.displayName.c_str(), i);

				if (first)
					AnchorLead(imgui, slot);

				if (g_self && g_self->hooks->UI)
					g_self->hooks->UI->SetWidgetVisible(slot.widget, true);
				return;
			}
		}

		void UnpinSlot(int index)
		{
			PinSlot& slot = g_pins[index];

			// The next pin along becomes the lead and takes the anchor.
			if (LeadPin() == index)
				g_pinReanchor = true;

			slot.nativeRecipe = nullptr;
			slot.rect = PinRect{};
			slot.dragging = false;
			if (g_self && g_self->hooks->UI && slot.widget)
				g_self->hooks->UI->SetWidgetVisible(slot.widget, false);
		}

		// Saves the lead's position as the current arrangement's anchor once,
		// when a drag that moved its window ends.
		void SaveLeadIfDragged(IModLoaderImGui* imgui, PinSlot& slot)
		{
			float posX = 0.0f, posY = 0.0f;
			imgui->GetWindowPos(&posX, &posY);

			if (imgui->IsMouseClicked(0, false) && imgui->IsWindowHovered(0))
			{
				slot.dragging = true;
				slot.dragFromX = posX;
				slot.dragFromY = posY;
			}
			else if (slot.dragging && imgui->IsMouseReleased(0))
			{
				slot.dragging = false;
				if (std::fabs(posX - slot.dragFromX) > kPinDragThreshold || std::fabs(posY - slot.dragFromY) > kPinDragThreshold)
				{
					imgui->GetDisplaySize(&g_displayW, &g_displayH);
					// Clamped so a lead dropped partly off the screen is still restored.
					const bool cascade = g_pinSettings.cascade;
					g_pinSettings.anchors[cascade ? 1 : 0] =
						{ std::clamp(posX / g_displayW, 0.0f, 1.0f), std::clamp(posY / g_displayH, 0.0f, 1.0f), true };
					WritePinAnchor(cascade, g_pinSettings.anchors[cascade ? 1 : 0]);
				}
			}
		}

		void RenderPinSlot(int index, IModLoaderImGui* imgui)
		{
			PinSlot& slot = g_pins[index];

			// A placement requested last frame has now been applied - stop
			// forcing it.
			slot.hints.pos_x = -1.0f;
			slot.hints.pos_y = -1.0f;

			float winX = 0.0f, winY = 0.0f, winW = 0.0f, winH = 0.0f;
			imgui->GetWindowPos(&winX, &winY);
			imgui->GetWindowSize(&winW, &winH);
			if (winW > 0.0f && winH > 0.0f)
				slot.rect = { winX, winY, winW, winH, true };

			imgui->GetDisplaySize(&g_displayW, &g_displayH);

			if (index == LeadPin())
			{
				// In cascade the lead stays at the back, so a click on it does not
				// raise it over the title bars of the pins below.
				slot.hints.extra_window_flags = kPinWindowFlags |
					(g_pinSettings.cascade ? kWindowFlagsNoBringToFrontOnFocus : 0);

				// On the Right edge the lead keeps its right edge when its window
				// grows, as it does when the Codex opens and the buttons show.
				const bool edgeAnchored = g_pinSettings.edge == PinEdge::Right &&
					!g_pinSettings.anchors[g_pinSettings.cascade ? 1 : 0].set;
				if (g_pinReanchor || g_displayW != g_pinAnchoredW || g_displayH != g_pinAnchoredH ||
					(edgeAnchored && slot.rect.w != g_pinAnchoredLeadW))
					AnchorLead(imgui, slot);
				SaveLeadIfDragged(imgui, slot);
				ShowDragCursor(imgui);
			}
			else
			{
				// Only the lead is moved by hand; the rest follow it.
				slot.hints.extra_window_flags = kPinWindowFlags | PluginWindowFlags_NoMove;

				float targetX[kMaxPins], targetY[kMaxPins];
				if (PinTargets(imgui, targetX, targetY))
				{
					// Kept on screen; the loader ignores a negative position.
					slot.hints.pivot_x = 0.0f;
					slot.hints.pivot_y = 0.0f;
					slot.hints.pos_cond = 0;
					slot.hints.pos_x = std::clamp(targetX[index], 0.0f, (std::max)(0.0f, g_displayW - slot.rect.w));
					slot.hints.pos_y = std::clamp(targetY[index], 0.0f, (std::max)(0.0f, g_displayH - slot.rect.h));
				}
			}

			const bool interactive = CodexUIActive(imgui);

			CodexRecipes::RecipeInfo info;
			if (!slot.nativeRecipe || !CodexRecipes::FindByNativeRecipe(slot.nativeRecipe, info))
			{
				imgui->TextDisabled("Recipe unavailable.");
				if (interactive && imgui->SmallButton("Unpin"))
					UnpinSlot(index);
				return;
			}

			IPluginImGuiTextures* textures = GetHooks() ? GetHooks()->ImGuiTextures : nullptr;
			constexpr float kIconSize = 20.0f;

			char line[256];
			snprintf(line, sizeof(line), "%s  -  %.1fs", info.buildingName.c_str(), info.buildTimeSeconds);
			imgui->TextDisabled(line);

			for (const CodexRecipes::RecipeItemRef& input : info.inputs)
			{
				PluginTextureHandle icon = CodexIcons::GetIcon(input.uniqueItemName);
				if (icon && textures)
				{
					textures->Image(icon, kIconSize, kIconSize);
					imgui->SameLine(0.0f, 6.0f);
				}
				const float rate = info.buildTimeSeconds > 0.0f
					? (static_cast<float>(input.count) / info.buildTimeSeconds) * 60.0f
					: 0.0f;
				if (rate > 0.0f)
					snprintf(line, sizeof(line), "%dx %s  (%.1f/min)", input.count, input.displayName.c_str(), rate);
				else
					snprintf(line, sizeof(line), "%dx %s", input.count, input.displayName.c_str());
				imgui->AlignTextToFramePadding();
				imgui->Text(line);
			}

			imgui->Separator();

			PluginTextureHandle outIcon = CodexIcons::GetIcon(info.output.uniqueItemName);
			if (outIcon && textures)
			{
				textures->Image(outIcon, kIconSize, kIconSize);
				imgui->SameLine(0.0f, 6.0f);
			}
			if (info.outputsPerMinute > 0.0f)
				snprintf(line, sizeof(line), "%dx %s  (%.1f/min)", info.output.count, info.output.displayName.c_str(), info.outputsPerMinute);
			else
				snprintf(line, sizeof(line), "%dx %s", info.output.count, info.output.displayName.c_str());
			imgui->AlignTextToFramePadding();
			imgui->Text(line);

			if (interactive)
			{
				if (imgui->SmallButton("Open"))
					OpenDetail(info.nativeRecipe);
				imgui->SameLine(0.0f, 6.0f);
				RenderLaunchPlannerButton(imgui, info);
				imgui->SameLine(0.0f, 6.0f);
				if (imgui->SmallButton("Unpin"))
					UnpinSlot(index);
			}
		}

		template <int Index>
		void RenderPinSlotThunk(IModLoaderImGui* imgui)
		{
			RenderPinSlot(Index, imgui);
		}

		template <int... Indices>
		constexpr auto MakePinRenderFns(std::integer_sequence<int, Indices...>)
		{
			return std::array<PluginImGuiRenderCallback, sizeof...(Indices)>{ &RenderPinSlotThunk<Indices>... };
		}

		constexpr auto kPinRenderFns = MakePinRenderFns(std::make_integer_sequence<int, kMaxPins>{});

		void RegisterPinSlots(IPluginSelf* self)
		{
			for (int i = 0; i < kMaxPins; ++i)
			{
				PinSlot& slot = g_pins[i];
				snprintf(slot.title, sizeof(slot.title), "Pinned Recipe###CodexPin%d", i);

				slot.hints.width  = 0.0f;
				slot.hints.height = 0.0f;
				slot.hints.pos_x  = -1.0f;
				slot.hints.pos_y  = -1.0f;
				slot.hints.size_cond = 0;
				slot.hints.pos_cond  = 0;
				slot.hints.extra_window_flags = kPinWindowFlags;

				slot.desc.name        = slot.title;
				slot.desc.renderFn    = kPinRenderFns[i];
				slot.desc.windowHints = &slot.hints;

				slot.widget = self->hooks->UI->RegisterWidget(&slot.desc);
				if (slot.widget)
					self->hooks->UI->SetWidgetVisible(slot.widget, false);
			}
		}

		void UnregisterPinSlots(IPluginSelf* self)
		{
			for (PinSlot& slot : g_pins)
			{
				slot.nativeRecipe = nullptr;
				if (slot.widget)
				{
					self->hooks->UI->UnregisterWidget(slot.widget);
					slot.widget = nullptr;
				}
			}
		}
	}

	void Init(IPluginSelf* self)
	{
		g_self = self;

		if (!self->hooks->UI)
		{
			LOG_WARN("CodexUI: UI hooks unavailable (server/generic build) - Codex search disabled.");
			return;
		}

		g_searchHints.width  = 440.0f;
		g_searchHints.height = 0.0f;
		g_searchHints.pos_x  = -1.0f; // placed by PlaceSearchBox each time it opens
		g_searchHints.pos_y  = -1.0f;
		g_searchHints.pivot_x = 0.5f;
		g_searchHints.pivot_y = 0.0f; // Top-anchored so growth expands downward, not from center
		g_searchHints.size_cond = 0; // Always
		g_searchHints.pos_cond  = 0; // Always - for the frames it is placed on
		g_searchHints.extra_window_flags = PluginWindowFlags_NoTitleBar | PluginWindowFlags_NoResize |
			PluginWindowFlags_NoSavedSettings | PluginWindowFlags_PauseMenuMouseInputs;

		g_searchDesc.name        = "Codex Search";
		g_searchDesc.renderFn    = &RenderSearchWidget;
		g_searchDesc.windowHints = &g_searchHints;
		g_searchWidget = self->hooks->UI->RegisterWidget(&g_searchDesc);
		if (g_searchWidget)
			self->hooks->UI->SetWidgetVisible(g_searchWidget, false);

		// Registered as a Panel (not a Widget) so it gets the same
		// freely-resizable/movable window as every other plugin's info
		// window (BetterCheats, ProductionViewer) - Widgets are meant for
		// pinned, non-resizable overlays like the search box above.
		g_detailDesc.buttonLabel = "Codex";
		g_detailDesc.windowTitle = "Codex";
		g_detailDesc.renderFn    = &RenderDetailWidget;
		g_detailPanel = self->hooks->UI->RegisterPanel(&g_detailDesc);
		self->hooks->UI->RegisterOnPanelWindowClosed(&OnDetailPanelClosed);

		// Zero-size, input-less and backgroundless: it exists for one frame
		// only to run CloseDetailNow outside the panel render loop.
		g_closerHints.width   = 1.0f;
		g_closerHints.height  = 1.0f;
		g_closerHints.pos_x   = 0.0f;
		g_closerHints.pos_y   = 0.0f;
		g_closerHints.pivot_x = 0.0f;
		g_closerHints.pivot_y = 0.0f;
		g_closerHints.size_cond = 0;
		g_closerHints.pos_cond  = 0;
		g_closerHints.extra_window_flags = PluginWindowFlags_NoTitleBar | PluginWindowFlags_NoResize |
			PluginWindowFlags_NoMove | PluginWindowFlags_NoScrollbar | PluginWindowFlags_NoBackground |
			PluginWindowFlags_NoSavedSettings | PluginWindowFlags_NoMouseInputs;

		g_closerDesc.name        = "##CodexCloser";
		g_closerDesc.renderFn    = &RenderCloserWidget;
		g_closerDesc.windowHints = &g_closerHints;
		g_closerWidget = self->hooks->UI->RegisterWidget(&g_closerDesc);
		if (g_closerWidget)
			self->hooks->UI->SetWidgetVisible(g_closerWidget, false);

		CodexConfig::Config::GetSearchBoxPos(g_searchSavedX, g_searchSavedY);
		LoadPinSettings();
		RegisterPinSlots(self);
		self->hooks->UI->RegisterOnConfigChanged(self, &OnConfigChanged);

		if (self->hooks->Input)
		{
			const char* searchKey = CodexConfig::Config::GetSearchKey();
			LOG_DEBUG("CodexUI: registering search keybind '%s'", searchKey);
			self->hooks->Input->RegisterKeybindByName(searchKey, EModKeyEvent::Pressed, &OnSearchKeyPressed);

			const char* codexKey = CodexConfig::Config::GetCodexKey();
			LOG_DEBUG("CodexUI: registering codex keybind '%s'", codexKey);
			self->hooks->Input->RegisterKeybindByName(codexKey, EModKeyEvent::Pressed, &OnCodexKeyPressed);
		}

		LOG_INFO("CodexUI: search, detail and pin windows registered");
	}

	void Shutdown(IPluginSelf* self)
	{
		if (!self->hooks->UI)
		{
			g_self = nullptr;
			return;
		}

		CloseSearch();
		CloseDetailNow();

		if (g_closerWidget)
		{
			self->hooks->UI->UnregisterWidget(g_closerWidget);
			g_closerWidget = nullptr;
		}

		if (self->hooks->Input)
		{
			const char* searchKey = CodexConfig::Config::GetSearchKey();
			self->hooks->Input->UnregisterKeybindByName(searchKey, EModKeyEvent::Pressed, &OnSearchKeyPressed);

			const char* codexKey = CodexConfig::Config::GetCodexKey();
			self->hooks->Input->UnregisterKeybindByName(codexKey, EModKeyEvent::Pressed, &OnCodexKeyPressed);
		}

		if (g_searchWidget)
		{
			self->hooks->UI->UnregisterWidget(g_searchWidget);
			g_searchWidget = nullptr;
		}

		self->hooks->UI->UnregisterOnConfigChanged(self, &OnConfigChanged);
		UnregisterPinSlots(self);

		self->hooks->UI->UnregisterOnPanelWindowClosed(&OnDetailPanelClosed);
		if (g_detailPanel)
		{
			self->hooks->UI->UnregisterPanel(g_detailPanel);
			g_detailPanel = nullptr;
		}

		g_self = nullptr;
	}
}
