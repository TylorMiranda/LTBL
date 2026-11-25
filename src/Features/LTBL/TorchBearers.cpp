#include "TorchBearers.h"

#include "I18n/I18n.h"
#include "State.h"

#include <array>
#include <filesystem>
#include <numbers>
#include <random>
#include <unordered_set>

#define I18N_KEY_PREFIX "feature.ltbl_torch_bearers."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LTBLTorchBearers::Settings,
	Enabled,
	DrawBelowLight,
	StowAboveLight,
	DwellSeconds,
	DrawDelayMax,
	StowDelayMin,
	StowDelayMax,
	GroupRadius,
	CheckInterval,
	StowWhenSneaking,
	IncludeGuards,
	IncludeFollowers,
	DouseInWater,
	WaterSearchRadius,
	WalkTimeout,
	ForceAnimations)

namespace
{
	constexpr RE::FormID kTorch01 = 0x0001D4EC;  // Skyrim.esm Torch01
	constexpr RE::FormID kXMarker = 0x0000003B;  // Skyrim.esm XMarker
	constexpr const char* kHostEvent = "IdlePickup_Ground";       // vanilla single-play idle the pack replaces
	constexpr const char* kStopEvent = "IdleForceDefaultState";
	constexpr float kMarkerBase = 7100.0f;                       // Variable10 = 7100 + SnuffKind

	// Timings baked into the clips (seconds). a/b: lighting = prop appears / flame catches;
	// putting out = flame dies / torch stowed. dur = clip length.
	struct ClipTiming
	{
		float a, b, dur;
	};
	constexpr ClipTiming kLightClip{ 0.62f, 2.40f, 3.60f };
	// Lighting clip: flint out of the pouch / back in, and the four downstrokes across the head.
	constexpr float kFlintOutAt = 1.12f;
	constexpr float kFlintBackAt = 2.88f;
	constexpr std::array<float, 4> kStrikeAt{ 1.52f, 1.80f, 2.07f, 2.37f };
	// Vanilla Skyrim.esm records (sound replacers apply).
	constexpr RE::FormID kStrikeSound = 0x000F69C2;   // WPNImpactBladeVsMetalSkin, pitched up and quiet = flint on steel
	constexpr RE::FormID kIgniteSound = 0x00042526;   // TRPOilIgniteLowSD: the torch catching
	constexpr RE::FormID kFireOutSound = 0x001080CC;  // FXFireOutSD: flame dies
	constexpr const char* kSparkModel = "Effects\\ImpactEffects\\FXMetalSparkImpact01.nif";
	constexpr const char* kFlintModel = "meshes\\LTBL\\LTBLFlint.nif";
	const RE::NiPoint3 kTorchHeadLocal{ 0.0f, 30.0f, 0.8f };  // vanilla torch.nif: head center above the grip
	constexpr ClipTiming kGroundClip{ 1.25f, 2.78f, 3.40f };
	constexpr ClipTiming kWaterClip{ 1.15f, 3.02f, 3.60f };
	constexpr ClipTiming kBasinClip{ 0.95f, 2.42f, 3.00f };

	const ClipTiming& TimingFor(std::int32_t kind)
	{
		switch (kind) {
		case 1:
			return kLightClip;
		case 2:
			return kWaterClip;
		case 4:
			return kBasinClip;
		default:
			return kGroundClip;
		}
	}
	constexpr const char* kAnimationFolder = "Data/meshes/actors/character/animations/OpenAnimationReplacer/LTBL_TorchBearers";
	constexpr float kArriveDistance = 160.0f;

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

	// Model path fragments of objects that hold water.
	constexpr std::array kWaterObjectModels{
		"trough"sv,
		"welltop"sv,
		"wellbase"sv,
		"\\well"sv,
		"waterbarrel"sv,
		"rainbarrel"sv,
		"barrelwater"sv,
		"waterbucket"sv,
		"fountain"sv,
		"puddle"sv,
	};

	bool InNoTorchBuilding(RE::Actor* actor)
	{
		auto* cell = actor->GetParentCell();
		if (!cell || !cell->IsInteriorCell())
			return false;
		// Only the cell's own location decides; a house inside a city is still a house,
		// but a dungeon inside a hold is not a house.
		auto* loc = cell->GetLocation();
		if (!loc)
			return false;
		for (auto kw : kNoTorchLocationKeywords) {
			if (loc->HasKeywordString(kw))
				return true;
		}
		return false;
	}

