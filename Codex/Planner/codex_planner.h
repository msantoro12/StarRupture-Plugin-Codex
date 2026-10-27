#pragma once

#include <string>

// "Launch planner" button support: opens the player's default browser at a
// production planner for an item.
//
// The planner is chosen in the plugin config (Planner/Planner): the default is
// the StarRupture Planner (https://starrupture.goodstuff.software), which
// takes its target item as ?p=base64url({"i":"<id>","r":60}); "Custom" uses
// Planner/CustomUrlTemplate with {item} replaced by the planner's item id,
// URL-encoded.
//
// Planners use their own lowercase item slugs ("basicfuel", "ore_calcium"),
// not the game's UniqueItemName, so the item is translated through a table
// built from the StarRupture Planner's item catalog.
namespace CodexPlanner
{
	// Opens the configured planner for the item. If the item has no planner id
	// the planner is still opened, without an item, and the id is logged.
	// Only http:// and https:// URLs are ever handed to the shell.
	void Launch(const std::string& uniqueItemName, const std::string& displayName);
}
