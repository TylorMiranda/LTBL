#pragma once

// Let There Be Light (LTBL) - Candlelight
//
// Reworks the novice Alteration spell Candlelight from a bright floating orb into a wide,
// soft area of light around the caster that fades gradually with distance. No hot spot.
// Followers never receive it; they use torches (see Torch Bearers).

struct LTBLCandlelight : Feature
{
	virtual inline std::string GetName() override { return "LTBL Candlelight"; }
	virtual std::string GetDisplayName() override { return T("feature.ltbl_candlelight.name", "Candlelight Aura"); }
	virtual inline std::string GetShortName() override { return "LTBLCandlelight"; }
	virtual std::string_view GetCategory() const override { return FeatureCategories::kLighting; }
	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return { T("feature.ltbl_candlelight.description", "Candlelight lights a wide, soft area around you instead of a bright orb."),
			{ T("feature.ltbl_candlelight.key_feature_1", "Large lit area that fades gradually with distance"),
				T("feature.ltbl_candlelight.key_feature_2", "No blinding hot spot"),
				T("feature.ltbl_candlelight.key_feature_3", "Applies to any spell built on the self-cast light effect") } };
	}

	virtual void DrawSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;
	virtual void DataLoaded() override;

	struct Settings
	{
		bool Enabled = true;
		std::uint32_t Radius = 1400;  // game units (~20 m)
		float Intensity = 0.55f;      // light fade
		float Falloff = 0.6f;         // lower = softer, wider spread
	};

	Settings settings;

private:
	struct Original
	{
		RE::TESObjectLIGH* light;
		std::uint32_t radius;
		float fade;
		float falloff;
	};
	std::vector<Original> originals;

	void Apply();
};
