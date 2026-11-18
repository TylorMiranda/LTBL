#pragma once

// Let There Be Light (LTBL) - Torch Bearers
//
// NPCs and followers draw torches when it is dark where they stand, indoors or out,
// and put them away when it is bright again. Native SKSE logic, no Papyrus polling.
// - Separate draw and stow thresholds plus a dwell time, so NPCs never flicker equip/unequip.
// - One light per group: no torch if someone nearby already carries one.
// - Stowed when mounted, sitting, sleeping, sneaking, or inside wooden/civilized buildings.
// - Combat freezes the current state. Guards are left to vanilla behavior by default.

struct LTBLTorchBearers : Feature
{
	virtual inline std::string GetName() override { return "LTBL Torch Bearers"; }
	virtual std::string GetDisplayName() override { return T("feature.ltbl_torch_bearers.name", "Torch Bearers"); }
	virtual inline std::string GetShortName() override { return "LTBLTorchBearers"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.ltbl_torch_bearers.description", "NPCs and followers light torches when it gets dark."),
			{ T("feature.ltbl_torch_bearers.key_feature_1", "Based on the real light where each NPC stands"),
				T("feature.ltbl_torch_bearers.key_feature_2", "One torch per group, no flickering on and off"),
				T("feature.ltbl_torch_bearers.key_feature_3", "Never inside wooden or civilized buildings"),
				T("feature.ltbl_torch_bearers.key_feature_4", "Put away when mounted, sitting, sleeping or sneaking") } };
	}

	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	virtual void DataLoaded() override;
	virtual void GameLoaded() override;
	virtual void EarlyPrepass() override;

	struct Settings
	{
		bool Enabled = true;
		float DrawBelowLight = 20.0f;  // draw a torch below this light level
		float StowAboveLight = 35.0f;  // put it away above this light level
		float DwellSeconds = 1.5f;     // how long a condition must hold before acting
		float GroupRadius = 350.0f;    // game units; ~5 meters
		float CheckInterval = 0.5f;    // seconds between evaluations
		bool StowWhenSneaking = true;
		bool IncludeGuards = false;
		bool IncludeFollowers = true;
	};

	Settings settings;

	// Called on the main thread through the SKSE task queue.
	void Evaluate(float elapsed);

private:
	struct ActorState
	{
		float darkTime = 0.0f;
		float brightTime = 0.0f;
		bool managed = false;  // we equipped this torch, so we may put it away
	};

	std::unordered_map<RE::FormID, ActorState> states;
	RE::TESObjectLIGH* torch = nullptr;
	float timer = 0.0f;
	std::atomic<bool> taskPending = false;
	int lastManagedCount = 0;
};
