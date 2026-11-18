#include "TorchBearers.h"

#include "I18n/I18n.h"
#include "State.h"

#include <array>
#include <unordered_set>

#define I18N_KEY_PREFIX "feature.ltbl_torch_bearers."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LTBLTorchBearers::Settings,
	Enabled,
	DrawBelowLight,
	StowAboveLight,
	DwellSeconds,
	GroupRadius,
	CheckInterval,
	StowWhenSneaking,
	IncludeGuards,
	IncludeFollowers)

namespace
{
	constexpr RE::FormID kTorch01 = 0x0001D4EC;  // Skyrim.esm Torch01

	// Interiors we treat as wooden or civilized: no torches inside.
	constexpr std::array kNoTorchLocationKeywords{
		"LocTypeDwelling"sv,
		"LocTypeHouse"sv,
		"LocTypeInn"sv,
		"LocTypeStore"sv,
		"LocTypePlayerHouse"sv,
		"LocTypeFarm"sv,
		"LocTypeLumberMill"sv,
	};

	bool InNoTorchBuilding(RE::Actor* actor)
	{
		auto* cell = actor->GetParentCell();
		if (!cell || !cell->IsInteriorCell())
			return false;
		auto* loc = cell->GetLocation();
		while (loc) {
			for (auto kw : kNoTorchLocationKeywords) {
				if (loc->HasKeywordString(kw))
					return true;
			}
			// Only the cell's own location decides; a house inside a city is still a house,
			// but a dungeon inside a hold is not a house.
			break;
		}
		return false;
	}

	bool HoldsLight(RE::Actor* actor)
	{
		auto* left = actor->GetEquippedObject(true);
		return left && left->Is(RE::FormType::Light);
	}

	float GetLightAt(RE::Actor* actor)
	{
		auto* high = actor->GetHighProcess();
		return high ? std::max(high->lightLevel, 0.0f) : 100.0f;
	}
}

void LTBLTorchBearers::DrawSettings()
{
	ImGui::Checkbox(T(TKEY("enabled"), "NPCs and followers light torches in the dark"), &settings.Enabled);
	ImGui::SliderFloat(T(TKEY("draw"), "Light torch below light level"), &settings.DrawBelowLight, 0.0f, 100.0f, "%.0f");
	ImGui::SliderFloat(T(TKEY("stow"), "Put away above light level"), &settings.StowAboveLight, 0.0f, 150.0f, "%.0f");
	if (settings.StowAboveLight < settings.DrawBelowLight + 5.0f)
		settings.StowAboveLight = settings.DrawBelowLight + 5.0f;
	ImGui::SliderFloat(T(TKEY("dwell"), "Wait before reacting (s)"), &settings.DwellSeconds, 0.0f, 10.0f, "%.1f");
	ImGui::SliderFloat(T(TKEY("group"), "One torch per group, radius"), &settings.GroupRadius, 0.0f, 1500.0f, "%.0f");
	ImGui::Checkbox(T(TKEY("sneak"), "Put torches away when sneaking"), &settings.StowWhenSneaking);
	ImGui::Checkbox(T(TKEY("followers"), "Include followers"), &settings.IncludeFollowers);
	ImGui::Checkbox(T(TKEY("guards"), "Include guards"), &settings.IncludeGuards);
	ImGui::Text("Torches currently managed: %d", lastManagedCount);
}

void LTBLTorchBearers::LoadSettings(json& o_json) { settings = o_json; }
void LTBLTorchBearers::SaveSettings(json& o_json) { o_json = settings; }
void LTBLTorchBearers::RestoreDefaultSettings() { settings = {}; }

void LTBLTorchBearers::DataLoaded()
{
	torch = RE::TESForm::LookupByID<RE::TESObjectLIGH>(kTorch01);
	if (!torch)
		logger::warn("[LTBL Torch Bearers] Torch01 not found; feature inactive");
	else
		logger::info("[LTBL Torch Bearers] Ready");
}

void LTBLTorchBearers::GameLoaded()
{
	states.clear();
	timer = 0.0f;
}

