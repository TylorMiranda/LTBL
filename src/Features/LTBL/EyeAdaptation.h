#pragma once

// Let There Be Light (LTBL) - Eye Adaptation
//
// The screen adapts the way the player character's eyes would.
// - Measured at the character (actor light level), so first and third person behave the same.
// - Brighter than baseline: glare that settles over 1 to 10 seconds, scaled by how bright.
// - Darker than baseline: adaptation over 20 (very dim) / 60 (very dark) / 90 (pitch black) seconds.
// - Continuous exponential response in log brightness, frame-rate independent, no stepping.

struct LTBLEyeAdaptation : Feature
{
	virtual inline std::string GetName() override { return "LTBL Eye Adaptation"; }
	virtual std::string GetDisplayName() override { return T("feature.ltbl_eye_adaptation.name", "Eye Adaptation"); }
	virtual inline std::string GetShortName() override { return "LTBLEyeAdaptation"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.ltbl_eye_adaptation.description", "Your view adapts to light and darkness the way real eyes do."),
			{ T("feature.ltbl_eye_adaptation.key_feature_1", "Glare settles in 1 to 10 seconds depending on brightness"),
				T("feature.ltbl_eye_adaptation.key_feature_2", "Darkness adapts over 20 to 90 seconds depending on how dark"),
				T("feature.ltbl_eye_adaptation.key_feature_3", "Measured at your character, identical in first and third person"),
				T("feature.ltbl_eye_adaptation.key_feature_4", "Smooth, continuous response with no visible stepping") } };
	}

	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	virtual void EarlyPrepass() override;
	virtual void GameLoaded() override { initialized = false; }

	struct Settings
	{
		bool Enabled = true;
		// Actor light level treated as "zero" (normal, comfortable light).
		float BaselineLight = 40.0f;
		// Glare (brighter than baseline).
		float GlareMinSeconds = 1.0f;
		float GlareMaxSeconds = 10.0f;
		float GlareFullStops = 6.0f;  // stops above baseline that produce the longest glare
		// Darkness (below baseline).
		float DimStops = 2.0f;
		float VeryDarkStops = 5.0f;
		float PitchBlackStops = 7.0f;
		float DimSeconds = 20.0f;
		float VeryDarkSeconds = 60.0f;
		float PitchBlackSeconds = 90.0f;
		// How far the eyes can compensate.
		float Strength = 0.85f;           // 1 = full compensation toward baseline
		float MaxDarkBoostStops = 3.0f;   // the most the eyes can open up in darkness (keeps pitch black barely visible)
		float MaxBrightCutStops = 2.5f;   // the most the eyes can close down in glare
		float NightDesaturation = 0.15f;  // subtle loss of color when dark adapted
		bool DisableVanillaAdaptation = true;
	};

	Settings settings;

	// Debug readout
	float lastLightLevel = 0.0f;
	float lastTargetLog = 0.0f;
	float lastCompStops = 0.0f;

private:
	bool initialized = false;
	float adaptedLog = 0.0f;

	// Cached values so we never compound our own writes if the engine does not rebuild the blended imagespace.
	float sourceBrightness = 1.0f;
	float sourceSaturation = 1.0f;
	float sourceAdaptStrength = 0.0f;
	float writtenBrightness = -1.0f;
	float writtenSaturation = -1.0f;
	float writtenAdaptStrength = -1.0f;
	bool wroteLastFrame = false;

	float DurationForDarkening(float stopsBelowBaseline) const;
	void RestoreImagespace();
};
