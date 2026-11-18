#pragma once

#include <unordered_set>

// Let There Be Light (LTBL) - Candlelight
//
// Reworks the novice Alteration spell Candlelight from a bright floating orb into a wide,
// soft area of light around the caster that fades gradually with distance. No hot spot.
// Followers never receive it; they use torches (see Torch Bearers).
//
// Tome progression:
// - The first tome teaches the spell, which starts dim.
// - Each Candlelight tome found in dungeon loot makes it brighter, up to full brightness at 10.
// - Tomes are added at runtime as extra loot in urns, coffins and chests of dungeons, caves
//   and ruins (small 1-2, normal 2-4, huge 5+). No loot lists are edited and nothing is replaced.
// - Once 10 are found, no more are added, and unlooted ones LTBL added are removed.
// - Vendors keep selling the tome as normal.

struct LTBLCandlelight :
	Feature,
	RE::BSTEventSink<RE::TESContainerChangedEvent>,
	RE::BSTEventSink<RE::TESCellFullyLoadedEvent>,
	RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
	virtual inline std::string GetName() override { return "LTBL Candlelight"; }
	virtual std::string GetDisplayName() override { return T("feature.ltbl_candlelight.name", "Candlelight Aura"); }
	virtual inline std::string GetShortName() override { return "LTBLCandlelight"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.ltbl_candlelight.description", "Candlelight lights a wide, soft area around you, growing brighter with every tome you find."),
			{ T("feature.ltbl_candlelight.key_feature_1", "Large lit area that fades gradually with distance"),
				T("feature.ltbl_candlelight.key_feature_2", "Starts dim, reaches full brightness after 10 tomes"),
				T("feature.ltbl_candlelight.key_feature_3", "Tomes appear as extra loot in dungeons, caves and ruins"),
				T("feature.ltbl_candlelight.key_feature_4", "Stops appearing once you have found 10") } };
	}

	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	virtual void Load() override;
	virtual void DataLoaded() override;
	virtual void GameLoaded() override;

	struct Settings
	{
		bool Enabled = true;
		// Full brightness (10 tomes)
		std::uint32_t Radius = 1400;  // game units (~20 m)
		float Intensity = 0.55f;
		// Starting brightness (1 tome)
		std::uint32_t StartRadius = 700;
		float StartIntensity = 0.15f;
		float Falloff = 0.6f;  // lower = softer, wider spread
		// Tomes
		bool TomeLoot = true;
		std::uint32_t TomesForFullBrightness = 10;
		bool VendorsStockTome = true;
	};

	Settings settings;

	// Event sinks
	RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>*) override;
	RE::BSEventNotifyControl ProcessEvent(const RE::TESCellFullyLoadedEvent* a_event, RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override;
	RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

	// SKSE co-save
	static void OnSave(SKSE::SerializationInterface* a_intfc);
	static void OnLoad(SKSE::SerializationInterface* a_intfc);
	static void OnRevert(SKSE::SerializationInterface* a_intfc);

private:
	struct Original
	{
		RE::TESObjectLIGH* light;
		std::uint32_t radius;
		float fade;
		float falloff;
	};
	std::vector<Original> originals;

	RE::SpellItem* spell = nullptr;
	RE::TESObjectBOOK* tome = nullptr;

	// Saved per playthrough
	std::uint32_t tomesFound = 0;
	std::unordered_set<RE::FormID> processedCells;
	std::unordered_set<RE::FormID> seededContainers;

	std::uint32_t CurrentLevel() const;
	bool Complete() const { return tomesFound >= std::max<std::uint32_t>(settings.TomesForFullBrightness, 1); }
	void Apply();
	void SeedCell(RE::TESObjectCELL* cell);
	void RemoveUnlootedTomes();
	void StockVendor(RE::Actor* vendor);
};
