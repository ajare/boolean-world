#include <random>
#include <stack>

#include "core/VertexDistribution.h"


namespace bw
{
	namespace core
	{

		using namespace std;

		VertexDistribution::VertexDistribution()
		{
		}

		void VertexDistribution::generatePoissonDiskFlat(wp::BoundingBox const& area, float r, float z)
		{
			// https://github.com/corporateshark/poisson-disk-generator
			// ^^ has custom density map, but this is done by oversampling then removing points
			//    based on density image

			// See https://www.cs.ubc.ca/~rbridson/docs/bridson-siggraph07-poissondisk.pdf
			// Impl: https://github.com/martynafford/poisson-disc-distribution-bridson/blob/master/include/poisson_disc_distribution.hpp
			// For variable r, see https://cds.ismrm.org/protected/21MProceedings/PDFfiles/1190.html
			//
			// 0. k = 30
			// 1. cell_size = r / sqrt(2)
			// 2. calculate number of cells based on area and cell_size and initialize int array to -1
			// 3. choose random 2d point and insert into cell array (ie update the relevant cell from -1 to 0),
			//    and add point to vector and add 0 to an "active list"
			// 4. while active list is not empty:
			//    - choose random index in it
			//    - choose up to k points in torus around chosen point of radii r and 2r
			//      - for each point, test if a neighbouring cell has a point within distance r
			//        - if no, add
			//        - if yes, ignore, try next.  if all k points generated, remove current from active list

			const int k = 30;
			const float cellSize = r / sqrtf(2.0f);

			wp::Vector2 minExtent, maxExtent;

			area.getExtents(minExtent, maxExtent);
			auto domainSize = area.getSize();

			// Generate grid
			int dx = (int)ceil(domainSize.x / cellSize);
			int dy = (int)ceil(domainSize.y / cellSize);

			// Initialise grid to 0
			auto grid = make_unique<int[]>(dx * dy);

			mPoints.clear();
			stack<int> work;

			mt19937 rng;

			constexpr float rngRange = (float)(rng.max() - rng.min());
			auto rngScale = domainSize / rngRange;

			// Generate first point
			float x = minExtent.x + rng() * rngScale.x;
			float y = minExtent.y + rng() * rngScale.y;

			mPoints.push_back({ x, y });
			//grid
			//work.push(0);

		}

	} // core
} // bw