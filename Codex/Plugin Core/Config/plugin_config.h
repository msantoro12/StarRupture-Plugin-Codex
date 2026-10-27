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
			"Planner",
			"Planner",
			ConfigValueType::String,
			"StarRupture Planner",
			"Planner the Launch planner button opens: \"StarRupture Planner\" or \"Custom\""
		},
		{
			"Planner",
			"CustomUrlTemplate",
			ConfigValueType::String,
			"",
			"Custom planner address; {item} becomes the item's planner id"
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

		// Returns which planner the Launch planner button opens: "StarRupture Planner" (default) or "Custom".
		static const char* GetPlanner()
		{
			static char buffer[64];
			if (s_self && s_self->config->ReadString(s_self, "Planner", "Planner", buffer, sizeof(buffer), "StarRupture Planner"))
				return buffer;
			return "StarRupture Planner";
		}

		// Returns the custom planner URL template; {item} stands for the item's planner id.
		static const char* GetPlannerUrlTemplate()
		{
			static char buffer[512];
			if (s_self && s_self->config->ReadString(s_self, "Planner", "CustomUrlTemplate", buffer, sizeof(buffer), ""))
				return buffer;
			return "";
		}

	private:
		static IPluginSelf* s_self;
	};
}
