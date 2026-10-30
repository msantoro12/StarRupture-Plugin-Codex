#include "codex_planner.h"
#include "Plugin Core/Config/plugin_config.h"
#include "Plugin Core/Helpers/plugin_helpers.h"

#include <cctype>
#include <cstring>
#include <unordered_map>

#include <shellapi.h>

namespace CodexPlanner
{
	namespace
	{
		constexpr const char* kDefaultPlannerBaseUrl = "https://starrupture.goodstuff.software/";
		constexpr const char* kDefaultPlannerName = "StarRupture Planner";
		constexpr const char* kItemPlaceholder = "{item}";
		constexpr size_t      kMaxUrlLength = 2048;

		// The StarRupture Planner's item catalog (update2_QoL,
		// public/game-data/update2_QoL/items_catalog.json): planner id and the
		// in-game display name. Game items are matched to it by comparing
		// letters and digits only, against the game's UniqueItemName first and
		// its display name second ("CalciumBlock" and "Calcium Block" are the
		// planner's "block_calcium").
		struct PlannerItem
		{
			const char* id;
			const char* name;
		};

		constexpr PlannerItem kPlannerItems[] = {
			{ "accumulator", "Accumulator" },
			{ "aerogel", "Aerogel" },
			{ "airlock", "Airlock" },
			{ "anti-radiationcovers", "Anti-Radiation Covers" },
			{ "applicator", "Applicator" },
			{ "basic_building", "Basic Building Material" },
			{ "basicfuel", "Basic Fuel" },
			{ "battery", "Battery" },
			{ "biofilament", "Biofilament" },
			{ "calcite_sheets", "Calcite Sheets" },
			{ "block_calcium", "Calcium Block" },
			{ "ore_calcium", "Calcium Ore" },
			{ "powder_calcium", "Calcium Powder" },
			{ "carbonsonar", "Carbon Sonar" },
			{ "ceramics", "Ceramics" },
			{ "chemicals", "Chemicals" },
			{ "condenser", "Condenser" },
			{ "converter", "Converter" },
			{ "cyclotrone", "Cyclotrone" },
			{ "dome", "Dome" },
			{ "electromagnet", "Electromagnet" },
			{ "electromagnetic_coil", "Electromagnetic Coil" },
			{ "electronics", "Electronics" },
			{ "epoxy", "Epoxy" },
			{ "grenade", "Explosive Charge" },
			{ "generator", "Generator" },
			{ "glass", "Glass" },
			{ "goethiteingot", "Goethite Ingot" },
			{ "goethitelattice", "Goethite Lattice" },
			{ "goethiteore", "Goethite Ore" },
			{ "goethitepowder", "Goethite Powder" },
			{ "hardening_agent", "Hardening Agent" },
			{ "heat_sheet", "Heat Resistant Sheet" },
			{ "heatshield", "Heat Shield" },
			{ "heavy_ammo", "Heavy Ammo" },
			{ "helium", "Helium-3" },
			{ "impeller", "Impeller" },
			{ "inductor", "Inductor" },
			{ "intermediate_building", "Intermediate Building Material" },
			{ "ionbomb", "Ion Bomb" },
			{ "ion_injector", "Ion Injector" },
			{ "ionstabilizer", "Ion Stabilizer" },
			{ "ionthrusters", "Ion Thrusters" },
			{ "laser_emitter", "Laser Emitter" },
			{ "lens", "Lens" },
			{ "liquid_helium", "Liquid Helium" },
			{ "nanofiber", "Nanofiber" },
			{ "nanosyringe", "Nanosyringe" },
			{ "nozzle", "Nozzle" },
			{ "onboardinstruments", "Onboard Instruments" },
			{ "organiccompound", "Organic Compound" },
			{ "pistol_ammo", "Pistol Ammo" },
			{ "uberoil", "Plastic" },
			{ "plasticdust", "Plastic Dust" },
			{ "magicoilore", "Powerium" },
			{ "pressure_tank", "Pressure Tank" },
			{ "pressurized_helium", "Pressurized Helium" },
			{ "pseudorubberinsulator", "Pseudorubber Insulator" },
			{ "pump", "Pump" },
			{ "quantumtranqualizer", "Quantum Tranquilizer" },
			{ "rnastabilizer", "RNA Stabilizer" },
			{ "rtsc", "RTSC" },
			{ "rtscgenerator", "RTSC Generator" },
			{ "refinedoil", "Refined Oil" },
			{ "reinforcedframe", "Reinforced Frame" },
			{ "reinforcedhousing", "Reinforced Housing" },
			{ "resonator", "Resonator" },
			{ "rocketengine", "Rocket Engine" },
			{ "rocketfuel", "Rocket Fuel" },
			{ "rotor", "Rotor" },
			{ "satellitebody", "Satellite Body" },
			{ "scafolding", "Scaffolding" },
			{ "scanner", "Scanner" },
			{ "shotgun_ammo", "Shotgun Ammo" },
			{ "stabilizer", "Stabilizer" },
			{ "standard_ammo", "Standard Ammo" },
			{ "stator", "Stator" },
			{ "substancefhs-135-493", "Substance #FHS-135-493" },
			{ "ore_sulfur", "Sulfur Ore" },
			{ "sulfuric_acid", "Sulfuric Acid" },
			{ "superconductor", "Superconductor" },
			{ "supermagnet", "Supermagnet" },
			{ "syntheticdna", "Synthetic DNA" },
			{ "syntheticprotein", "Synthetic Protein" },
			{ "synthetic_resin", "Synthetic Resin" },
			{ "synthetic_silicon", "Synthetic Silicon" },
			{ "bar_titanium", "Titanium Bar" },
			{ "titanium_beam", "Titanium Beam" },
			{ "titanium_housing", "Titanium Housing" },
			{ "ore_titanium", "Titanium Ore" },
			{ "titanium_rod", "Titanium Rod" },
			{ "titanium_sheet", "Titanium Sheet" },
			{ "ubertitanium", "Titanoferrite Ingot" },
			{ "trackingdevice", "Tracking Device" },
			{ "tube", "Tube" },
			{ "turbine", "Turbine" },
			{ "uberfilament", "Uberfilament" },
			{ "valve", "Valve" },
			{ "bar_wolfram", "Wolfram Bar" },
			{ "ore_wolfram", "Wolfram Ore" },
			{ "wolfram_plate", "Wolfram Plate" },
			{ "powder_wolfram", "Wolfram Powder" },
			{ "uberwolfram", "Wolfram Steel Ingot" },
			{ "wolfram_wire", "Wolfram Wire" },
		};

