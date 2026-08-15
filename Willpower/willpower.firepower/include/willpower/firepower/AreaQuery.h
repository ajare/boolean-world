#pragma once

#include <vector>

#include "willpower/common/Vector2.h"

#include "willpower/firepower/Platform.h"

#include "willpower/firepower/Mesh.h"

namespace WP_NAMESPACE
{
	namespace firepower
	{

		class WP_FIREPOWER_API AreaQuery
		{
		protected:

			bool lineIntersectsMesh(Vector2 const& v0, Vector2 const& v1, Vector2 const& dir, Mesh const& mesh, std::vector<std::pair<uint32_t, uint32_t>> const& verticesToEdges, int32_t ignoreVertex = -1);

			Vector2 projectLineThroughMesh(Vector2 const& v0, Vector2 const& v1, Vector2 const& dir, Mesh const& mesh, bool* isFull);

			Vector2 projectSide(Vector2 const& pos, Vector2 const& proj, Vector2 const& e00, Vector2 const& e01, Vector2 const& e10, Vector2 const& e11);
		};

	} // firepower
} // WP_NAMESPACE