	bool HoldsLight(RE::Actor* actor)
	{
		auto* left = actor->GetEquippedObject(true);
		return left && left->Is(RE::FormType::Light);
	}

	// Rebuild an actor's 3D on the next main-thread tick so a torch shows or hides its model and light.
	void Refresh3D(RE::Actor* actor)
	{
		auto handle = actor->GetHandle();
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([handle]() {
				if (auto a = handle.get(); a && a->Is3DLoaded())
					a->Update3DModel();
			});
		}
	}

	float GetLightAt(RE::Actor* actor)
	{
		auto* high = actor->GetHighProcess();
		return high ? std::max(high->lightLevel, 0.0f) : 100.0f;
	}

	std::mt19937& Rng()
	{
		static std::mt19937 rng{ std::random_device{}() };
		return rng;
	}

	float RandomFloat(float lo, float hi)
	{
		if (hi <= lo)
			return lo;
		return std::uniform_real_distribution<float>(lo, hi)(Rng());
	}

	std::string ToLower(std::string_view s)
	{
		std::string out(s);
		std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
	}

	bool IsWaterObject(RE::TESObjectREFR* ref)
	{
		auto* base = ref ? ref->GetBaseObject() : nullptr;
		auto* model = base ? base->As<RE::TESModel>() : nullptr;
		const char* path = model ? model->GetModel() : nullptr;
		if (!path || !*path)
			return false;
		const auto lower = ToLower(path);
		for (auto frag : kWaterObjectModels) {
			if (lower.find(frag) != std::string::npos)
				return true;
		}
		return false;
	}

	// Ask the actor to walk to a reference (Papyrus Actor.PathToReference, latent).
	bool PathToReference(RE::Actor* actor, RE::TESObjectREFR* target)
	{
		auto* skyrimVM = RE::SkyrimVM::GetSingleton();
		if (!skyrimVM || !skyrimVM->impl || !actor || !target)
			return false;
		auto vm = skyrimVM->impl;
		auto* policy = vm->GetObjectHandlePolicy();
		if (!policy)
			return false;
		const auto handle = policy->GetHandleForObject(RE::Actor::FORMTYPE, actor);
		if (handle == policy->EmptyHandle())
			return false;
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
		auto* args = RE::MakeFunctionArguments(static_cast<RE::TESObjectREFR*>(target), 0.5f);
		return vm->DispatchMethodCall(handle, "Actor", "PathToReference", args, callback);
	}

	RE::NiNode* ShieldNode(RE::Actor* actor)
	{
		auto* root = actor ? actor->Get3D(false) : nullptr;
		auto* obj = root ? root->GetObjectByName("SHIELD") : nullptr;
		return obj ? obj->AsNode() : nullptr;
	}

	RE::NiPointer<RE::NiNode> LoadModel(const std::string& path)
	{
		RE::BSModelDB::DBTraits::ArgsType args{};
		RE::NiPointer<RE::NiNode> nif;
		if (RE::BSModelDB::Demand(path.c_str(), nif, args) != RE::BSResource::ErrorCode::kNone || !nif)
			return nullptr;
		auto* clone = nif->Clone();
		return RE::NiPointer<RE::NiNode>(clone ? clone->AsNode() : nullptr);
	}

	// The torch's own model, from the torch form, so mesh and texture replacers apply.
	RE::NiPointer<RE::NiNode> LoadTorchModel(RE::TESObjectLIGH* torch)
	{
		const char* model = torch ? torch->GetModel() : nullptr;
		if (!model || !*model)
			return nullptr;
		return LoadModel(std::string("meshes\\") + model);
	}

	void PlaySoundAt(RE::FormID id, const RE::NiPoint3& pos, float volume, float frequency)
	{
		auto* desc = RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(id);
		auto* audio = RE::BSAudioManager::GetSingleton();
		if (!desc || !audio)
			return;
		RE::BSSoundHandle h;
		if (!audio->GetSoundHandle(h, desc) || !h.IsValid())
			return;
		h.SetPosition(pos);
		h.SetVolume(volume);
		if (frequency != 1.0f)
			h.SetFrequency(frequency);
		h.Play();
	}

	RE::NiNode* RightHandNode(RE::Actor* actor)
	{
		auto* root = actor ? actor->Get3D(false) : nullptr;
		auto* obj = root ? root->GetObjectByName("NPC R Hand [RHnd]") : nullptr;
		return obj ? obj->AsNode() : nullptr;
	}

	void FaceTowards(RE::Actor* actor, const RE::NiPoint3& point)
	{
		const auto pos = actor->GetPosition();
		const float dx = point.x - pos.x;
		const float dy = point.y - pos.y;
		if (dx * dx + dy * dy > 1.0f)
			actor->SetHeading(std::atan2(dx, dy));
	}

	void DeleteMarker(RE::ObjectRefHandle& marker)
	{
		if (auto ref = marker.get()) {
			ref->Disable();
			ref->SetDelete(true);
		}
		marker = {};
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

	if (ImGui::TreeNodeEx(T(TKEY("timing"), "One by one"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat(T(TKEY("draw_delay"), "Random wait before lighting, up to (s)"), &settings.DrawDelayMax, 0.0f, 60.0f, "%.0f");
		ImGui::SliderFloat(T(TKEY("stow_min"), "Random wait before putting out, from (s)"), &settings.StowDelayMin, 0.0f, 120.0f, "%.0f");
		ImGui::SliderFloat(T(TKEY("stow_max"), "Random wait before putting out, to (s)"), &settings.StowDelayMax, 0.0f, 300.0f, "%.0f");
		if (settings.StowDelayMax < settings.StowDelayMin)
			settings.StowDelayMax = settings.StowDelayMin;
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("water"), "Putting torches out"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox(T(TKEY("douse"), "Walk to nearby water to douse it"), &settings.DouseInWater);
		ImGui::SliderFloat(T(TKEY("water_radius"), "How far they look for water"), &settings.WaterSearchRadius, 300.0f, 5000.0f, "%.0f");
		ImGui::TextDisabled("%s", T(TKEY("water_sources"), "Rivers, lakes, ponds, creeks, puddles in rain, troughs, wells and barrels."));
		ImGui::TreePop();
	}

	ImGui::SliderFloat(T(TKEY("group"), "One torch per group, radius"), &settings.GroupRadius, 0.0f, 1500.0f, "%.0f");
	ImGui::Checkbox(T(TKEY("sneak"), "Put torches away when sneaking"), &settings.StowWhenSneaking);
	ImGui::Checkbox(T(TKEY("followers"), "Include followers"), &settings.IncludeFollowers);
	ImGui::Checkbox(T(TKEY("guards"), "Include guards"), &settings.IncludeGuards);

	if (ImGui::TreeNode(T(TKEY("animations"), "Animations"))) {
		if (animationsInstalled)
			ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.5f, 1.0f), "%s", T(TKEY("anim_found"), "Torch animations installed."));
		else
			ImGui::TextDisabled("%s", T(TKEY("anim_missing"), "Open Animation Replacer or the torch animations are missing. NPCs will not use torches."));
		ImGui::TextDisabled("%s", T(TKEY("anim_list"), "Light (draw, strike flint), snuff on ground, douse at shore or puddle, douse in trough, barrel or well."));
		ImGui::Checkbox(T(TKEY("force_anim"), "Play the clips even if the pack is not detected"), &settings.ForceAnimations);
		ImGui::TreePop();
	}

	ImGui::Text("Torches currently managed: %d", lastManagedCount);
}

