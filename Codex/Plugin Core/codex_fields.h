#pragma once

#include "plugin_interface.h"
#include "Chimera_classes.hpp"
#include "Chimera_structs.hpp"
#include "AuCrafting_classes.hpp"
#include "AuCrafting_structs.hpp"
#include "AuItems_classes.hpp"
#include "AuItems_structs.hpp"
#include "AuActorPlacement_classes.hpp"
#include "plugin_map.h"

#include <string>

// Every member of a game class Codex reads, taken from the loader's map for the
// running game build (see plugin_map.h). Resolved once in OnPluginLoadHooks;
// read on the game thread with In(object), which is null when the object is
// null. Structs Codex only reads through those members are covered by a size
// check instead of a field per member.

#define CODEX_FIELD(Class, Member) SR_FIELD(decltype(SDK::Class::Member), Class, Member, 0)

namespace CodexFields
{
	inline auto UIItemType        = CODEX_FIELD(UAuItemDataBase, UIItemType);
	inline auto UniqueItemName    = CODEX_FIELD(UAuItemDataBase, UniqueItemName);
	inline auto ItemName          = CODEX_FIELD(UAuItemDataBase, ItemName);
	inline auto ItemIcon          = CODEX_FIELD(UAuItemDataBase, ItemIcon);
	inline auto EntityType        = CODEX_FIELD(UAuActorPlacementData, EntityType);
	inline auto BuildingType      = CODEX_FIELD(UCrBuildingData, Type);
	inline auto BuildingName      = CODEX_FIELD(UCrBuildingData, BuildingName);
	inline auto LinkedCollections = CODEX_FIELD(UCrItemRecipeCollectionSubsystem, LinkedCollections);
	inline auto Recipes           = CODEX_FIELD(UCrItemRecipeCollection, Recipes);
	inline auto RecipeBuildTime   = CODEX_FIELD(UAuItemRecipeData, BuildTime);
	inline auto RecipeDisplayText = CODEX_FIELD(UCrItemRecipeData, DisplayText);
	inline auto CraftingParameters = CODEX_FIELD(UCrBuildingCraftingTrait, CraftingParameters);
	inline auto CrafterToRecipeMap = CODEX_FIELD(ACrCraftingRecipeOwner, CrafterToRecipeMap);

	inline auto SimpleItemSize        = SR_SIZE_CHECK(FAuSimpleItem);
	inline auto EntityTypeSize        = SR_SIZE_CHECK(FAuAPMassSpawnedEntityType);
	inline auto CraftingParametersSize = SR_SIZE_CHECK(FCrCraftingParameters);
	inline auto NestedCollectionsSize = SR_SIZE_CHECK(FNestedItemRecipeCollectionArray);

	// Asks the loader for every row above. False when any did not resolve; the
	// loader has then recorded the failure and refuses the plugin.
	inline bool Resolve(IPluginSelf* self, IPluginHookScanner* scanner)
	{
		return PluginMap::ResolveFields(scanner, self,
			UIItemType, UniqueItemName, ItemName, ItemIcon, EntityType, BuildingType, BuildingName,
			LinkedCollections, Recipes, RecipeBuildTime, RecipeDisplayText, CraftingParameters, CrafterToRecipeMap,
			SimpleItemSize, EntityTypeSize, CraftingParametersSize, NestedCollectionsSize);
	}

	// The member's value, or a fallback when the member did not resolve.
	template <class T, class Object>
	T ValueOr(const PluginField<T>& field, const Object* object, T fallback = T{})
	{
		const T* at = field.In(static_cast<const void*>(object));
		return at ? *at : fallback;
	}

	// An item's unique name and its localized name. Empty when unresolved.
	inline std::string UniqueNameOf(const SDK::UAuItemDataBase* item)
	{
		const SDK::FName* name = UniqueItemName.In(static_cast<const void*>(item));
		return name ? name->ToString() : std::string();
	}

	inline std::string DisplayNameOf(const SDK::UAuItemDataBase* item)
	{
		const SDK::FText* text = ItemName.In(static_cast<const void*>(item));
		return text ? SDK::UKismetTextLibrary::Conv_TextToString(*text).ToString() : std::string();
	}

	// The building's type, or an out-of-range value when it did not resolve.
	inline auto TypeOf(const SDK::UCrBuildingData* building)
	{
		using Type = decltype(SDK::UCrBuildingData::Type);
		return ValueOr<Type>(BuildingType, building, static_cast<Type>(0xFF));
	}
}
