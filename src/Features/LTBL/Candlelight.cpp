#include "Candlelight.h"

#include "I18n/I18n.h"
#include "State.h"

#include <array>
#include <random>
#include <unordered_set>

#define I18N_KEY_PREFIX "feature.ltbl_candlelight."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LTBLCandlelight::Settings,
	Enabled,
	Radius,
	Intensity,
	StartRadius,
	StartIntensity,
	Falloff,
	TomeLoot,
	TomesForFullBrightness,
	VendorsStockTome)

namespace
{
	constexpr std::uint32_t kSerializationID = 0x4C54424C;  // 'LTBL'
	constexpr std::uint32_t kRecordCandlelight = 0x434E444C;  // 'CNDL'
	constexpr std::uint32_t kRecordVersion = 1;
	constexpr RE::FormID kPlayerRef = 0x14;

	// Locations where tomes can turn up as extra loot.
	constexpr std::array kDungeonKeywords{
		"LocTypeDungeon"sv,
		"LocTypeCave"sv,
		"LocTypeDraugrCrypt"sv,
		"LocTypeNordicRuin"sv,
		"LocTypeDwarvenAutomatons"sv,
		"LocTypeFalmerHive"sv,
		"LocTypeAnimalDen"sv,
		"LocTypeMine"sv,
		"LocTypeVampireLair"sv,
		"LocTypeWarlockLair"sv,
		"LocTypeDragonPriestLair"sv,
		"LocTypeBanditCamp"sv,
		"LocTypeForswornCamp"sv,
		"LocTypeHagravenNest"sv,
		"LocTypeSprigganGrove"sv,
	};

	bool IsDungeonCell(const RE::TESObjectCELL* cell)
	{
		if (!cell || !cell->IsInteriorCell())
			return false;
		int depth = 0;
		for (auto* loc = cell->GetLocation(); loc && depth < 8; loc = loc->parentLoc, ++depth) {
			for (auto kw : kDungeonKeywords) {
				if (loc->HasKeywordString(kw))
					return true;
			}
		}
		return false;
	}

	std::mt19937& Rng()
	{
		static std::mt19937 rng{ std::random_device{}() };
		return rng;
	}

	int RandomInt(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(Rng()); }

	inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
}

void LTBLCandlelight::DrawSettings()
{
	bool changed = false;
	changed |= ImGui::Checkbox(T(TKEY("enabled"), "Candlelight lights the area around you"), &settings.Enabled);

	if (ImGui::TreeNodeEx(T(TKEY("brightness"), "Brightness"), ImGuiTreeNodeFlags_DefaultOpen)) {
		int start = static_cast<int>(settings.StartRadius);
		int full = static_cast<int>(settings.Radius);
		if (ImGui::SliderInt(T(TKEY("start_radius"), "Lit area at first tome"), &start, 128, 4096)) {
			settings.StartRadius = static_cast<std::uint32_t>(start);
			changed = true;
		}
		if (ImGui::SliderInt(T(TKEY("radius"), "Lit area at full brightness"), &full, 256, 4096)) {
			settings.Radius = static_cast<std::uint32_t>(full);
			changed = true;
		}
		changed |= ImGui::SliderFloat(T(TKEY("start_intensity"), "Brightness at first tome"), &settings.StartIntensity, 0.05f, 2.0f, "%.2f");
		changed |= ImGui::SliderFloat(T(TKEY("intensity"), "Brightness at full"), &settings.Intensity, 0.1f, 2.0f, "%.2f");
		changed |= ImGui::SliderFloat(T(TKEY("falloff"), "Edge softness"), &settings.Falloff, 0.1f, 2.0f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("falloff_tt"), "Lower values spread light further before it fades."));
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("tomes"), "Tomes"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox(T(TKEY("tome_loot"), "Tomes appear as extra dungeon loot"), &settings.TomeLoot);
		int need = static_cast<int>(settings.TomesForFullBrightness);
		if (ImGui::SliderInt(T(TKEY("tomes_needed"), "Tomes for full brightness"), &need, 1, 20)) {
			settings.TomesForFullBrightness = static_cast<std::uint32_t>(need);
			changed = true;
		}
		ImGui::Checkbox(T(TKEY("vendors"), "Vendors stock the tome"), &settings.VendorsStockTome);
		ImGui::Text("Tomes found: %u / %u   (level %u)", tomesFound, settings.TomesForFullBrightness, CurrentLevel());
		ImGui::TreePop();
	}

	ImGui::TextDisabled("%s", T(TKEY("recast"), "Recast the spell to see brightness changes."));
	if (changed)
		Apply();
}