void LTBLTorchBearers::LoadSettings(json& o_json) { settings = o_json; }
void LTBLTorchBearers::SaveSettings(json& o_json) { o_json = settings; }
void LTBLTorchBearers::RestoreDefaultSettings() { settings = {}; }

void LTBLTorchBearers::DataLoaded()
{
	torch = RE::TESForm::LookupByID<RE::TESObjectLIGH>(kTorch01);
	xMarker = RE::TESForm::LookupByID<RE::TESBoundObject>(kXMarker);
	std::error_code ec;
	animationsInstalled = std::filesystem::exists(kAnimationFolder, ec);
	if (!torch)
		logger::warn("[LTBL Torch Bearers] Torch01 not found; feature inactive");
	else
		logger::info("[LTBL Torch Bearers] Ready; animations {}", animationsInstalled ? "installed" : "not installed");
}

void LTBLTorchBearers::GameLoaded()
{
	for (auto& [id, st] : states)
		CleanupState(st);
	states.clear();
	timer = 0.0f;
	activeSequences = 0;
}

void LTBLTorchBearers::EarlyPrepass()
{
	if (!settings.Enabled || !torch)
		return;

	float dt = globals::game::deltaTime ? *globals::game::deltaTime : (1.0f / 60.0f);
	dt = std::clamp(dt, 0.0f, 0.25f);

	auto* tasks = SKSE::GetTaskInterface();
	if (!tasks)
		return;

	// Animated sequences need frame-accurate timing for the flame.
	if (activeSequences > 0 && !tickPending) {
		tickPending = true;
		tasks->AddTask([this, dt]() {
			TickSequences(dt);
			tickPending = false;
		});
	}

	timer += dt;
	if (timer < settings.CheckInterval || taskPending)
		return;

	const float elapsed = timer;
	timer = 0.0f;
	taskPending = true;

	// Actor and equipment changes must happen on the main thread.
	tasks->AddTask([this, elapsed]() {
		Evaluate(elapsed);
		taskPending = false;
	});
}

