#pragma once

#include "willpower/common/Vector2.h"

#include "willpower/firepower/Platform.h"

namespace WP_NAMESPACE
{
	namespace firepower
	{
		struct MeshCollisionEdge
		{
			uint32 vertices[2], normal;
		};

	} // firepower
} // WP_NAMESPACW