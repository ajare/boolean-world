#pragma once

#include <array>
#include <string>


namespace bw
{
	namespace common
	{

		using MaterialDefinition = std::tuple<std::string, uint32_t, std::array<float, 3>>;
		using MaterialParameterDefinition = std::tuple<std::string, float, float, float>;

		// Material name, param count, base colour
		static std::array<MaterialDefinition, BW_MATERIAL_COUNT> MaterialNames = { {
			{ "Marble", 8, { 0.18f, 0.18f, 0.20f } },
			{ "Stone", 3, { 0.25f, 0.24f, 0.22f } }
		}};

		// Parameter name, min value, max value, default value
		static std::array MaterialParams
		{
			// Marble
			std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>
			{{
				{ "warp_scale", 0.0f, 5.0f, 1.1f },
				{ "veins_scale", 1.0f, 10.0f, 6.0f },
				{ "veins_fine_scale", 15.0f, 20.0f, 18.0f },
				{ "fine_detail_scale", 0.01f, 1.0f, 0.15f },
				{ "light_warm_mix", 0.0f, 1.0f, 0.25f },
				{ "vein_mix", 0.0f, 1.0f, 0.65f },
				{ "cloudiness", 0.0f, 1.0f, 0.2f },
				{ "fbm_scale", 0.01f, 1.0f, 0.5f }
			}},
			// Stone
			std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>
			{{
				{ "base_scale",  1.0f, 3.0f, 2.0f },
				{ "medium_scale", 5.0f, 10.0f, 8.0f },
				{ "stone_mix", 0.0f, 1.0f, 0.8f }
			}}
		};

	} // common
} // bw