void LTBLTorchBearers::EarlyPrepass()
{
	if (!settings.Enabled || !torch)
		return;

	float dt = globals::game::deltaTime ? *globals::game::deltaTime : (1.0f / 60.0f);
	timer += std::clamp(dt, 0.0f, 0.25f);
	if (timer < settings.CheckInterval || taskPending)
		return;

	const float elapsed = timer;
	timer = 0.0f;
	taskPending = true;

	// Actor and equipment changes must happen on the main thread.
	if (auto* tasks = SKSE::GetTaskInterface()) {
		tasks->AddTask([this, elapsed]() {
			Evaluate(elapsed);
			taskPending = false;
		});
	} else {
		taskPending = false;
	}
}

void LTBLTorchBearers::Evaluate(float elapsed)
{
	auto* processLists = RE::ProcessLists::GetSingleton();
	auto* equipManager = RE::ActorEquipManager::GetSingleton();
	if (!processLists || !equipManager || !torch)
		return;

	// Gather actors in the high process (near the player).
	std::vector<RE::NiPointer<RE::Actor>> actors;
	actors.reserve(processLists->highActorHandles.size());
	for (auto& handle : processLists->highActorHandles) {
		if (auto actor = handle.get(); actor)
			actors.push_back(actor);
	}

	// Positions of everyone currently holding a light, for the one-per-group rule.
	std::vector<std::pair<RE::Actor*, RE::NiPoint3>> lightHolders;
	for (auto& a : actors) {
		if (HoldsLight(a.get()))
			lightHolders.emplace_back(a.get(), a->GetPosition());
	}

	std::unordered_set<RE::FormID> seen;
	int managedCount = 0;

	for (auto& ptr : actors) {
		auto* actor = ptr.get();
		if (!actor || actor->IsPlayerRef() || actor->IsDead() || actor->IsDisabled() || actor->IsChild())
			continue;
		if (!actor->HasKeywordString("ActorTypeNPC"))
			continue;
		if (actor->IsGuard() && !settings.IncludeGuards)
			continue;
		if (actor->IsPlayerTeammate() && !settings.IncludeFollowers)
			continue;

		const auto id = actor->GetFormID();
		seen.insert(id);
		auto& st = states[id];

		const bool holding = HoldsLight(actor);
		if (st.managed && !holding)
			st.managed = false;  // the game or the player took it away

		// Combat freezes whatever they have.
		if (actor->IsInCombat()) {
			if (st.managed)
				++managedCount;
			continue;
		}

		const auto* actorState = actor->AsActorState();
		const bool sitting = actorState && actorState->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal;
		const bool blocked = actor->IsOnMount() || sitting ||
		                     (settings.StowWhenSneaking && actor->IsSneaking()) ||
		                     InNoTorchBuilding(actor);

		if (blocked) {
			st.darkTime = st.brightTime = 0.0f;
			if (st.managed && holding) {
				equipManager->UnequipObject(actor, torch);
				st.managed = false;
			}
			continue;
		}

		const float light = GetLightAt(actor);
		if (light < settings.DrawBelowLight) {
			st.darkTime += elapsed;
			st.brightTime = 0.0f;
		} else if (light > settings.StowAboveLight) {
			st.brightTime += elapsed;
			st.darkTime = 0.0f;
		} else {
			// Between thresholds: hold current state.
			st.darkTime = st.brightTime = 0.0f;
		}

		if (!holding && st.darkTime >= settings.DwellSeconds) {
			// One light per group.
			bool someoneNearbyLit = false;
			const auto pos = actor->GetPosition();
			for (auto& [other, otherPos] : lightHolders) {
				if (other != actor && pos.GetDistance(otherPos) < settings.GroupRadius) {
					someoneNearbyLit = true;
					break;
				}
			}
			if (!someoneNearbyLit) {
				auto counts = actor->GetInventoryCounts([&](RE::TESBoundObject& obj) { return &obj == torch; });
				if (counts.empty())
					actor->AddObjectToContainer(torch, nullptr, 1, nullptr);
				equipManager->EquipObject(actor, torch);
				st.managed = true;
				st.darkTime = 0.0f;
				lightHolders.emplace_back(actor, pos);
			}
		} else if (holding && st.managed && st.brightTime >= settings.DwellSeconds) {
			equipManager->UnequipObject(actor, torch);
			st.managed = false;
			st.brightTime = 0.0f;
		}

		if (st.managed)
			++managedCount;
	}

	// Forget actors that left the high process.
	std::erase_if(states, [&](const auto& kv) { return !seen.contains(kv.first); });
	lastManagedCount = managedCount;
}