void LTBLTorchBearers::CountActive()
{
	int n = 0;
	for (auto& [id, st] : states) {
		if (st.phase != Phase::kIdle)
			++n;
	}
	activeSequences = n;
}

void LTBLTorchBearers::StartLighting(RE::Actor* actor, ActorState& st)
{
	// No animation, no torch: an NPC never gets one without the full draw-and-light sequence.
	if (!UseAnimations() || !PlayClip(actor, st, SnuffKind::kLight))
		return;

	auto counts = actor->GetInventoryCounts([&](RE::TESBoundObject& obj) { return &obj == torch; });
	if (counts.empty())
		actor->AddObjectToContainer(torch, nullptr, 1, nullptr);
	st.phase = Phase::kLighting;
	st.phaseTime = 0.0f;
	st.step1 = st.step2 = false;
	st.sparksDone = 0;
	st.kind = SnuffKind::kLight;
	st.managed = true;
}

void LTBLTorchBearers::StartPuttingOut(RE::Actor* actor, ActorState& st)
{
	if (!UseAnimations()) {
		RE::ActorEquipManager::GetSingleton()->UnequipObject(actor, torch, nullptr, 1, nullptr, false, false, true, true);
		Refresh3D(actor);
		st.managed = false;
		return;
	}

	if (settings.DouseInWater) {
		RE::NiPoint3 point;
		RE::TESObjectREFR* waterRef = nullptr;
		if (FindWater(actor, point, waterRef)) {
			// Troughs, barrels, wells and fountains are waist high; puddle meshes and shores are not.
			SnuffKind kind = SnuffKind::kWater;
			if (waterRef) {
				auto* model = waterRef->GetBaseObject() ? waterRef->GetBaseObject()->As<RE::TESModel>() : nullptr;
				const char* path = model ? model->GetModel() : nullptr;
				if (!path || ToLower(path).find("puddle") == std::string::npos)
					kind = SnuffKind::kBasin;
			}
			st.walkTarget = point;
			// Already at the water: douse it right here.
			if (actor->GetPosition().GetDistance(point) <= kArriveDistance) {
				BeginSnuffAnimation(actor, st, kind);
				return;
			}
			RE::TESObjectREFR* target = waterRef;
			if (!target && xMarker) {
				if (auto marker = actor->PlaceObjectAtMe(xMarker, false)) {
					marker->SetPosition(point);
					st.marker = marker->GetHandle();
					target = marker.get();
				}
			}
			if (target && PathToReference(actor, target)) {
				st.phase = Phase::kWalkingToWater;
				st.phaseTime = 0.0f;
				st.kind = kind;
				return;
			}
			DeleteMarker(st.marker);
		}
	}
	BeginSnuffAnimation(actor, st, SnuffKind::kGround);
}