void LTBLCandlelight::LoadSettings(json& o_json) { settings = o_json; }
void LTBLCandlelight::SaveSettings(json& o_json) { o_json = settings; }
void LTBLCandlelight::RestoreDefaultSettings()
{
	settings = {};
	Apply();
}

void LTBLCandlelight::Load()
{
	if (auto* serialization = SKSE::GetSerializationInterface()) {
		serialization->SetUniqueID(kSerializationID);
		serialization->SetSaveCallback(OnSave);
		serialization->SetLoadCallback(OnLoad);
		serialization->SetRevertCallback(OnRevert);
	}
}

void LTBLCandlelight::DataLoaded()
{
	auto* dataHandler = RE::TESDataHandler::GetSingleton();
	if (!dataHandler)
		return;

	// Every self-cast light effect gets the aura treatment.
	std::unordered_set<RE::TESObjectLIGH*> found;
	std::unordered_set<RE::SpellItem*> lightSpells;
	for (auto* s : dataHandler->GetFormArray<RE::SpellItem>()) {
		if (!s || s->GetDelivery() != RE::MagicSystem::Delivery::kSelf)
			continue;
		for (auto* effect : s->effects) {
			auto* base = effect ? effect->baseEffect : nullptr;
			if (!base || !base->HasArchetype(RE::EffectSetting::Archetype::kLight))
				continue;
			lightSpells.insert(s);
			auto* light = base->data.associatedForm ? base->data.associatedForm->As<RE::TESObjectLIGH>() : nullptr;
			if (light && found.insert(light).second)
				originals.push_back({ light, light->data.radius, light->fade, light->data.fallofExponent });
		}
	}

	// Candlelight is the self-cast light spell that has a tome.
	for (auto* book : dataHandler->GetFormArray<RE::TESObjectBOOK>()) {
		if (!book || !book->TeachesSpell())
			continue;
		auto* taught = book->GetSpell();
		if (taught && lightSpells.contains(taught)) {
			spell = taught;
			tome = book;
			break;
		}
	}

	if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
		events->AddEventSink<RE::TESContainerChangedEvent>(this);
		events->AddEventSink<RE::TESCellFullyLoadedEvent>(this);
	}
	if (auto* ui = RE::UI::GetSingleton())
		ui->AddEventSink<RE::MenuOpenCloseEvent>(this);

	logger::info("[LTBL Candlelight] {} light effect(s); tome {}", originals.size(), tome ? "found" : "NOT found");
	Apply();
}

void LTBLCandlelight::GameLoaded()
{
	Apply();
}

std::uint32_t LTBLCandlelight::CurrentLevel() const
{
	const auto maxLevel = std::max<std::uint32_t>(settings.TomesForFullBrightness, 1);
	return std::clamp<std::uint32_t>(std::max<std::uint32_t>(tomesFound, 1), 1, maxLevel);
}