		// Game UniqueItemNames that spell the item differently from the planner.
		struct ItemAlias
		{
			const char* gameKey; // normalized UniqueItemName
			const char* id;      // planner id
		};

		constexpr ItemAlias kItemAliases[] = {
			{ "goethite",      "goethiteore" },
			{ "magicoil",      "magicoilore" },
			{ "nanofibre",     "nanofiber" },
			{ "sulphur",       "ore_sulfur" },
			{ "sulphuricacid", "sulfuric_acid" },
		};

		// Lowercase letters and digits only.
		std::string Normalize(const std::string& s)
		{
			std::string out;
			for (char c : s)
				if (std::isalnum(static_cast<unsigned char>(c)))
					out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return out;
		}

		const std::unordered_map<std::string, std::string>& ItemIndex()
		{
			static const std::unordered_map<std::string, std::string> index = []
			{
				std::unordered_map<std::string, std::string> map;
				for (const PlannerItem& item : kPlannerItems)
				{
					map.emplace(Normalize(item.id), item.id);
					map.emplace(Normalize(item.name), item.id);
				}
				for (const ItemAlias& alias : kItemAliases)
					map.emplace(alias.gameKey, alias.id);
				return map;
			}();
			return index;
		}

		// Planner id for a game item, or empty if the planner has no such item.
		std::string FindPlannerId(const std::string& uniqueItemName, const std::string& displayName)
		{
			const auto& index = ItemIndex();
			for (const std::string* name : { &uniqueItemName, &displayName })
			{
				auto it = index.find(Normalize(*name));
				if (it != index.end())
					return it->second;
			}
			return std::string();
		}

