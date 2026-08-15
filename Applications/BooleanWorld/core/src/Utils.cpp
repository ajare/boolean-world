#pragma once

#include <algorithm>

#include "core/Utils.h"


namespace bw
{
	namespace core
	{
		using namespace std;

		float clamp_angle(float angle)
		{
			while (angle < 0.0f)
			{
				angle += 360.0f;
			}

			while (angle >= 360.0f)
			{
				angle -= 360.0f;
			}

			return angle;
		}

		float clamp_unit(float value)
		{
			return clamp(value, 0.0f, 1.0f);
		}

		pair<wp::Vector2, wp::Vector2> calculateFovTriangle(wp::Vector2 const& pos, float viewAngle, float viewDist, float fov)
		{
			auto angle0 = viewAngle - fov * 0.5f;
			auto angle1 = viewAngle + fov * 0.5f;
			auto up = wp::Vector2(0, viewDist);

			auto v0 = pos + up.rotatedClockwiseCopy(angle0);
			auto v1 = pos + up.rotatedClockwiseCopy(angle1);

			return make_pair(v0, v1);
		}

	} // core
} // bw