void LTBLTorchBearers::BeginSnuffAnimation(RE::Actor* actor, ActorState& st, SnuffKind kind)
{
	DeleteMarker(st.marker);
	if (kind != SnuffKind::kGround)
		FaceTowards(actor, st.walkTarget);

	auto* equipManager = RE::ActorEquipManager::GetSingleton();
	if (!PlayClip(actor, st, kind)) {
		equipManager->UnequipObject(actor, torch, nullptr, 1, nullptr, false, false, true, true);
		Refresh3D(actor);
		st.managed = false;
		st.phase = Phase::kIdle;
		st.kind = SnuffKind::kNone;
		return;
	}
	// The clip starts in the vanilla carry pose: swap the real torch for the lit prop on this frame.
	equipManager->UnequipObject(actor, torch, nullptr, 1, nullptr, false, false, true, true);
	Refresh3D(actor);
	SetProp(actor, st, true, true);
	st.managed = false;
	st.phase = Phase::kSnuffing;
	st.phaseTime = 0.0f;
	st.step1 = st.step2 = false;
	st.kind = kind;
}

bool LTBLTorchBearers::PlayClip(RE::Actor* actor, ActorState& st, SnuffKind kind)
{
	auto* avo = actor->AsActorValueOwner();
	if (!avo)
		return false;
	if (!st.markerSet) {
		st.savedMarker = avo->GetActorValue(RE::ActorValue::kVariable10);
		st.markerSet = true;
	}
	avo->SetActorValue(RE::ActorValue::kVariable10, kMarkerBase + static_cast<float>(kind));
	if (actor->NotifyAnimationGraph(kHostEvent))
		return true;
	RestoreMarker(actor, st);
	return false;
}

void LTBLTorchBearers::RestoreMarker(RE::Actor* actor, ActorState& st)
{
	if (!st.markerSet)
		return;
	if (actor) {
		if (auto* avo = actor->AsActorValueOwner())
			avo->SetActorValue(RE::ActorValue::kVariable10, st.savedMarker);
	}
	st.markerSet = false;
}

void LTBLTorchBearers::SetProp(RE::Actor* actor, ActorState& st, bool on, bool lit)
{
	if (!on) {
		RemoveProp(st);
		return;
	}
	auto* shield = ShieldNode(actor);
	if (!st.prop)
		st.prop = LoadTorchModel(torch);
	if (!st.prop || !shield)
		return;
	const bool reattached = st.prop->parent != shield;
	if (reattached) {
		if (st.prop->parent)
			st.prop->parent->DetachChild(st.prop.get());
		shield->AttachChild(st.prop.get(), true);
		RE::NiUpdateData ud{};
		st.prop->UpdateWorldData(&ud);
	}
	const bool wasLit = st.propLit;
	st.propOn = true;

	// Flame: the torch mesh's own fire node (particles, glow, heat haze), shown or hidden.
	if (auto* fire = st.prop->GetObjectByName("TorchFire"))
		fire->SetAppCulled(!lit);

	// Light: made by the torch form itself, exactly as the engine does for a carried torch
	// (same color, radius, fade and flicker flags, including any changes other mods make to the torch).
	auto* ssn = globals::game::smState ? globals::game::smState->shadowSceneNode[0] : nullptr;
	if (lit && (!st.propLight || reattached)) {
		if (st.propLight && ssn)
			ssn->RemoveLight(st.propLight.get());
		auto* attachObj = st.prop->GetObjectByName("AttachLight");
		auto* attach = attachObj ? attachObj->AsNode() : st.prop.get();
		st.propLight.reset(torch->GenDynamic(actor, attach, 1, 1, 0));
	} else if (!lit && st.propLight) {
		if (ssn)
			ssn->RemoveLight(st.propLight.get());
		if (st.propLight->parent)
			st.propLight->parent->DetachChild(st.propLight.get());
		st.propLight.reset();
	}

	// Burning sound: the torch form's own loop.
	if (lit && !st.burnSound.IsPlaying() && torch->sound) {
		if (auto* audio = RE::BSAudioManager::GetSingleton(); audio && audio->GetSoundHandle(st.burnSound, torch->sound)) {
			st.burnSound.SetObjectToFollow(st.prop.get());
			st.burnSound.Play();
		}
	} else if (!lit && st.burnSound.IsValid()) {
		st.burnSound.FadeOutAndRelease(250);
	}
	if (reattached && lit && st.burnSound.IsValid())
		st.burnSound.SetObjectToFollow(st.prop.get());

	if (wasLit && !lit) {
		RE::NiPoint3 head = st.prop->world * kTorchHeadLocal;
		PlaySoundAt(kFireOutSound, head, 1.0f, 1.0f);
	}
	st.propLit = lit;
}