void LTBLCandlelight::Apply()
{
	const auto maxLevel = std::max<std::uint32_t>(settings.TomesForFullBrightness, 1);
	const float t = maxLevel > 1 ? static_cast<float>(CurrentLevel() - 1) / static_cast<float>(maxLevel - 1) : 1.0f;
	const auto radius = static_cast<std::uint32_t>(Lerp(static_cast<float>(settings.StartRadius), static_cast<float>(settings.Radius), t));
	const float intensity = Lerp(settings.StartIntensity, settings.Intensity, t);

	for (auto& o : originals) {
		if (settings.Enabled) {
			o.light->data.radius = radius;
			o.light->fade = intensity;
			o.light->data.fallofExponent = settings.Falloff;
		} else {
			o.light->data.radius = o.radius;
			o.light->fade = o.fade;
			o.light->data.fallofExponent = o.falloff;
		}
	}
}

// A tome entering the player's inventory from dungeon loot counts toward brightness.
RE::BSEventNotifyControl LTBLCandlelight::ProcessEvent(const RE::TESContainerChangedEvent* a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>*)
{
	if (!a_event || !tome || a_event->baseObj != tome->GetFormID() || a_event->itemCount <= 0)
		return RE::BSEventNotifyControl::kContinue;

	// Any tome leaving a container we seeded means that container no longer needs cleanup.
	if (a_event->oldContainer != 0)
		seededContainers.erase(a_event->oldContainer);

	if (a_event->newContainer != kPlayerRef || a_event->oldContainer == kPlayerRef || Complete())
		return RE::BSEventNotifyControl::kContinue;

	bool fromDungeon = false;
	if (a_event->oldContainer == 0) {
		// Picked up from the world.
		auto* player = RE::PlayerCharacter::GetSingleton();
		fromDungeon = player && IsDungeonCell(player->GetParentCell());
	} else if (auto* from = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_event->oldContainer)) {
		auto* base = from->GetBaseObject();
		fromDungeon = base && base->Is(RE::FormType::Container) && IsDungeonCell(from->GetParentCell());
	}
	if (!fromDungeon)
		return RE::BSEventNotifyControl::kContinue;

	tomesFound = std::min(tomesFound + static_cast<std::uint32_t>(a_event->itemCount), settings.TomesForFullBrightness);
	Apply();

	const auto msg = Complete() ? std::string("Your Candlelight burns at full brightness.") :
	                              std::format("Your Candlelight grows brighter ({}/{}).", tomesFound, settings.TomesForFullBrightness);
	RE::SendHUDMessage::ShowHUDMessage(msg.c_str());

	if (Complete()) {
		if (auto* tasks = SKSE::GetTaskInterface())
			tasks->AddTask([this]() { RemoveUnlootedTomes(); });
	}
	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl LTBLCandlelight::ProcessEvent(const RE::TESCellFullyLoadedEvent* a_event, RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*)
{
	if (!a_event || !a_event->cell || !settings.TomeLoot || !tome || Complete())
		return RE::BSEventNotifyControl::kContinue;
	auto* cell = a_event->cell;
	if (auto* tasks = SKSE::GetTaskInterface())
		tasks->AddTask([this, cell]() { SeedCell(cell); });
	return RE::BSEventNotifyControl::kContinue;
}

// Vendors keep a tome in stock: added to their merchant chest when you start talking to them.
RE::BSEventNotifyControl LTBLCandlelight::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (!a_event || !a_event->opening || !settings.VendorsStockTome || !tome)
		return RE::BSEventNotifyControl::kContinue;
	if (a_event->menuName != RE::DialogueMenu::MENU_NAME)
		return RE::BSEventNotifyControl::kContinue;

	if (auto* tasks = SKSE::GetTaskInterface()) {
		tasks->AddTask([this]() {
			auto* topics = RE::MenuTopicManager::GetSingleton();
			if (!topics)
				return;
			auto speaker = topics->speaker.get();
			if (speaker) {
				if (auto* actor = speaker->As<RE::Actor>())
					StockVendor(actor);
			}
		});
	}
	return RE::BSEventNotifyControl::kContinue;
}

