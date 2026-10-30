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
			"Screen edge new pinned recipes line up along: \"Right\", \"Left\" or \"Top\""
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

		// Returns the screen edge new pins line up along: "Right" (default), "Left" or "Top".
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

	private:
		static IPluginSelf* s_self;
	};
}
