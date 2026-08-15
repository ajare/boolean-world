#include "willpower/common/MathsUtils.h"
#include "willpower/firepower/AreaQuery.h"

using namespace std;

namespace WP_NAMESPACE
{
	namespace firepower
	{
		using namespace std;

		bool AreaQuery::lineIntersectsMesh(Vector2 const& v0, Vector2 const& v1, Vector2 const& dir, Mesh const& mesh, vector<pair<uint32_t, uint32_t>> const& verticesToEdges, int32_t ignoreVertex)
		{
			int32_t ignore0 = -1, ignore1 = -1;
			if (ignoreVertex >= 0)
			{
				ignore0 = verticesToEdges[ignoreVertex].first;
				ignore1 = verticesToEdges[ignoreVertex].second;
			}

			int numEdges = (int)mesh.edges.size();
			for (int i = 0; i < numEdges; ++i)
			{
				if (i == ignore0 || i == ignore1)
				{
					continue;
				}

				auto const& edge = mesh.edges[i];

				if (edge.n.dot(dir) < 0)
				{
					continue;
				}

				if (MathsUtils::lineIntersectsLine(v0, v1, edge.v[0], edge.v[1]) ==
					MathsUtils::LineIntersectionType::Intersecting)
				{
					return true;
				}
			}

			return false;
		}

		Vector2 AreaQuery::projectLineThroughMesh(Vector2 const& v0, Vector2 const& v1, Vector2 const& dir, Mesh const& mesh, bool* isFull)
		{
			float hitDist = 1e10f;
			Vector2 hitPos = v1;

			*isFull = true;

			int numEdges = (int)mesh.edges.size();
			for (int i = 0; i < numEdges; ++i)
			{
				auto const& edge = mesh.edges[i];

				if (edge.n.dot(dir) >= 0)
				{
					continue;
				}

				LineHit hit;
				if (MathsUtils::lineLineIntersection(v0, v1, edge.v[0], edge.v[1], &hit) !=
					MathsUtils::LineIntersectionType::NotIntersecting)
				{
					if (!hit.isTouching())
					{
						float d = hit.getPosition().distanceToSq(v0);
						if (d < hitDist)
						{
							hitPos = hit.getPosition();
							hitDist = d;
						}

						*isFull = false;
					}
				}
			}

			return hitPos;
		}

		Vector2 AreaQuery::projectSide(Vector2 const& pos, Vector2 const& proj, Vector2 const& e00, Vector2 const& e01, Vector2 const& e10, Vector2 const& e11)
		{
			Vector2 start = pos;
			Vector2 end = start + proj;

			LineHit hit0, hit1;
			MathsUtils::lineLineIntersection(start, end, e00, e01, &hit0);
			MathsUtils::lineLineIntersection(start, end, e10, e11, &hit1);

			float h0time = hit0.getTime();
			float h1time = hit1.getTime();

			Vector2 hitPos;
			if (h0time < 0.0f)
			{
				hitPos = hit1.getPosition();
			}
			else if (h1time < 0.0f)
			{
				hitPos = hit0.getPosition();
			}
			else
			{
				hitPos = h0time < h1time ? hit0.getPosition() : hit1.getPosition();
			}

			return hitPos;
		}

	} // firepower
} // WP_NAMESPACE