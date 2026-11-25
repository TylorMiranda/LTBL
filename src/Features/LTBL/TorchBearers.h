#pragma once

#include <optional>
#include <unordered_map>

// Let There Be Light (LTBL) - Torch Bearers
//
// NPCs and followers draw torches when it is dark where they stand, indoors or out,
// and put them away when it is bright again. Native SKSE logic, no Papyrus polling.
// - Separate draw and stow thresholds plus a dwell time, so NPCs never flicker equip/unequip.
// - Each NPC waits a random time before acting, so a town's torches light and go out one by one.
// - One light per group: no torch if someone nearby already carries one.
// - Stowed when mounted, sitting, sleeping, sneaking, or inside wooden/civilized buildings.
// - Combat freezes the current state. Guards are left to vanilla behavior by default.
//
// Animated sequences (LTBL ships them as an Open Animation Replacer pack):
// - Lighting: reach over the left shoulder, draw the torch, strike flint at the head, raise to carry.
// - Putting out: walk to the nearest water and douse it there (kneel at a shore or puddle, lean
//   over a trough, barrel or well), or snuff it on the ground if no water is close; then stow it.
// The clips replace a vanilla idle only while LTBL marks the actor (Variable10 = 7101..7104).
// During a clip the torch in hand is a visual prop with its own flame and light, so the
// engine's left-hand torch pose never fights the animation. The real torch is equipped at the
// exact frame the clip reaches the vanilla carry pose. Without OAR or the pack, torches change
// without animation, so nothing wrong ever plays.

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
				T("feature.ltbl_torch_bearers.key_feature_2", "Torches light and go out one by one, not all at once"),
				T("feature.ltbl_torch_bearers.key_feature_3", "Doused in nearby water, or snuffed on the ground"),
				T("feature.ltbl_torch_bearers.key_feature_4", "Never inside wooden or civilized buildings") } };
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
		float DrawDelayMax = 12.0f;    // extra random wait before lighting, per NPC (0 to this)
		float StowDelayMin = 5.0f;     // random wait before putting out, per NPC
		float StowDelayMax = 90.0f;
		float GroupRadius = 350.0f;    // game units; ~5 meters
		float CheckInterval = 0.5f;    // seconds between evaluations
		bool StowWhenSneaking = true;
		bool IncludeGuards = false;
		bool IncludeFollowers = true;
		// Putting out at water
		bool DouseInWater = true;
		float WaterSearchRadius = 1800.0f;  // game units; ~25 meters
		float WalkTimeout = 25.0f;          // give up walking and snuff where they are
		bool ForceAnimations = false;  // play the clips even if the pack is not detected
	};

	Settings settings;

	// Called on the main thread through the SKSE task queue.
	void Evaluate(float elapsed);
	void TickSequences(float dt);

private:
	enum class Phase : std::uint8_t
	{
		kIdle,
		kLighting,
		kWalkingToWater,
		kSnuffing
	};

	enum class SnuffKind : std::int32_t
	{
		kNone = 0,
		kLight = 1,
		kWater = 2,  // shore or puddle: kneel
		kGround = 3,
		kBasin = 4   // trough, barrel, well, fountain: lean over
	};

	struct ActorState
	{
		float darkTime = 0.0f;
		float brightTime = 0.0f;
		float drawDelay = -1.0f;  // rolled when darkness begins
		float stowDelay = -1.0f;  // rolled when brightness begins
		bool managed = false;     // we equipped this torch, so we may put it away
		Phase phase = Phase::kIdle;
		float phaseTime = 0.0f;
		bool step1 = false;  // lighting: prop appears / putting out: flame dies
		bool step2 = false;  // lighting: flame catches / putting out: torch stowed
		SnuffKind kind = SnuffKind::kNone;
		float savedMarker = 0.0f;
		bool markerSet = false;
		bool propOn = false;
		bool propLit = false;
		RE::NiPointer<RE::NiNode> prop;
		RE::NiPointer<RE::NiPointLight> propLight;
		RE::NiPoint3 walkTarget{};
		RE::ObjectRefHandle marker{};
		RE::ActorHandle handle{};
	};

	std::unordered_map<RE::FormID, ActorState> states;
	RE::TESObjectLIGH* torch = nullptr;
	RE::TESBoundObject* xMarker = nullptr;
	bool animationsInstalled = false;
	float timer = 0.0f;
	std::atomic<bool> taskPending = false;
	std::atomic<bool> tickPending = false;
	std::atomic<int> activeSequences = 0;
	int lastManagedCount = 0;

	bool UseAnimations() const { return animationsInstalled || settings.ForceAnimations; }
	void StartLighting(RE::Actor* actor, ActorState& st);
	void StartPuttingOut(RE::Actor* actor, ActorState& st);
	void BeginSnuffAnimation(RE::Actor* actor, ActorState& st, SnuffKind kind);
	bool PlayClip(RE::Actor* actor, ActorState& st, SnuffKind kind);
	void RestoreMarker(RE::Actor* actor, ActorState& st);
	void SetProp(RE::Actor* actor, ActorState& st, bool on, bool lit);
	void RemoveProp(ActorState& st);
	void CleanupState(ActorState& st);
	void FinishSequence(RE::Actor* actor, ActorState& st);
	void CancelSequence(RE::Actor* actor, ActorState& st);
	bool FindWater(RE::Actor* actor, RE::NiPoint3& outPoint, RE::TESObjectREFR*& outRef) const;
	void CountActive();
};