void LTBLTorchBearers::SetFlint(RE::Actor* actor, ActorState& st, bool on)
{
	if (!on) {
		if (st.flint && st.flint->parent)
			st.flint->parent->DetachChild(st.flint.get());
		st.flintOn = false;
		return;
	}
	auto* hand = RightHandNode(actor);
	if (!st.flint)
		st.flint = LoadModel(kFlintModel);
	if (!st.flint || !hand)
		return;
	if (st.flint->parent != hand) {
		if (st.flint->parent)
			st.flint->parent->DetachChild(st.flint.get());
		hand->AttachChild(st.flint.get(), true);
		RE::NiUpdateData ud{};
		st.flint->UpdateWorldData(&ud);
	}
	st.flintOn = true;
}

void LTBLTorchBearers::StrikeSpark(RE::Actor* actor, ActorState& st)
{
	if (!st.prop || !actor)
		return;
	const RE::NiPoint3 head = st.prop->world * kTorchHeadLocal;
	if (auto* cell = actor->GetParentCell())
		RE::BSTempEffectParticle::Spawn(cell, 0.6f, kSparkModel, RE::NiPoint3{ 0.0f, 0.0f, 0.0f }, head, 0.35f, 7, nullptr);
	PlaySoundAt(kStrikeSound, head, 0.35f, 1.6f);
}

void LTBLTorchBearers::RemoveProp(ActorState& st)
{
	if (st.burnSound.IsValid())
		st.burnSound.FadeOutAndRelease(150);
	if (st.propLight) {
		auto* ssn = globals::game::smState ? globals::game::smState->shadowSceneNode[0] : nullptr;
		if (ssn)
			ssn->RemoveLight(st.propLight.get());
		if (st.propLight->parent)
			st.propLight->parent->DetachChild(st.propLight.get());
		st.propLight.reset();
	}
	if (st.prop && st.prop->parent)
		st.prop->parent->DetachChild(st.prop.get());
	if (st.flint && st.flint->parent)
		st.flint->parent->DetachChild(st.flint.get());
	st.propOn = false;
	st.propLit = false;
	st.flintOn = false;
}

void LTBLTorchBearers::CleanupState(ActorState& st)
{
	auto actor = st.handle.get();
	RemoveProp(st);
	st.prop.reset();
	st.flint.reset();
	RestoreMarker(actor.get(), st);
	DeleteMarker(st.marker);
}

void LTBLTorchBearers::FinishSequence(RE::Actor* actor, ActorState& st)
{
	RemoveProp(st);
	RestoreMarker(actor, st);
	DeleteMarker(st.marker);
	st.phase = Phase::kIdle;
	st.phaseTime = 0.0f;
	st.kind = SnuffKind::kNone;
}

void LTBLTorchBearers::CancelSequence(RE::Actor* actor, ActorState& st)
{
	if (st.phase == Phase::kIdle)
		return;
	const bool playing = st.phase == Phase::kLighting || st.phase == Phase::kSnuffing;
	RemoveProp(st);
	RestoreMarker(actor, st);
	if (actor && playing)
		actor->NotifyAnimationGraph(kStopEvent);
	if (st.phase == Phase::kLighting)
		st.managed = false;  // the torch never reached the hand
	DeleteMarker(st.marker);
	st.phase = Phase::kIdle;
	st.phaseTime = 0.0f;
	st.kind = SnuffKind::kNone;
}

