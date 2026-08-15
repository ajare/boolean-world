#pragma once

#include <willpower/common/Vector2.h>

#include "core/Platform.h"


namespace bw
{
	namespace core
	{

		float BW_API clamp_angle(float angle);

		float BW_API clamp_unit(float value);

		std::pair<wp::Vector2, wp::Vector2> calculateFovTriangle(wp::Vector2 const& pos, float viewAngle, float viewDist, float fov);

	} // core
} // bw