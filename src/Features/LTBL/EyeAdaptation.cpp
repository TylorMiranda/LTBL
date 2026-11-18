#include "EyeAdaptation.h"

#include "I18n/I18n.h"
#include "State.h"

#define I18N_KEY_PREFIX "feature.ltbl_eye_adaptation."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LTBLEyeAdaptation::Settings,
	Enabled,
	BaselineLight,
	GlareMinSeconds,
	GlareMaxSeconds,
	GlareFullStops,
	DimStops,
	VeryDarkStops,
	PitchBlackStops,
	DimSeconds,
	VeryDarkSeconds,
	PitchBlackSeconds,
	Strength,
	MaxDarkBoostStops,
	MaxBrightCutStops,
	NightDesaturation,
	DisableVanillaAdaptation)

namespace
{
	// Floor so log2 stays finite in total darkness.
	constexpr float kLightEpsilon = 0.5f;
	// An exponential approach is ~99% complete after 4.6 time constants.
	constexpr float kTimeConstantsToComplete = 4.6f;

	inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
	inline float Saturate(float v) { return std::clamp(v, 0.0f, 1.0f); }
}

void LTBLEyeAdaptation::DrawSettings()
{
	ImGui::Checkbox(T(TKEY("enabled"), "Eyes adjust to light and dark"), &settings.Enabled);

	if (ImGui::TreeNodeEx(T(TKEY("timing"), "Timing"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat(T(TKEY("glare_min"), "Glare, slightly bright (s)"), &settings.GlareMinSeconds, 0.25f, 5.0f, "%.1f");
		ImGui::SliderFloat(T(TKEY("glare_max"), "Glare, blinding (s)"), &settings.GlareMaxSeconds, 1.0f, 20.0f, "%.1f");
		ImGui::SliderFloat(T(TKEY("dim"), "Very dim (s)"), &settings.DimSeconds, 5.0f, 60.0f, "%.0f");
		ImGui::SliderFloat(T(TKEY("very_dark"), "Very dark (s)"), &settings.VeryDarkSeconds, 10.0f, 120.0f, "%.0f");
		ImGui::SliderFloat(T(TKEY("pitch_black"), "Pitch black (s)"), &settings.PitchBlackSeconds, 20.0f, 180.0f, "%.0f");
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx(T(TKEY("range"), "How far your eyes can adjust"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat(T(TKEY("strength"), "Adjustment strength"), &settings.Strength, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat(T(TKEY("dark_boost"), "Night vision limit (stops)"), &settings.MaxDarkBoostStops, 0.0f, 6.0f, "%.1f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("%s", T(TKEY("dark_boost_tt"), "How much your eyes can open up in darkness. Low values keep pitch black barely visible."));
		ImGui::SliderFloat(T(TKEY("bright_cut"), "Glare limit (stops)"), &settings.MaxBrightCutStops, 0.0f, 6.0f, "%.1f");
		ImGui::SliderFloat(T(TKEY("night_desat"), "Color loss in the dark"), &settings.NightDesaturation, 0.0f, 0.5f, "%.2f");
		ImGui::SliderFloat(T(TKEY("baseline"), "Normal light level"), &settings.BaselineLight, 5.0f, 150.0f, "%.0f");
		ImGui::Checkbox(T(TKEY("disable_vanilla"), "Replace vanilla adaptation"), &settings.DisableVanillaAdaptation);
		ImGui::TreePop();
	}

	if (ImGui::TreeNode(T(TKEY("debug"), "Readout"))) {
		ImGui::Text("Light at character: %.1f", lastLightLevel);
		ImGui::Text("Target: %.2f  Adapted: %.2f (log2)", lastTargetLog, adaptedLog);
		ImGui::Text("Eye compensation: %+.2f stops", lastCompStops);
		ImGui::TreePop();
	}
}

void LTBLEyeAdaptation::LoadSettings(json& o_json) { settings = o_json; }
void LTBLEyeAdaptation::SaveSettings(json& o_json) { o_json = settings; }
void LTBLEyeAdaptation::RestoreDefaultSettings() { settings = {}; }

float LTBLEyeAdaptation::DurationForDarkening(float stopsBelow) const
{
	// Coming down from bright to normal is quick; real darkness takes the long route.
	const float quick = settings.GlareMinSeconds * 4.0f;
	if (stopsBelow <= 0.0f)
		return quick;
	if (stopsBelow <= settings.DimStops)
		return Lerp(quick, settings.DimSeconds, Saturate(stopsBelow / std::max(settings.DimStops, 0.01f)));
	if (stopsBelow <= settings.VeryDarkStops)
		return Lerp(settings.DimSeconds, settings.VeryDarkSeconds,
			Saturate((stopsBelow - settings.DimStops) / std::max(settings.VeryDarkStops - settings.DimStops, 0.01f)));
	if (stopsBelow <= settings.PitchBlackStops)
		return Lerp(settings.VeryDarkSeconds, settings.PitchBlackSeconds,
			Saturate((stopsBelow - settings.VeryDarkStops) / std::max(settings.PitchBlackStops - settings.VeryDarkStops, 0.01f)));
	return settings.PitchBlackSeconds;
}

void LTBLEyeAdaptation::RestoreImagespace()
{
	if (!wroteLastFrame)
		return;
	if (auto* ism = globals::game::imageSpaceManager) {
		auto& bd = ism->GetRuntimeData().data.baseData;
		if (bd.cinematic.brightness == writtenBrightness)
			bd.cinematic.brightness = sourceBrightness;
		if (bd.cinematic.saturation == writtenSaturation)
			bd.cinematic.saturation = sourceSaturation;
		if (bd.hdr.eyeAdaptStrength == writtenAdaptStrength)
			bd.hdr.eyeAdaptStrength = sourceAdaptStrength;
	}
	wroteLastFrame = false;
}

void LTBLEyeAdaptation::EarlyPrepass()
{
	if (!settings.Enabled) {
		RestoreImagespace();
		initialized = false;
		return;
	}

	auto* player = globals::game::player;
	auto* ism = globals::game::imageSpaceManager;
	if (!player || !ism)
		return;

	auto* high = player->GetHighProcess();
	if (!high)
		return;

	const float light = std::max(high->lightLevel, 0.0f);
	const float targetLog = std::log2(light + kLightEpsilon);
	const float baseLog = std::log2(settings.BaselineLight + kLightEpsilon);

	float dt = globals::game::deltaTime ? *globals::game::deltaTime : (1.0f / 60.0f);
	dt = std::clamp(dt, 0.0f, 0.25f);

	if (!initialized) {
		adaptedLog = targetLog;
		initialized = true;
	}

	float duration;
	if (targetLog > adaptedLog) {
		// Getting brighter: glare. Brighter above baseline = longer settle.
		const float stopsAbove = std::max(targetLog - baseLog, 0.0f);
		duration = Lerp(settings.GlareMinSeconds, settings.GlareMaxSeconds, Saturate(stopsAbove / std::max(settings.GlareFullStops, 0.01f)));
	} else {
		duration = DurationForDarkening(baseLog - targetLog);
	}

	// Fast at first, easing off over time; exact for any frame rate.
	const float tau = std::max(duration / kTimeConstantsToComplete, 0.001f);
	adaptedLog += (targetLog - adaptedLog) * (1.0f - std::exp(-dt / tau));

	// Compensation: eyes open up when adapted to darkness, close down when adapted to brightness.
	float compStops = settings.Strength * (baseLog - adaptedLog);
	compStops = std::clamp(compStops, -settings.MaxBrightCutStops, settings.MaxDarkBoostStops);
	const float exposure = std::exp2(compStops);
	const float darkAdapted = settings.MaxDarkBoostStops > 0.0f ? Saturate(compStops / settings.MaxDarkBoostStops) : 0.0f;

	auto& bd = ism->GetRuntimeData().data.baseData;

	// If the engine rebuilt the blended values, take them as the new source; otherwise reuse our cached source.
	if (!wroteLastFrame || bd.cinematic.brightness != writtenBrightness)
		sourceBrightness = bd.cinematic.brightness;
	if (!wroteLastFrame || bd.cinematic.saturation != writtenSaturation)
		sourceSaturation = bd.cinematic.saturation;
	if (!wroteLastFrame || bd.hdr.eyeAdaptStrength != writtenAdaptStrength)
		sourceAdaptStrength = bd.hdr.eyeAdaptStrength;

	bd.cinematic.brightness = sourceBrightness * exposure;
	bd.cinematic.saturation = sourceSaturation * (1.0f - settings.NightDesaturation * darkAdapted);
	bd.hdr.eyeAdaptStrength = settings.DisableVanillaAdaptation ? 0.0f : sourceAdaptStrength;

	writtenBrightness = bd.cinematic.brightness;
	writtenSaturation = bd.cinematic.saturation;
	writtenAdaptStrength = bd.hdr.eyeAdaptStrength;
	wroteLastFrame = true;

	lastLightLevel = light;
	lastTargetLog = targetLog;
	lastCompStops = compStops;
}
