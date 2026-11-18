#include "Candlelight.h"

#include "I18n/I18n.h"
#include "State.h"

#include <unordered_set>

#define I18N_KEY_PREFIX "feature.ltbl_candlelight."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LTBLCandlelight::Settings,
	Enabled,
	Radius,
	Intensity,
	Falloff)

void LTBLCandlelight::DrawSettings()
{
	bool changed = false;
	changed |= ImGui::Checkbox(T(TKEY("enabled"), "Candlelight lights the area around you"), &settings.Enabled);
	int radius = static_cast<int>(settings.Radius);
	if (ImGui::SliderInt(T(TKEY("radius"), "Lit area size"), &radius, 256, 4096)) {
		settings.Radius = static_cast<std::uint32_t>(radius);
		changed = true;
	}
	changed |= ImGui::SliderFloat(T(TKEY("intensity"), "Brightness"), &settings.Intensity, 0.1f, 2.0f, "%.2f");
	changed |= ImGui::SliderFloat(T(TKEY("falloff"), "Edge softness"), &settings.Falloff, 0.1f, 2.0f, "%.2f");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("%s", T(TKEY("falloff_tt"), "Lower values spread light further before it fades."));
	ImGui::Text("Light effects reworked: %zu", originals.size());
	ImGui::TextDisabled("%s", T(TKEY("recast"), "Recast the spell to see changes."));
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

void LTBLCandlelight::DataLoaded()
{
	auto* dataHandler = RE::TESDataHandler::GetSingleton();
	if (!dataHandler)
		return;

	std::unordered_set<RE::TESObjectLIGH*> found;
	for (auto* spell : dataHandler->GetFormArray<RE::SpellItem>()) {
		if (!spell || spell->GetDelivery() != RE::MagicSystem::Delivery::kSelf)
			continue;
		for (auto* effect : spell->effects) {
			auto* base = effect ? effect->baseEffect : nullptr;
			if (!base || !base->HasArchetype(RE::EffectSetting::Archetype::kLight))
				continue;
			auto* light = base->data.associatedForm ? base->data.associatedForm->As<RE::TESObjectLIGH>() : nullptr;
			if (light && found.insert(light).second)
				originals.push_back({ light, light->data.radius, light->fade, light->data.fallofExponent });
		}
	}

	logger::info("[LTBL Candlelight] Found {} self-cast light effect(s)", originals.size());
	Apply();
}

void LTBLCandlelight::Apply()
{
	for (auto& o : originals) {
		if (settings.Enabled) {
			o.light->data.radius = settings.Radius;
			o.light->fade = settings.Intensity;
			o.light->data.fallofExponent = settings.Falloff;
		} else {
			o.light->data.radius = o.radius;
			o.light->fade = o.fade;
			o.light->data.fallofExponent = o.falloff;
		}
	}
}