void LTBLCandlelight::StockVendor(RE::Actor* vendor)
{
	auto* faction = vendor ? vendor->GetVendorFaction() : nullptr;
	auto* chest = faction ? faction->vendorData.merchantContainer : nullptr;
	if (!chest)
		return;
	auto counts = chest->GetInventoryCounts([&](RE::TESBoundObject& obj) { return &obj == tome; });
	if (counts.empty())
		chest->AddObjectToContainer(tome, nullptr, 1, nullptr);
}

void LTBLCandlelight::SeedCell(RE::TESObjectCELL* cell)
{
	if (!cell || Complete() || !IsDungeonCell(cell) || processedCells.contains(cell->GetFormID()))
		return;
	processedCells.insert(cell->GetFormID());

	std::vector<RE::TESObjectREFR*> containers;
	cell->ForEachReference([&](RE::TESObjectREFR* ref) {
		if (ref && !ref->IsDisabled() && !ref->IsDeleted()) {
			auto* base = ref->GetBaseObject();
			if (base && base->Is(RE::FormType::Container))
				containers.push_back(ref);
		}
		return RE::BSContainer::ForEachResult::kContinue;
	});
	if (containers.empty())
		return;

	// Dungeon size by how many loot spots it has: small 1-2, normal 2-4, huge 5+.
	int count;
	const auto n = containers.size();
	if (n <= 5)
		count = RandomInt(1, 2);
	else if (n < 20)
		count = RandomInt(2, 4);
	else
		count = RandomInt(5, 7);
	count = std::min<int>(count, static_cast<int>(n));

	std::shuffle(containers.begin(), containers.end(), Rng());
	for (int i = 0; i < count; ++i) {
		containers[i]->AddObjectToContainer(tome, nullptr, 1, nullptr);
		seededContainers.insert(containers[i]->GetFormID());
	}
	logger::info("[LTBL Candlelight] Added {} tome(s) to cell {:08X}", count, cell->GetFormID());
}

void LTBLCandlelight::RemoveUnlootedTomes()
{
	for (auto id : seededContainers) {
		auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
		if (!ref)
			continue;
		auto counts = ref->GetInventoryCounts([&](RE::TESBoundObject& obj) { return &obj == tome; });
		if (!counts.empty())
			ref->RemoveItem(tome, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
	}
	seededContainers.clear();
}

void LTBLCandlelight::OnSave(SKSE::SerializationInterface* a_intfc)
{
	auto& self = globals::features::ltblCandlelight;
	if (!a_intfc->OpenRecord(kRecordCandlelight, kRecordVersion))
		return;
	a_intfc->WriteRecordData(self.tomesFound);
	auto writeSet = [&](const std::unordered_set<RE::FormID>& set) {
		const auto size = static_cast<std::uint32_t>(set.size());
		a_intfc->WriteRecordData(size);
		for (auto id : set)
			a_intfc->WriteRecordData(id);
	};
	writeSet(self.processedCells);
	writeSet(self.seededContainers);
}

void LTBLCandlelight::OnLoad(SKSE::SerializationInterface* a_intfc)
{
	auto& self = globals::features::ltblCandlelight;
	std::uint32_t type, version, length;
	while (a_intfc->GetNextRecordInfo(type, version, length)) {
		if (type != kRecordCandlelight || version != kRecordVersion)
			continue;
		a_intfc->ReadRecordData(self.tomesFound);
		auto readSet = [&](std::unordered_set<RE::FormID>& set) {
			set.clear();
			std::uint32_t size = 0;
			a_intfc->ReadRecordData(size);
			for (std::uint32_t i = 0; i < size; ++i) {
				RE::FormID id = 0, resolved = 0;
				a_intfc->ReadRecordData(id);
				if (a_intfc->ResolveFormID(id, resolved))
					set.insert(resolved);
			}
		};
		readSet(self.processedCells);
		readSet(self.seededContainers);
	}
}

void LTBLCandlelight::OnRevert(SKSE::SerializationInterface*)
{
	auto& self = globals::features::ltblCandlelight;
	self.tomesFound = 0;
	self.processedCells.clear();
	self.seededContainers.clear();
}
