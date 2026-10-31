#pragma once

#include "plugin_interface.h"

namespace CodexConfig
{
	static const ConfigEntry CONFIG_ENTRIES[] = {
		{
			"General",
			"Enabled",
			ConfigValueType::Boolean,
			"true",
			"Enable or disable Codex"
		},
		{
			"Menu",
			"SearchKey",
			ConfigValueType::Keybind,
			"N",
			"Key to open the recipe search box"
		},
		{
			"Menu",
			"CodexKey",
			ConfigValueType::Keybind,
			"Shift+N",
			"Key to open (or close) the full Codex window"
		},
		{
			"Menu",
			"PinLayout",
			ConfigValueType::String,
			"Right",
			"Screen edge pinned recipes start from: \"Right\", \"Left\" or \"Top\""
		},
		{
			"Menu",
			"PinArrangement",
			ConfigValueType::String,
			"Tile",
			"How pinned recipes sit under the first one: \"Tile\" or \"Cascade\""
		},
		{
			"Menu",
			"PinGap",
			ConfigValueType::Boolean,
			"true",
			"Leave a gap between tiled pinned recipes; off packs them edge to edge"
		},
		{
			"Menu",
			"SearchBoxX",
			ConfigValueType::Float,
			"-1",
			"Horizontal centre of the search box as a fraction of the screen width; -1 centres it. Drag the box to set"
		},
		{
			"Menu",
			"SearchBoxY",
			ConfigValueType::Float,
			"-1",
			"Top edge of the search box as a fraction of the screen height; -1 centres it. Drag the box to set"
		},
		{
			"Menu",
			"PinTileOffset",
			ConfigValueType::String,
			"",
			"Where the first pinned recipe was last dragged to while tiling, as a screen-fraction \"x,y\" (empty = not moved). Clear to reset"
		},
		{
			"Menu",
			"PinCascadeOffset",
			ConfigValueType::String,
			"",
			"Where the first pinned recipe was last dragged to while cascading, as a screen-fraction \"x,y\" (empty = not moved). Clear to reset"
		},
		{
			"Planner",
			"Planner",
			ConfigValueType::String,
			"StarRupture Planner",
			"Planner the Launch planner button opens: \"StarRupture Planner\", \"starruptureplanner.com\", \"starrupturecalc.com\", \"starrupture-planner.com\" or \"Custom\""
		},
		{
			"Planner",
			"CustomUrlTemplate",
			ConfigValueType::String,
			"",
			"Custom planner address; {item} becomes the StarRupture Planner's item id, {name} the item's display name and {game} the game's internal item name"
		}
	};

	static const ConfigSchema SCHEMA = {
		CONFIG_ENTRIES,
		sizeof(CONFIG_ENTRIES) / sizeof(ConfigEntry)
	};

	// Type-safe config accessor class
	class Config
	{
	public:
		static void Initialize(IPluginSelf* self)
		{
			s_self = self;

			// Initialize config from schema - creates file with defaults if missing
			if (s_self)
			{
				s_self->config->InitializeFromSchema(s_self, &SCHEMA);
			}
		}

		static bool IsEnabled()
		{
			return s_self ? s_self->config->ReadBool(s_self, "General", "Enabled", true) : true;
		}

		// Returns the current search-box keybind string (e.g. "N", "Ctrl+F").
		// The modloader re-registers the keybind automatically when the user changes it.
		static const char* GetSearchKey()
		{
			static char buffer[64];
			if (s_self && s_self->config->ReadString(s_self, "Menu", "SearchKey", buffer, sizeof(buffer), "N"))
				return buffer;
			return "N";
		}

		// Returns the current full-Codex-window keybind string (default "Shift+N").
		static const char* GetCodexKey()
		{
			static char buffer[64];
			if (s_self && s_self->config->ReadString(s_self, "Menu", "CodexKey", buffer, sizeof(buffer), "Shift+N"))
				return buffer;
			return "Shift+N";
		}

		// Returns the screen edge pins start from: "Right" (default), "Left" or "Top".
		static const char* GetPinLayout()
		{
			static char buffer[16];
			if (s_self && s_self->config->ReadString(s_self, "Menu", "PinLayout", buffer, sizeof(buffer), "Right"))
				return buffer;
			return "Right";
		}

		// Saved search-box position as fractions of the screen (x = centre, y = top
		// edge); a negative value means "not placed yet, centre it".
		static void GetSearchBoxPos(float& x, float& y)
		{
			x = s_self ? s_self->config->ReadFloat(s_self, "Menu", "SearchBoxX", -1.0f) : -1.0f;
			y = s_self ? s_self->config->ReadFloat(s_self, "Menu", "SearchBoxY", -1.0f) : -1.0f;
		}

		static void SetSearchBoxPos(float x, float y)
		{
			if (!s_self)
				return;
			s_self->config->WriteFloat(s_self, "Menu", "SearchBoxX", x);
			s_self->config->WriteFloat(s_self, "Menu", "SearchBoxY", y);
		}

		// Returns which planner the Launch planner button opens: "StarRupture Planner" (default),
		// "starruptureplanner.com", "starrupturecalc.com", "starrupture-planner.com" or "Custom".
		static const char* GetPlanner()
		{
			static char buffer[64];
			if (s_self && s_self->config->ReadString(s_self, "Planner", "Planner", buffer, sizeof(buffer) - 1, "StarRupture Planner"))
				return buffer;
			return "StarRupture Planner";
		}

		// Returns the custom planner URL template; {item} stands for the StarRupture Planner's item id,
		// {name} for the display name and {game} for the game's UniqueItemName.
		static const char* GetPlannerUrlTemplate()
		{
			static char buffer[512];
			if (s_self && s_self->config->ReadString(s_self, "Planner", "CustomUrlTemplate", buffer, sizeof(buffer) - 1, ""))
				return buffer;
			return "";
		}

		// Returns how pins sit under the first one: "Tile" (default) or "Cascade".
		static const char* GetPinArrangement()
		{
			static char buffer[16];
			if (s_self && s_self->config->ReadString(s_self, "Menu", "PinArrangement", buffer, sizeof(buffer), "Tile"))
				return buffer;
			return "Tile";
		}

		// Returns whether tiled pins leave a gap between them.
		static bool GetPinGap()
		{
			return s_self ? s_self->config->ReadBool(s_self, "Menu", "PinGap", true) : true;
		}

		// Saved position of the first pin for the tile or cascade arrangement: an "x,y"
		// screen fraction of its top-left corner; empty means it was never moved.
		static void GetPinOffset(bool cascade, char* out, int maxLen)
		{
			if (!s_self || !s_self->config->ReadString(s_self, "Menu", cascade ? "PinCascadeOffset" : "PinTileOffset", out, maxLen, ""))
				out[0] = '\0';
		}

		static void SetPinOffset(bool cascade, const char* offset)
		{
			if (s_self)
				s_self->config->WriteString(s_self, "Menu", cascade ? "PinCascadeOffset" : "PinTileOffset", offset);
		}

	private:
		static IPluginSelf* s_self;
	};
}