		// Percent-encodes everything outside the RFC 3986 unreserved set.
		std::string UrlEncode(const std::string& s)
		{
			static constexpr char kHex[] = "0123456789ABCDEF";
			std::string out;
			for (char c : s)
			{
				const unsigned char u = static_cast<unsigned char>(c);
				if (std::isalnum(u) || c == '-' || c == '_' || c == '.' || c == '~')
				{
					out += c;
				}
				else
				{
					out += '%';
					out += kHex[u >> 4];
					out += kHex[u & 15];
				}
			}
			return out;
		}

		// The only URLs the shell is ever given: http(s), printable ASCII, and
		// none of quote, angle brackets, backslash, caret, backtick, pipe or
		// braces, which could break out of the browser's command line.
		bool IsSafeUrl(const std::string& url)
		{
			const bool https = _strnicmp(url.c_str(), "https://", 8) == 0;
			const bool http = _strnicmp(url.c_str(), "http://", 7) == 0;
			if ((!https && !http) || url.size() > kMaxUrlLength)
				return false;

			for (char c : url)
			{
				const unsigned char u = static_cast<unsigned char>(c);
				if (u <= 0x20 || u >= 0x7F || std::strchr("\"<>\\^`|{}", c))
					return false;
			}
			return true;
		}

		std::string BuildUrl(const std::string& plannerId)
		{
			const char* planner = CodexConfig::Config::GetPlanner();
			if (_stricmp(planner, "Custom") == 0)
			{
				std::string url = CodexConfig::Config::GetPlannerUrlTemplate();
				const std::string item = UrlEncode(plannerId);
				const size_t placeholderLength = std::strlen(kItemPlaceholder);
				for (size_t pos = url.find(kItemPlaceholder); pos != std::string::npos; pos = url.find(kItemPlaceholder, pos + item.size()))
					url.replace(pos, placeholderLength, item);
				return url;
			}

			if (_stricmp(planner, kDefaultPlannerName) != 0)
				LOG_WARN("CodexPlanner: unknown Planner '%s'; using the %s.", planner, kDefaultPlannerName);

			std::string url = kDefaultPlannerBaseUrl;
			if (!plannerId.empty())
				url += "?add=" + UrlEncode(plannerId);
			return url;
		}
	}

	void Launch(const std::string& uniqueItemName, const std::string& displayName)
	{
		const std::string plannerId = FindPlannerId(uniqueItemName, displayName);
		if (plannerId.empty())
			LOG_INFO("CodexPlanner: no planner id for item '%s' (%s); opening the planner without an item.",
				uniqueItemName.c_str(), displayName.c_str());

		const std::string url = BuildUrl(plannerId);
		if (!IsSafeUrl(url))
		{
			LOG_WARN("CodexPlanner: not opening '%s'; the planner URL must be http:// or https:// with printable ASCII and no spaces, quotes or braces. Check Planner/CustomUrlTemplate.",
				url.c_str());
			return;
		}

		// The URL is printable ASCII, so widening byte by byte is exact.
		const std::wstring wideUrl(url.begin(), url.end());
		const INT_PTR result = reinterpret_cast<INT_PTR>(
			ShellExecuteW(nullptr, L"open", wideUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
		if (result <= 32)
		{
			LOG_WARN("CodexPlanner: could not open the browser (ShellExecute error %d) for %s", static_cast<int>(result), url.c_str());
		}
		else
		{
			LOG_DEBUG("CodexPlanner: opened %s", url.c_str());
		}
	}
}
