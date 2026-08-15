#pragma once

#include <vector>
#include <functional>

#include "willpower/common/Vector2.h"

#include "willpower/firepower/Platform.h"

namespace WP_NAMESPACE
{
	namespace firepower
	{

		struct BeamShard
		{
			Vector2 nearVertex, farVertex;
			float u, v;
		};

	} // firepower
} // WP_NAMESPACE

