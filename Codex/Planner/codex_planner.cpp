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
		constexpr const char* kCustomPlannerName = "Custom";
		constexpr size_t      kMaxUrlLength = 2048;

		// The StarRupture Planner's item catalog (update2_QoL,
		// public/game-data/update2_QoL/items_catalog.json): planner id and the
		// in-game display name. Game items are matched to it by comparing
		// letters and digits only, against the game's UniqueItemName first and
		// its display name second ("CalciumBlock" and "Calcium Block" are the
		// planner's "block_calcium").
		//
		// The other two columns are the same item's id on starruptureplanner.com
		// (its /database page) and starrupturecalc.com (its /items/ pages), found
		// by the display name. They are empty where that site has no such item or
		// its calculator cannot target it, so the planner opens without an item.
		struct PlannerItem
		{
			const char* id;
			const char* name;
			const char* starruptureplannerId;
			const char* starrupturecalcId;
		};

		constexpr PlannerItem kPlannerItems[] = {
			{ "accumulator", "Accumulator", "", "accumulator" },
			{ "aerogel", "Aerogel", "", "" },
			{ "airlock", "Airlock", "", "airlock" },
			{ "anti-radiationcovers", "Anti-Radiation Covers", "", "anti-radiation-covers" },
			{ "applicator", "Applicator", "comp_applicator", "syringe" },
			{ "basic_building", "Basic Building Material", "item_bbm", "basic-building-material" },
			{ "basicfuel", "Basic Fuel", "", "" },
			{ "battery", "Battery", "comp_battery", "battery" },
			{ "biofilament", "Biofilament", "", "biofilament" },
			{ "calcite_sheets", "Calcite Sheets", "sheet_calcite", "calcite-sheets" },
			{ "block_calcium", "Calcium Block", "block_calcium", "calcium-block" },
			{ "ore_calcium", "Calcium Ore", "", "calcium-ore" },
			{ "powder_calcium", "Calcium Powder", "powder_calcium", "calcium-powder" },
			{ "carbonsonar", "Carbon Sonar", "", "carbon-sonar" },
			{ "ceramics", "Ceramics", "comp_ceramics", "ceramics" },
			{ "chemicals", "Chemicals", "comp_chemicals", "explosive" },
			{ "condenser", "Condenser", "", "condenser" },
			{ "converter", "Converter", "", "converter" },
			{ "cyclotrone", "Cyclotrone", "", "cyclotrone" },
			{ "dome", "Dome", "", "dome" },
			{ "electromagnet", "Electromagnet", "", "electromagnet" },
			{ "electromagnetic_coil", "Electromagnetic Coil", "comp_electromagnetic_coil", "electromagnetic-coil" },
			{ "electronics", "Electronics", "comp_electronics", "electronics" },
			{ "epoxy", "Epoxy", "", "" },
			{ "grenade", "Explosive Charge", "", "grenade" },
			{ "generator", "Generator", "", "generator" },
			{ "glass", "Glass", "comp_glass", "glass" },
			{ "goethiteingot", "Goethite Ingot", "", "goethite-ingot" },
			{ "goethitelattice", "Goethite Lattice", "", "" },
			{ "goethiteore", "Goethite Ore", "", "goethite-ore" },
			{ "goethitepowder", "Goethite Powder", "", "" },
			{ "hardening_agent", "Hardening Agent", "comp_hardening_agent", "hardening-agent" },
			{ "heat_sheet", "Heat Resistant Sheet", "sheet_heat_resistant", "heat-resistant-sheet" },
			{ "heatshield", "Heat Shield", "", "heat-shield" },
			{ "heavy_ammo", "Heavy Ammo", "ammo_heavy", "heavy-ammo-item" },
			{ "helium", "Helium-3", "", "helium-ore" },
			{ "impeller", "Impeller", "comp_impeller", "impeller" },
			{ "inductor", "Inductor", "comp_inductor", "coil" },
			{ "intermediate_building", "Intermediate Building Material", "item_ibm", "intermediate-building-material" },
			{ "ionbomb", "Ion Bomb", "", "ion-bomb" },
			{ "ion_injector", "Ion Injector", "", "ion-injector" },
			{ "ionstabilizer", "Ion Stabilizer", "", "ion-stabilizer" },
			{ "ionthrusters", "Ion Thrusters", "", "ion-thrusters" },
			{ "laser_emitter", "Laser Emitter", "", "laser-emitter" },
			{ "lens", "Lens", "", "lens" },
			{ "liquid_helium", "Liquid Helium", "", "liquid-helium" },
			{ "nanofiber", "Nanofiber", "", "nanofibre" },
			{ "nanosyringe", "Nanosyringe", "", "nanosyringe" },
			{ "nozzle", "Nozzle", "comp_nozzle", "nozzle" },
			{ "onboardinstruments", "Onboard Instruments", "", "onboard-instruments" },
			{ "organiccompound", "Organic Compound", "", "" },
			{ "pistol_ammo", "Pistol Ammo", "ammo_pistol", "pistol-ammo-item" },
			{ "uberoil", "Plastic", "", "" },
			{ "plasticdust", "Plastic Dust", "", "plastic-dust" },
			{ "magicoilore", "Powerium", "", "" },
			{ "pressure_tank", "Pressure Tank", "", "pressure-tank" },
			{ "pressurized_helium", "Pressurized Helium", "fluid_pressurized_helium", "pressurized-helium" },
			{ "pseudorubberinsulator", "Pseudorubber Insulator", "", "pseudorubber-insulator" },
			{ "pump", "Pump", "comp_pump", "pump" },
			{ "quantumtranqualizer", "Quantum Tranquilizer", "", "quantum-tranqualizer" },
			{ "rnastabilizer", "RNA Stabilizer", "", "rna-stabilizer" },
			{ "rtsc", "RTSC", "", "rtsc" },
			{ "rtscgenerator", "RTSC Generator", "", "rtsc-generator" },
			{ "refinedoil", "Refined Oil", "", "" },
			{ "reinforcedframe", "Reinforced Frame", "", "" },
			{ "reinforcedhousing", "Reinforced Housing", "", "" },
			{ "resonator", "Resonator", "", "resonator" },
			{ "rocketengine", "Rocket Engine", "", "rocket-engine" },
			{ "rocketfuel", "Rocket Fuel", "", "rocket-fuel" },
			{ "rotor", "Rotor", "comp_rotor", "rotor" },
			{ "satellitebody", "Satellite Body", "", "satellite-body" },
			{ "scafolding", "Scaffolding", "", "scafolding" },
			{ "scanner", "Scanner", "", "scanner" },
			{ "shotgun_ammo", "Shotgun Ammo", "ammo_shotgun", "shotgun-ammo-item" },
			{ "stabilizer", "Stabilizer", "comp_stabilizer", "gyroscope" },
			{ "standard_ammo", "Standard Ammo", "ammo_standard", "standard-ammo-item" },
			{ "stator", "Stator", "comp_stator", "stator" },
			{ "substancefhs-135-493", "Substance #FHS-135-493", "", "substance-fhs-135-493" },
			{ "ore_sulfur", "Sulfur Ore", "", "sulphur-ore" },
			{ "sulfuric_acid", "Sulfuric Acid", "fluid_sulfuric_acid", "sulphuric-acid" },
			{ "superconductor", "Superconductor", "", "superconductor" },
			{ "supermagnet", "Supermagnet", "comp_supermagnet", "supermagnet" },
			{ "syntheticdna", "Synthetic DNA", "", "synthetic-dna" },
			{ "syntheticprotein", "Synthetic Protein", "", "synthetic-protein" },
			{ "synthetic_resin", "Synthetic Resin", "", "synthetic-resin" },
			{ "synthetic_silicon", "Synthetic Silicon", "comp_synthetic_silicon", "silica-sand" },
			{ "bar_titanium", "Titanium Bar", "ingot_titanium", "titanium-bar" },
			{ "titanium_beam", "Titanium Beam", "beam_titanium", "titanium-beam" },
			{ "titanium_housing", "Titanium Housing", "comp_titanium_housing", "titanium-housing" },
			{ "ore_titanium", "Titanium Ore", "", "titanium-ore" },
			{ "titanium_rod", "Titanium Rod", "rod_titanium", "titanium-rod" },
			{ "titanium_sheet", "Titanium Sheet", "sheet_titanium", "titanium-sheet" },
			{ "ubertitanium", "Titanoferrite Ingot", "", "uber-titanium" },
			{ "trackingdevice", "Tracking Device", "", "tracking-device" },
			{ "tube", "Tube", "comp_tube", "tube" },
			{ "turbine", "Turbine", "comp_turbine", "turbine" },
			{ "uberfilament", "Uberfilament", "", "uberfilament" },
			{ "valve", "Valve", "comp_valve", "valve" },
			{ "bar_wolfram", "Wolfram Bar", "ingot_wolfram", "wolfram-bar" },
			{ "ore_wolfram", "Wolfram Ore", "", "wolfram-ore" },
			{ "wolfram_plate", "Wolfram Plate", "plate_wolfram", "wolfram-plate" },
			{ "powder_wolfram", "Wolfram Powder", "powder_wolfram", "wolfram-powder" },
			{ "uberwolfram", "Wolfram Steel Ingot", "", "uber-wolfram" },
			{ "wolfram_wire", "Wolfram Wire", "wire_wolfram", "wolfram-wire" },
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

		const std::unordered_map<std::string, const PlannerItem*>& ItemIndex()
		{
			static const std::unordered_map<std::string, const PlannerItem*> index = []
			{
				std::unordered_map<std::string, const PlannerItem*> map;
				for (const PlannerItem& item : kPlannerItems)
				{
					map.emplace(Normalize(item.id), &item);
					map.emplace(Normalize(item.name), &item);
				}
				for (const ItemAlias& alias : kItemAliases)
					for (const PlannerItem& item : kPlannerItems)
						if (std::strcmp(item.id, alias.id) == 0)
							map.emplace(alias.gameKey, &item);
				return map;
			}();
			return index;
		}

		// The planner's entry for a game item, or null if the planner has no such item.
		const PlannerItem* FindPlannerItem(const std::string& uniqueItemName, const std::string& displayName)
		{
			const auto& index = ItemIndex();
			for (const std::string* name : { &uniqueItemName, &displayName })
			{
				auto it = index.find(Normalize(*name));
				if (it != index.end())
					return it->second;
			}
			return nullptr;
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

		// Replaces every occurrence of a placeholder in the URL template.
		void ReplacePlaceholder(std::string& url, const char* placeholder, const std::string& value)
		{
			const size_t placeholderLength = std::strlen(placeholder);
			for (size_t pos = url.find(placeholder); pos != std::string::npos; pos = url.find(placeholder, pos + value.size()))
				url.replace(pos, placeholderLength, value);
		}

		// A planner the Launch planner button can open. itemId picks the item's
		// id on that planner. A planner with no item link leaves itemId and
		// itemParam null and always opens at url.
		struct PlannerSite
		{
			const char* name;      // value of Planner/Planner; the first site is the default
			const char* url;       // opened as is, or with itemParam and the item id appended
			const char* itemParam;
			const char* PlannerItem::* itemId;
		};

		constexpr PlannerSite kPlannerSites[] = {
			{ "StarRupture Planner",     "https://starrupture.goodstuff.software/",       "?add=",  &PlannerItem::id },
			{ "starruptureplanner.com",  "https://starruptureplanner.com/calculator",     "?item=", &PlannerItem::starruptureplannerId },
			{ "starrupturecalc.com",     "https://www.starrupturecalc.com/",              "?item=", &PlannerItem::starrupturecalcId },
			{ "starrupture-planner.com", "https://www.starrupture-planner.com/planner/",  nullptr,  nullptr },
		};

		std::string BuildUrl(const PlannerItem* item, const std::string& uniqueItemName, const std::string& displayName)
		{
			const char* planner = CodexConfig::Config::GetPlanner();
			if (_stricmp(planner, kCustomPlannerName) == 0)
			{
				std::string url = CodexConfig::Config::GetPlannerUrlTemplate();
				ReplacePlaceholder(url, "{item}", UrlEncode(item ? item->id : ""));
				ReplacePlaceholder(url, "{name}", UrlEncode(displayName));
				ReplacePlaceholder(url, "{game}", UrlEncode(uniqueItemName));
				return url;
			}

			const PlannerSite* site = &kPlannerSites[0];
			for (const PlannerSite& candidate : kPlannerSites)
				if (_stricmp(planner, candidate.name) == 0)
					site = &candidate;
			if (_stricmp(planner, site->name) != 0)
				LOG_WARN("CodexPlanner: unknown Planner '%s'; using the %s.", planner, site->name);

			std::string url = site->url;
			if (!site->itemId)
				return url;

			const char* itemId = item ? item->*site->itemId : "";
			if (*itemId)
				url += site->itemParam + UrlEncode(itemId);
			else
				LOG_INFO("CodexPlanner: no %s id for item '%s' (%s); opening the planner without an item.",
					site->name, uniqueItemName.c_str(), displayName.c_str());
			return url;
		}
	}

	void Launch(const std::string& uniqueItemName, const std::string& displayName)
	{
		const std::string url = BuildUrl(FindPlannerItem(uniqueItemName, displayName), uniqueItemName, displayName);
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
