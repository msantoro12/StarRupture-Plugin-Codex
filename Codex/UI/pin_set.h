#pragma once

// The pinned recipes as they are saved in the config: the recipes' keys
// (their asset names, which survive a game update where a runtime pointer
// does not), joined with ";" in pin order. A pure function of the keys: no
// ImGui, no game, so it can be tested on its own.

#include <algorithm>
#include <string>
#include <vector>

namespace CodexPinSet
{
	constexpr char kSeparator = ';';

	// The keys joined in order. A key that is empty or holds the separator is
	// skipped (it could not be read back), and so is one that would not fit
	// in `maxLen` characters, along with every key after it, so the text
	// always reads back as a leading run of the pins.
	inline std::string Format(const std::vector<std::string>& keys, size_t maxLen)
	{
		std::string text;
		for (const std::string& key : keys)
		{
			if (key.empty() || key.find(kSeparator) != std::string::npos)
				continue;
			const size_t added = key.size() + (text.empty() ? 0 : 1);
			if (text.size() + added > maxLen)
				break;
			if (!text.empty())
				text += kSeparator;
			text += key;
		}
		return text;
	}

	// The keys in the text, in order, at most `maxKeys` of them. Blank entries
	// and repeats are dropped.
	inline std::vector<std::string> Parse(const char* text, int maxKeys)
	{
		std::vector<std::string> keys;
		if (!text)
			return keys;

		const std::string all = text;
		size_t from = 0;
		while (from <= all.size() && static_cast<int>(keys.size()) < maxKeys)
		{
			size_t end = all.find(kSeparator, from);
			if (end == std::string::npos)
				end = all.size();

			size_t first = from, last = end;
			while (first < last && (all[first] == ' ' || all[first] == '\t'))
				++first;
			while (last > first && (all[last - 1] == ' ' || all[last - 1] == '\t'))
				--last;

			const std::string key = all.substr(first, last - first);
			if (!key.empty() && std::find(keys.begin(), keys.end(), key) == keys.end())
				keys.push_back(key);
			from = end + 1;
		}
		return keys;
	}
}