bool LTBLTorchBearers::FindWater(RE::Actor* actor, RE::NiPoint3& outPoint, RE::TESObjectREFR*& outRef) const
{
	outRef = nullptr;
	const auto origin = actor->GetPosition();
	const float radius = settings.WaterSearchRadius;
	float best = radius;
	bool found = false;

	auto* tes = RE::TES::GetSingleton();
	if (!tes)
		return false;

	// Objects that hold water: troughs, wells, barrels, fountains, puddle meshes.
	tes->ForEachReferenceInRange(actor, radius, [&](RE::TESObjectREFR* ref) {
		if (ref && ref != actor && !ref->IsDisabled() && IsWaterObject(ref)) {
			const float d = origin.GetDistance(ref->GetPosition());
			if (d < best) {
				best = d;
				outPoint = ref->GetPosition();
				outRef = ref;
				found = true;
			}
		}
		return RE::BSContainer::ForEachResult::kContinue;
	});

	auto* cell = actor->GetParentCell();
	if (cell && !cell->IsInteriorCell()) {
		// Natural water: rivers, lakes, ponds, creeks. Sample rings around the actor and find the
		// nearest spot where the water surface is above the ground, then stand on the shore.
		constexpr int kDirections = 16;
		constexpr float kStep = 150.0f;
		for (float dist = kStep; dist < best; dist += kStep) {
			bool ringHit = false;
			for (int i = 0; i < kDirections; ++i) {
				const float angle = (2.0f * std::numbers::pi_v<float> * i) / kDirections;
				const RE::NiPoint3 dir{ std::cos(angle), std::sin(angle), 0.0f };
				RE::NiPoint3 p = origin + dir * dist;
				float land = 0.0f;
				if (!tes->GetLandHeight(p, land))
					continue;
				auto* pCell = tes->GetCell(p);
				if (!pCell)
					continue;
				const float water = tes->GetWaterHeight(p, pCell);
				if (water > -1.0e6f && water > land + 8.0f) {
					// Step back toward the actor to stand at the water's edge.
					RE::NiPoint3 shore = origin + dir * std::max(dist - 100.0f, 0.0f);
					float shoreLand = shore.z;
					if (tes->GetLandHeight(shore, shoreLand))
						shore.z = shoreLand;
					best = dist;
					outPoint = shore;
					outRef = nullptr;
					found = true;
					ringHit = true;
					break;
				}
			}
			if (ringHit)
				break;
		}

		// Rain makes puddles everywhere outdoors: douse it right where they stand.
		if (!found) {
			auto* sky = RE::Sky::GetSingleton();
			if (sky && sky->IsRaining()) {
				outPoint = origin;
				found = true;
			}
		}
	}
	return found;
}

