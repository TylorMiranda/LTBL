#include "WorldLight.h"

#include "I18n/I18n.h"
#include "State.h"

#define I18N_KEY_PREFIX "feature.ltbl_world_light."

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	LTBLWorldLight::Settings,
	ColorEnabled,
	Neutralize,
	Saturation,
	Contrast,
	WaterEnabled,
	WaterGlare,
	SprayBrightness,
	SmokeEnabled,
	SmokeNightFloor,
	LivingLightEnabled,
	LivingLightScale,
	ScanRadius)

namespace
{
	constexpr float kScanInterval = 2.0f;   // seconds between scene scans (main thread)
	constexpr float kApplyInterval = 0.5f;  // seconds between brightness updates

	std::string Lower(std::string_view s)
	{
		std::string out(s);
		std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
	}

	bool ContainsAny(const std::string& s, std::initializer_list<const char*> words)
	{
		for (auto* w : words)
			if (s.find(w) != std::string::npos)
				return true;
		return false;
	}

	float Luma(const RE::NiColor& c) { return 0.2126f * c.red + 0.7152f * c.green + 0.0722f * c.blue; }

	// Things that glow in the lore and the light they cast.
	struct GlowRule
	{
		const char* pathPart;
		const char* exclude;
		float r, g, b;
		float radius;
		float fade;
	};
	constexpr GlowRule kGlowRules[] = {
		{ "nirnroot01red", "_empty", 1.00f, 0.22f, 0.22f, 110.0f, 0.55f },  // crimson nirnroot
		{ "nirnroot", "_empty", 0.45f, 0.65f, 1.00f, 110.0f, 0.55f },       // nirnroot's blue chime-glow
		{ "glowingmushroom", nullptr, 0.35f, 0.85f, 1.00f, 140.0f, 0.75f },
		{ "blackreachcrystal", nullptr, 0.55f, 0.75f, 1.00f, 220.0f, 0.85f },
		{ "critters\\firefly\\", nullptr, 0.95f, 0.92f, 0.45f, 70.0f, 0.50f },  // torchbugs
	};

	const GlowRule* MatchGlow(const std::string& model)
	{
		for (auto& r : kGlowRules) {
			if (model.find(r.pathPart) == std::string::npos)
				continue;
			if (r.exclude && model.find(r.exclude) != std::string::npos)
				return nullptr;
			return &r;
		}
		return nullptr;
	}
}

