#pragma once

#include <unordered_map>
#include <unordered_set>

// Let There Be Light (LTBL) - World Light
//
// The look of light across the world, built natively so no other mod is needed:
// - Color: neutral white balance (weather tints removed) with bold, rich color.
// - Water glare: water's sun glints and sparkle toned down; waterfalls, foam, spray and mist are
//   no longer blinding.
// - Smoke and haze obey light: smoke, steam, dust and fog cards are effects the engine draws
//   unlit, so they glowed at night. They now darken with the scene's light (day, night, interiors).
// - Living light: things that glow in the lore cast real light: nirnroots (blue, crimson red),
//   glowing mushrooms, Blackreach crystals and torchbugs.
// Built to grow: each part is a module with its own settings.

struct LTBLWorldLight : Feature
{
	virtual inline std::string GetName() override { return "LTBL World Light"; }
	virtual std::string GetDisplayName() override { return T("feature.ltbl_world_light.name", "World Light"); }
	virtual inline std::string GetShortName() override { return "LTBLWorldLight"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.ltbl_world_light.description", "Neutral, bold color; calm water glare; smoke that darkens at night; real light from glowing plants, crystals and torchbugs."),
			{ T("feature.ltbl_world_light.key_feature_1", "Neutral white balance with bold color"),
				T("feature.ltbl_world_light.key_feature_2", "Waterfalls, foam and water glints no longer blinding"),
				T("feature.ltbl_world_light.key_feature_3", "Smoke, steam, dust and fog darken with the light around them"),
				T("feature.ltbl_world_light.key_feature_4", "Nirnroots, glowing mushrooms, Blackreach crystals and torchbugs cast real light") } };
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
		// Color
		bool ColorEnabled = true;
		float Neutralize = 1.0f;   // share of the weather/imagespace color tint removed
		float Saturation = 1.2f;   // multiplier on the scene's saturation
		float Contrast = 1.05f;    // multiplier on the scene's contrast
		// Water
		bool WaterEnabled = true;
		float WaterGlare = 0.55f;  // multiplier on water sun glints, sparkle and specular
		float SprayBrightness = 0.65f;  // cap on waterfall, foam, spray and mist brightness
		// Smoke and haze
		bool SmokeEnabled = true;
		float SmokeNightFloor = 0.08f;  // how dark smoke gets in full darkness (share of daytime)
		// Living light
		bool LivingLightEnabled = true;
		float LivingLightScale = 1.0f;
		float ScanRadius = 6000.0f;
	};
	Settings settings;

private:
	// Color
	void ApplyColor();
	void RestoreColor();
	bool colorWritten = false;
	float srcSaturation = 1.0f, srcContrast = 1.0f, srcTint = 0.0f;
	float wrSaturation = -1.0f, wrContrast = -1.0f, wrTint = -1.0f;

	// Water
	struct WaterOriginal
	{
		float sunSpecularMagnitude, sunSparkleMagnitude, specularBrightness;
	};
	std::unordered_map<RE::TESWaterForm*, WaterOriginal> waterOriginals;
	float appliedWaterGlare = -1.0f;
	void ApplyWater();

	// Effect materials (smoke and spray)
	enum class EffectClass : std::uint8_t
	{
		kNone,
		kSmoke,
		kSpray
	};
	struct TrackedMaterial
	{
		RE::NiPointer<RE::BSEffectShaderProperty> holder;  // keeps the material alive while we track it
		float originalScale;
		EffectClass cls;
	};
	std::unordered_map<RE::BSShaderMaterial*, TrackedMaterial> materials;
	void ApplyMaterials(float sceneLight);
	void RestoreMaterials();

	// Living light
	struct LivingLight
	{
		RE::NiPointer<RE::NiPointLight> light;
		RE::NiPointer<RE::NiNode> root;
	};
	std::unordered_map<RE::FormID, LivingLight> livingLights;
	void RemoveLivingLight(LivingLight& ll);
	void RemoveAllLivingLights();

	// Main-thread work
	void Scan();
	float SceneLight() const;
	float timeSinceScan = 0.0f;
	float timeSinceApply = 0.0f;
	std::atomic<bool> scanQueued = false;
	float lastSceneLight = 1.0f;
};