void LTBLTorchBearers::TickSequences(float dt)
{
	auto* equipManager = RE::ActorEquipManager::GetSingleton();
	if (!equipManager || !torch)
		return;

	for (auto& [id, st] : states) {
		if (st.phase == Phase::kIdle)
			continue;
		auto actor = st.handle.get();
		if (!actor || actor->IsDead() || !actor->Is3DLoaded()) {
			CancelSequence(actor.get(), st);
			continue;
		}
		st.phaseTime += dt;

		switch (st.phase) {
		case Phase::kLighting:
			{
				const auto& tm = kLightClip;
				if (!st.step1 && st.phaseTime >= tm.a) {
					SetProp(actor.get(), st, true, false);  // hand closes on the handle behind the shoulder
					st.step1 = true;
				}
				if (!st.flintOn && st.phaseTime >= kFlintOutAt && st.phaseTime < kFlintBackAt)
					SetFlint(actor.get(), st, true);  // flint out of the belt pouch
				if (st.flintOn) {
					if (st.phaseTime >= kFlintBackAt)
						SetFlint(actor.get(), st, false);
					else
						SetFlint(actor.get(), st, true);  // re-attach if the 3D was rebuilt
				}
				while (st.sparksDone < static_cast<int>(kStrikeAt.size()) && st.phaseTime >= kStrikeAt[st.sparksDone]) {
					StrikeSpark(actor.get(), st);
					++st.sparksDone;
				}
				if (!st.step2 && st.phaseTime >= tm.b) {
					SetProp(actor.get(), st, true, true);  // the torch catches
					if (st.prop)
						PlaySoundAt(kIgniteSound, st.prop->world * kTorchHeadLocal, 0.7f, 1.0f);
					st.step2 = true;
				}
				if (st.propOn)
					SetProp(actor.get(), st, true, st.propLit);  // re-attach if the 3D was rebuilt
				if (st.phaseTime >= tm.dur) {
					// The clip ends exactly in the vanilla carry pose: hand over to the real torch.
					RemoveProp(st);
					equipManager->EquipObject(actor.get(), torch, nullptr, 1, nullptr, false, false, true, true);
					Refresh3D(actor.get());
					FinishSequence(actor.get(), st);
				}
			}
			break;
		case Phase::kWalkingToWater:
			{
				const bool arrived = actor->GetPosition().GetDistance(st.walkTarget) <= kArriveDistance;
				if (arrived || st.phaseTime >= settings.WalkTimeout)
					BeginSnuffAnimation(actor.get(), st, arrived ? st.kind : SnuffKind::kGround);
			}
			break;
		case Phase::kSnuffing:
			{
				const auto& tm = TimingFor(static_cast<std::int32_t>(st.kind));
				if (!st.step1 && st.phaseTime >= tm.a) {
					SetProp(actor.get(), st, true, false);  // flame dies
					st.step1 = true;
				}
				if (!st.step2 && st.phaseTime >= tm.b) {
					RemoveProp(st);  // stowed over the shoulder
					st.step2 = true;
				}
				if (st.propOn)
					SetProp(actor.get(), st, true, st.propLit);
				if (st.phaseTime >= tm.dur)
					FinishSequence(actor.get(), st);
			}
			break;
		default:
			break;
		}
	}
	CountActive();
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

	// Positions of everyone holding, or about to hold, a light, for the one-per-group rule.
	std::vector<std::pair<RE::Actor*, RE::NiPoint3>> lightHolders;
	for (auto& a : actors) {
		auto it = states.find(a->GetFormID());
		const bool lighting = it != states.end() && it->second.phase == Phase::kLighting;
		if (HoldsLight(a.get()) || lighting)
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
		st.handle = actor->GetHandle();

		// A sequence in progress is driven by TickSequences.
		if (st.phase != Phase::kIdle) {
			if (st.managed)
				++managedCount;
			continue;
		}

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
			// Situations that need the hands now: put it away immediately, no animation.
			st.darkTime = st.brightTime = 0.0f;
			st.drawDelay = st.stowDelay = -1.0f;
			if (st.managed && holding) {
				equipManager->UnequipObject(actor, torch, nullptr, 1, nullptr, false, false, true, true);
				Refresh3D(actor);
				st.managed = false;
			}
			continue;
		}

		const float light = GetLightAt(actor);
		if (light < settings.DrawBelowLight) {
			if (st.drawDelay < 0.0f)
				st.drawDelay = RandomFloat(0.0f, settings.DrawDelayMax);
			st.darkTime += elapsed;
			st.brightTime = 0.0f;
			st.stowDelay = -1.0f;
		} else if (light > settings.StowAboveLight) {
			if (st.stowDelay < 0.0f)
				st.stowDelay = RandomFloat(settings.StowDelayMin, settings.StowDelayMax);
			st.brightTime += elapsed;
			st.darkTime = 0.0f;
			st.drawDelay = -1.0f;
		} else {
			// Between thresholds: hold current state.
			st.darkTime = st.brightTime = 0.0f;
			st.drawDelay = st.stowDelay = -1.0f;
		}

		if (!holding && st.darkTime >= settings.DwellSeconds + std::max(st.drawDelay, 0.0f)) {
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
				StartLighting(actor, st);
				st.darkTime = 0.0f;
				st.drawDelay = -1.0f;
				lightHolders.emplace_back(actor, pos);
			}
		} else if (holding && st.managed && st.brightTime >= settings.DwellSeconds + std::max(st.stowDelay, 0.0f)) {
			StartPuttingOut(actor, st);
			st.brightTime = 0.0f;
			st.stowDelay = -1.0f;
		}

		if (st.managed)
			++managedCount;
	}

	// Forget actors that left the high process.
	for (auto it = states.begin(); it != states.end();) {
		if (!seen.contains(it->first)) {
			CleanupState(it->second);
			it = states.erase(it);
		} else {
			++it;
		}
	}
	lastManagedCount = managedCount;
	CountActive();
}