void LTBLWorldLight::DrawSettings()
{
	if (ImGui::TreeNodeEx(T(TKEY("color"), "Color"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox(T(TKEY("color_enabled"), "Neutral, bold color"), &settings.ColorEnabled);
		ImGui::SliderFloat(T(TKEY("neutralize"), "Remove warm/cold tint"), &settings.Neutralize, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat(T(TKEY("saturation"), "Color strength"), &settings.Saturation, 0.5f, 2.0f, "%.2f");
		ImGui::SliderFloat(T(TKEY("contrast"), "Contrast"), &settings.Contrast, 0.5f, 1.5f, "%.2f");
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx(T(TKEY("water"), "Water glare"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox(T(TKEY("water_enabled"), "Calm water glare"), &settings.WaterEnabled);
		ImGui::SliderFloat(T(TKEY("water_glare"), "Sun glints on water"), &settings.WaterGlare, 0.1f, 1.0f, "%.2f");
		ImGui::SliderFloat(T(TKEY("spray"), "Waterfall, foam and mist brightness"), &settings.SprayBrightness, 0.2f, 1.0f, "%.2f");
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx(T(TKEY("smoke"), "Smoke and haze"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox(T(TKEY("smoke_enabled"), "Smoke, steam, dust and fog darken with the light"), &settings.SmokeEnabled);
		ImGui::SliderFloat(T(TKEY("smoke_floor"), "Darkest it gets at night"), &settings.SmokeNightFloor, 0.0f, 0.5f, "%.2f");
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx(T(TKEY("living"), "Living light"), ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox(T(TKEY("living_enabled"), "Glowing plants, crystals and torchbugs cast light"), &settings.LivingLightEnabled);
		ImGui::SliderFloat(T(TKEY("living_scale"), "Brightness"), &settings.LivingLightScale, 0.0f, 3.0f, "%.2f");
		ImGui::Text("%s %zu", T(TKEY("living_count"), "Lights nearby:"), livingLights.size());
		ImGui::TreePop();
	}
}

void LTBLWorldLight::LoadSettings(json& o_json) { settings = o_json; }
void LTBLWorldLight::SaveSettings(json& o_json) { o_json = settings; }
void LTBLWorldLight::RestoreDefaultSettings() { settings = {}; }

void LTBLWorldLight::DataLoaded()
{
	if (auto* dh = RE::TESDataHandler::GetSingleton()) {
		for (auto* water : dh->GetFormArray<RE::TESWaterForm>()) {
			if (!water)
				continue;
			auto& d = water->data;
			waterOriginals[water] = { d.sunSpecularMagnitude, d.sunSparkleMagnitude, d.specularBrightness };
		}
	}
	ApplyWater();
	logger::info("[LTBL World Light] Ready; {} water types", waterOriginals.size());
}

void LTBLWorldLight::GameLoaded()
{
	RestoreMaterials();
	RemoveAllLivingLights();
	timeSinceScan = kScanInterval;  // scan right away
}

// ------------------------------------------------------------------ color

void LTBLWorldLight::ApplyColor()
{
	auto* ism = globals::game::imageSpaceManager;
	if (!ism)
		return;
	auto& bd = ism->GetRuntimeData().data.baseData;
	// The engine rebuilds these from the weather each frame; take fresh values as the source.
	if (!colorWritten || bd.cinematic.saturation != wrSaturation)
		srcSaturation = bd.cinematic.saturation;
	if (!colorWritten || bd.cinematic.contrast != wrContrast)
		srcContrast = bd.cinematic.contrast;
	if (!colorWritten || bd.tint.amount != wrTint)
		srcTint = bd.tint.amount;

	bd.cinematic.saturation = srcSaturation * settings.Saturation;
	bd.cinematic.contrast = srcContrast * settings.Contrast;
	bd.tint.amount = srcTint * (1.0f - std::clamp(settings.Neutralize, 0.0f, 1.0f));

	wrSaturation = bd.cinematic.saturation;
	wrContrast = bd.cinematic.contrast;
	wrTint = bd.tint.amount;
	colorWritten = true;
}

void LTBLWorldLight::RestoreColor()
{
	if (!colorWritten)
		return;
	if (auto* ism = globals::game::imageSpaceManager) {
		auto& bd = ism->GetRuntimeData().data.baseData;
		if (bd.cinematic.saturation == wrSaturation)
			bd.cinematic.saturation = srcSaturation;
		if (bd.cinematic.contrast == wrContrast)
			bd.cinematic.contrast = srcContrast;
		if (bd.tint.amount == wrTint)
			bd.tint.amount = srcTint;
	}
	colorWritten = false;
}

// ------------------------------------------------------------------ water

void LTBLWorldLight::ApplyWater()
{
	const float glare = settings.WaterEnabled ? std::clamp(settings.WaterGlare, 0.0f, 1.0f) : 1.0f;
	if (glare == appliedWaterGlare)
		return;
	for (auto& [water, o] : waterOriginals) {
		water->data.sunSpecularMagnitude = o.sunSpecularMagnitude * glare;
		water->data.sunSparkleMagnitude = o.sunSparkleMagnitude * glare;
		water->data.specularBrightness = o.specularBrightness * glare;
	}
	appliedWaterGlare = glare;
}

// ------------------------------------------------------------------ smoke and spray

float LTBLWorldLight::SceneLight() const
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* cell = player ? player->GetParentCell() : nullptr;
	if (cell && cell->IsInteriorCell()) {
		if (auto* l = cell->GetLighting()) {
			RE::NiColor amb{ l->ambient.red / 255.0f, l->ambient.green / 255.0f, l->ambient.blue / 255.0f };
			return std::clamp(Luma(amb) / 0.35f, 0.0f, 1.0f);
		}
		return 0.5f;
	}
	auto* sky = RE::Sky::GetSingleton();
	if (!sky)
		return 1.0f;
	const float amb = Luma(sky->skyColor[RE::TESWeather::ColorTypes::kAmbient]);
	const float sun = Luma(sky->skyColor[RE::TESWeather::ColorTypes::kSunlight]);
	// Clear midday is about 0.5 ambient + 0.8 sunlight; a moonless night is a few percent of that.
	return std::clamp((amb * 0.6f + sun * 0.4f) / 0.6f, 0.0f, 1.0f);
}

void LTBLWorldLight::ApplyMaterials(float sceneLight)
{
	const float smoke = std::lerp(std::clamp(settings.SmokeNightFloor, 0.0f, 1.0f), 1.0f, sceneLight);
	for (auto& [mat, t] : materials) {
		auto* m = static_cast<RE::BSEffectShaderMaterial*>(mat);
		float scale = t.originalScale;
		if (t.cls == EffectClass::kSmoke && settings.SmokeEnabled)
			scale *= smoke;
		else if (t.cls == EffectClass::kSpray && settings.WaterEnabled)
			scale *= std::clamp(settings.SprayBrightness, 0.0f, 1.0f) * std::lerp(0.35f, 1.0f, sceneLight);
		m->baseColorScale = scale;
	}
}

void LTBLWorldLight::RestoreMaterials()
{
	for (auto& [mat, t] : materials)
		static_cast<RE::BSEffectShaderMaterial*>(mat)->baseColorScale = t.originalScale;
	materials.clear();
}

// ------------------------------------------------------------------ living light

void LTBLWorldLight::RemoveLivingLight(LivingLight& ll)
{
	auto* ssn = globals::game::smState ? globals::game::smState->shadowSceneNode[0] : nullptr;
	if (ll.light) {
		if (ssn)
			ssn->RemoveLight(ll.light.get());
		if (ll.light->parent)
			ll.light->parent->DetachChild(ll.light.get());
	}
	ll.light.reset();
	ll.root.reset();
}

void LTBLWorldLight::RemoveAllLivingLights()
{
	for (auto& [id, ll] : livingLights)
		RemoveLivingLight(ll);
	livingLights.clear();
}

// ------------------------------------------------------------------ scan (main thread)

void LTBLWorldLight::Scan()
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	auto* tes = RE::TES::GetSingleton();
	auto* ssn = globals::game::smState ? globals::game::smState->shadowSceneNode[0] : nullptr;
	if (!player || !tes || !player->Is3DLoaded())
		return;

	std::unordered_map<RE::BSShaderMaterial*, TrackedMaterial> found;
	std::unordered_set<RE::FormID> glowSeen;
	const bool wantMaterials = settings.SmokeEnabled || settings.WaterEnabled;

	tes->ForEachReferenceInRange(player, settings.ScanRadius, [&](RE::TESObjectREFR* ref) {
		if (!ref || ref->IsDisabled())
			return RE::BSContainer::ForEachResult::kContinue;
		auto* root3D = ref->Get3D();
		if (!root3D)
			return RE::BSContainer::ForEachResult::kContinue;

		if (wantMaterials) {
			RE::BSVisit::TraverseScenegraphGeometries(root3D, [&](RE::BSGeometry* geo) {
				auto* prop = geo->GetGeometryRuntimeData().shaderProperty.get();
				auto* effect = prop ? netimmerse_cast<RE::BSEffectShaderProperty*>(prop) : nullptr;
				auto* mat = effect ? effect->material : nullptr;
				if (!mat || mat->GetType() != RE::BSShaderMaterial::Type::kEffect)
					return RE::BSVisit::BSVisitControl::kContinue;
				if (found.contains(mat))
					return RE::BSVisit::BSVisitControl::kContinue;
				if (auto it = materials.find(mat); it != materials.end()) {
					found.emplace(mat, it->second);  // still tracked: keep its original value
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				auto* em = static_cast<RE::BSEffectShaderMaterial*>(mat);
				const char* texPath = em->sourceTexturePath.c_str();
				const std::string tex = Lower(texPath ? texPath : "");
				if (tex.empty())
					return RE::BSVisit::BSVisitControl::kContinue;
				// Anything that makes its own light stays as it is.
				if (ContainsAny(tex, { "fire", "flame", "ember", "spark", "lava", "magic", "glow", "candle", "torch", "lightning", "shock", "soul" }))
					return RE::BSVisit::BSVisitControl::kContinue;
				EffectClass cls = EffectClass::kNone;
				if (ContainsAny(tex, { "waterfall", "wfall", "foam", "splash", "spray", "rapids", "mist", "ripple" }))
					cls = EffectClass::kSpray;
				else if (ContainsAny(tex, { "smoke", "steam", "dust", "fog", "haze", "vapor" }))
					cls = EffectClass::kSmoke;
				if (cls != EffectClass::kNone)
					found.emplace(mat, TrackedMaterial{ RE::NiPointer<RE::BSEffectShaderProperty>(effect), em->baseColorScale, cls });
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}

		if (settings.LivingLightEnabled && ssn) {
			auto* base = ref->GetBaseObject();
			auto* model = base ? base->As<RE::TESModel>() : nullptr;
			const char* path = model ? model->GetModel() : nullptr;
			if (path && *path) {
				const std::string lower = Lower(path);
				if (const GlowRule* rule = MatchGlow(lower)) {
					const auto id = ref->GetFormID();
					glowSeen.insert(id);
					auto* rootNode = root3D->AsNode();
					auto& ll = livingLights[id];
					if (ll.light && ll.root.get() != rootNode)
						RemoveLivingLight(ll);  // 3D was rebuilt
					if (!ll.light && rootNode) {
						RE::NiPointer<RE::NiPointLight> light(RE::NiPointLight::Create());
						if (light) {
							light->name = "LTBL_LivingLight";
							auto& d = light->GetLightRuntimeData();
							d.ambient = { 0.0f, 0.0f, 0.0f };
							d.diffuse = { rule->r, rule->g, rule->b };
							const float radius = rule->radius;
							d.radius = { radius, radius, radius };
							d.fade = rule->fade * settings.LivingLightScale;
							light->SetLightAttenuation(radius);
							light->local.translate = { 0.0f, 0.0f, 10.0f };  // a little above the base
							rootNode->AttachChild(light.get(), true);
							RE::ShadowSceneNode::LIGHT_CREATE_PARAMS params{};
							params.dynamic = true;
							params.shadowLight = false;
							params.portalStrict = false;
							params.affectLand = true;
							params.affectWater = true;
							params.neverFades = false;
							params.fov = 0.0f;
							params.falloff = 1.0f;
							params.nearDistance = 5.0f;
							params.depthBias = 0.0f;
							params.sceneGraphIndex = 0;
							params.restrictedNode = nullptr;
							params.lensFlareData = nullptr;
							ssn->AddLight(light.get(), params);
							ll.light = light;
							ll.root.reset(rootNode);
						}
					} else if (ll.light) {
						ll.light->GetLightRuntimeData().fade = rule->fade * settings.LivingLightScale;
					}
				}
			}
		}
		return RE::BSContainer::ForEachResult::kContinue;
	});

	// Materials no longer nearby: put them back before letting go.
	for (auto& [mat, t] : materials) {
		if (!found.contains(mat))
			static_cast<RE::BSEffectShaderMaterial*>(mat)->baseColorScale = t.originalScale;
	}
	materials = std::move(found);
	if (!wantMaterials)
		RestoreMaterials();

	// Lights whose plant, crystal or bug is gone or out of range.
	for (auto it = livingLights.begin(); it != livingLights.end();) {
		if (!settings.LivingLightEnabled || !glowSeen.contains(it->first)) {
			RemoveLivingLight(it->second);
			it = livingLights.erase(it);
		} else {
			++it;
		}
	}
}

// ------------------------------------------------------------------ per frame

void LTBLWorldLight::EarlyPrepass()
{
	if (settings.ColorEnabled)
		ApplyColor();
	else
		RestoreColor();

	float dt = globals::game::deltaTime ? *globals::game::deltaTime : (1.0f / 60.0f);
	dt = std::clamp(dt, 0.0f, 0.25f);
	timeSinceScan += dt;
	timeSinceApply += dt;

	if (timeSinceApply < kApplyInterval || scanQueued.load())
		return;
	const bool doScan = timeSinceScan >= kScanInterval;
	timeSinceApply = 0.0f;
	if (doScan)
		timeSinceScan = 0.0f;

	auto* tasks = SKSE::GetTaskInterface();
	if (!tasks)
		return;
	scanQueued = true;
	tasks->AddTask([this, doScan]() {
		ApplyWater();
		if (doScan)
			Scan();
		lastSceneLight = SceneLight();
		ApplyMaterials(lastSceneLight);
		scanQueued = false;
	});
}

#undef I18N_KEY_PREFIX
