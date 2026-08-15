#include <unordered_map>
#include <unordered_set>
#include <set>
#include <cmath>
#include <algorithm>

#include <mapbox/earcut.hpp>

#include "Expr.h"


namespace expr
{
	using namespace Clipper2Lib;
	using namespace std;

	struct Segment
	{
		Vertex v[2];
		int p;
	};

	uint64_t canonicalBits(double d)
	{
		if (d == 0.0)
		{
			// Convert -0.0 to +0.0
			d = 0.0;
		}

		if (isnan(d))
		{
			return 0x7ff8000000000000ULL;
		}

		uint64_t bits;
		memcpy(&bits, &d, sizeof(bits));
		return bits;
	}

	struct PointHash
	{
		size_t operator()(Vertex const& v) const
		{
			uint64_t x = canonicalBits(v.x);
			uint64_t y = canonicalBits(v.y);

			return hash<uint64_t>()(x ^ (y + 0x9e3779b97f4a7c15ULL + (x << 6) + (x >> 2)));
		}
	};

	struct PointEq
	{
		bool operator()(Vertex const& a, Vertex const& b) const
		{
			return a.x == b.x && a.y == b.y;
		}
	};

	vector<Segment>	ExtractSegments(vector<bw::core::Clipper2Polygon> const& polygons)
	{
		vector<Segment> result;

		for (int i = 0; i < (int)polygons.size(); ++i)
		{
			auto const& path = polygons[i].path;

			if (path.size() < 2)
			{
				continue;
			}

			for (size_t j = 0; j < path.size(); ++j)
			{
				auto k = (j + 1) % path.size();

				Vertex a{ path[j].x / 1000.0, path[j].y / 1000.0 };
				Vertex b{ path[k].x / 1000.0, path[k].y / 1000.0 };

				result.push_back({ a, b, i });
			}
		}

		return result;
	}

	struct Intersection
	{
		bool hit;
		double t0;
		double t1;
		Vertex v;
	};

	struct Overlap
	{
		bool hit = false;
		Vertex a;
		Vertex b;
	};

	static inline double Cross(Vertex const& a, Vertex const& b, Vertex const& c)
	{
		return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
	}

	static inline bool Collinear(Vertex const& a, Vertex const& b, Vertex const& c)
	{
		return Cross(a, b, c) == 0;
	}

	Overlap CollinearOverlap(Segment const& s0, Segment const& s1)
	{
		if (!Collinear(s0.v[0], s0.v[1], s1.v[0]))
		{
			return { false, Vertex(), Vertex() };
		}

		if (!Collinear(s0.v[0], s0.v[1], s1.v[1]))
		{
			return { false, Vertex(), Vertex() };
		}

		bool useX = abs(s0.v[1].x - s0.v[0].x) >= abs(s0.v[1].y - s0.v[0].y);

		auto coord = [&](Vertex const& v)
		{
			return useX ? v.x : v.y;
		};

		auto a0 = coord(s0.v[0]);
		auto a1 = coord(s0.v[1]);

		auto b0 = coord(s1.v[0]);
		auto b1 = coord(s1.v[1]);

		if (a0 > a1)
		{
			swap(a0, a1);
		}

		if (b0 > b1)
		{
			swap(b0, b1);
		}

		double lo = max(a0, b0);
		double hi = min(a1, b1);

		if (lo > hi)
		{
			return { false, Vertex(), Vertex() };
		}

		auto PointAtCoord = [&](Segment const& s, double c)
		{
			auto sx = s.v[1].x - s.v[0].x;
			auto sy = s.v[1].y - s.v[0].y;

			if (useX)
			{
				double t = (c - s.v[0].x) / sx;

				return Vertex
				{
					c,
					(double)llround(s.v[0].y + t * sy)
				};
			}
			else
			{
				double t = (c - s.v[0].y) / sy;

				return Vertex
				{
					(double)llround(s.v[0].x + t * sx),
					c
				};
			}
		};

		return
		{
			true,
			PointAtCoord(s0, lo),
			PointAtCoord(s0, hi)
		};
	}

	double ParameterOnSegment(Segment const& s, Vertex const& v)
	{
		double dx = s.v[1].x - s.v[0].x;
		double dy = s.v[1].y - s.v[0].y;

		if (abs(dx) >= abs(dy))
		{
			if (dx == 0)
			{
				return 0.0;
			}

			return (v.x - s.v[0].x) / dx;
		}
		else
		{
			if (dy == 0)
			{
				return 0.0;
			}

			return (v.y - s.v[0].y) / dy;
		}
	}

	static double SideOfSegment(Vertex const& p, Vertex const& a, Vertex const& b)
	{
		double x0 = (b.y - a.y) * p.x;
		double x1 = (a.x - b.x) * p.y;
		double x2 = (b.x * a.y - a.x * b.y);
		return x0 + x1 + x2;
	}

	Intersection SegmentIntersection(Segment const& subject, Segment const& clip)
	{
		auto x1 = subject.v[0].x;
		auto y1 = subject.v[0].y;
		auto x2 = subject.v[1].x;
		auto y2 = subject.v[1].y;

		auto x3 = clip.v[0].x;
		auto y3 = clip.v[0].y;
		auto x4 = clip.v[1].x;
		auto y4 = clip.v[1].y;

		auto dx1 = x2 - x1;
		auto dy1 = y2 - y1;

		auto dx2 = x4 - x3;
		auto dy2 = y4 - y3;

		auto denom = dx1 * dy2 - dy1 * dx2;

		if (abs(denom) < 1e-12)
		{
			return { false, 0, 0, Vertex() };
		}

		double t = ((x3 - x1) * dy2 - (y3 - y1) * dx2) / denom;
		double u = ((x3 - x1) * dy1 - (y3 - y1) * dx1) / denom;

		if (t < 0.0 || t > 1.0)
		{
			return { false, 0, 0, Vertex() };
		}

		if (u < 0.0 || u > 1.0)
		{
			return { false, 0, 0, Vertex() };
		}

		Vertex v((double)llround(x1 + t * dx1), (double)llround(y1 + t * dy1));

		// Check they're not meeting at an endpoint
		if (v == subject.v[0] || v == subject.v[1] || v == clip.v[0] || v == clip.v[1])
		{
			return { false, 0, 0, Vertex() };
		}

		return { true, t, u, v };
	}

	PSLG BuildPSLG(vector<bw::core::Clipper2Polygon> const& polygons, vector<bw::core::Primitive*> const& primitives)
	{
		// It is important here to maintain the order of everything.  'polygons' argument
		// should be ordered by priority, ie clip order.  This order needs to be maintained
		// for segments, so we can be guaranteed that when we test segments against each other,
		// we know which is the subject and which is the clip polygon.
		vector<Segment> segs = ExtractSegments(polygons);

		size_t n = segs.size();

		struct SplitPoint
		{
			double t;
			Vertex v;
		};

		vector<vector<SplitPoint>> splits(n);

		for (size_t i = 0; i < n; ++i)
		{
			splits[i].push_back({ 0.0, segs[i].v[0] });
			splits[i].push_back({ 1.0, segs[i].v[1] });
		}

		for (size_t i = 0; i < n; ++i)
		{
			for (size_t j = i + 1; j < n; ++j)
			{
				auto hit = SegmentIntersection(segs[i], segs[j]);

				if (hit.hit)
				{
					splits[i].push_back({ hit.t0, hit.v });
					splits[j].push_back({ hit.t1, hit.v });
					continue;
				}

				auto ov = CollinearOverlap(segs[i], segs[j]);

				if (!ov.hit)
				{
					continue;
				}

				double t0a = ParameterOnSegment(segs[i], ov.a);
				double t0b = ParameterOnSegment(segs[i], ov.b);
				double t1a = ParameterOnSegment(segs[j], ov.a);
				double t1b = ParameterOnSegment(segs[j], ov.b);

				splits[i].push_back({ t0a, ov.a });
				splits[i].push_back({ t0b, ov.b });

				splits[j].push_back({ t1a, ov.a });
				splits[j].push_back({ t1b, ov.b });
			}
		}

		PSLG graph;

		unordered_map<Vertex, int, PointHash, PointEq> vertexMap;

		auto getVertex =
			[&](Vertex const& v)
		{
			auto it = vertexMap.find(v);

			if (it != vertexMap.end())
			{
				return it->second;
			}

			auto idx = (int)graph.vs.size();
			graph.vs.push_back({ v });
			vertexMap[v] = idx;

			return idx;
		};

		typedef pair<int, int> EdgeDef;
		map<EdgeDef, int> edgeMap;

		for (size_t i = 0; i < n; ++i)
		{
			auto& pts = splits[i];

			sort(pts.begin(), pts.end(), [](auto& a, auto& b)
			{
				return a.t < b.t;
			});

			for (size_t k = 0; k + 1 < pts.size(); ++k)
			{
				auto a = pts[k].v;
				auto b = pts[k + 1].v;

				if (a == b)
					continue;

				int va = getVertex(a);
				int vb = getVertex(b);

				if (va > vb)
					swap(va, vb);

				int edgeIndex = (int)graph.es.size();
				auto key = EdgeDef(va, vb);

				auto it = edgeMap.insert({ key, edgeIndex });

				if (it.second)
				{
					// New edge
					graph.es.push_back({ va, vb });
				}
			}
		}

		return graph;
	}

	Vertex SamplePoint(PSLG const& graph, Cycle const& cycle)
	{
		// Get edge midpoint
		auto p0 = graph.vs[cycle.vis[0]];
		auto p1 = graph.vs[cycle.vis[1]];

		Vertex mid{
			(p0.x + p1.x) / 2,
			(p0.y + p1.y) / 2
		};

		auto dx = p1.x - p0.x;
		auto dy = p1.y - p0.y;

		if (cycle.area > 0)
		{
			// Nudge left
			mid.x -= dy * 0.001;
			mid.y += dx * 0.001;
		}
		else
		{
			mid.x += dy * 0.001;
			mid.y -= dx * 0.001;
		}

		return mid;
	}

	vector<Cycle> ExtractMinimalCycles(PSLG const& graph)
	{
		struct HalfEdge
		{
			int v[2];
			int twin;
			int e;
			int next = -1;
			bool visited = false;
			double angle;
		};

		vector<HalfEdge> halfEdges;
		halfEdges.reserve(graph.es.size() * 2);

		vector<vector<int>>	outgoing(graph.vs.size());

		//
		// Build half-edges
		//
		for (int i = 0; i < (int)graph.es.size(); ++i)
		{
			const auto& e = graph.es[i];
			int h0 = (int)halfEdges.size();

			halfEdges.push_back({
				{ e.vi[0], e.vi[1] },
				h0 + 1,
				i,
				-1,
				false,
				0.0
			});

			halfEdges.push_back({
				{ e.vi[1], e.vi[0] },
				h0,
				i,
				-1,
				false,
				0.0
			});

			outgoing[e.vi[0]].push_back(h0);
			outgoing[e.vi[1]].push_back(h0 + 1);
		}

		//
		// Compute angles
		//
		for (auto& h : halfEdges)
		{
			auto const& a = graph.vs[h.v[0]];
			auto const& b = graph.vs[h.v[1]];

			h.angle = atan2(double(b.y - a.y), double(b.x - a.x));
		}

		//
		// Sort outgoing edges CCW
		//
		for (auto& list : outgoing)
		{
			sort(list.begin(), list.end(), [&](int lhs, int rhs)
			{
				return halfEdges[lhs].angle < halfEdges[rhs].angle;
			});
		}

		//
		// Build next-face pointers
		//
		for (size_t v = 0; v < outgoing.size(); ++v)
		{
			auto& list = outgoing[v];

			int n = (int)list.size();

			for (int i = 0; i < n; ++i)
			{
				int h = list[i];
				int twin = halfEdges[h].twin;
				int twinOrigin = halfEdges[twin].v[0];
				auto& twinList = outgoing[twinOrigin];

				auto it = find(twinList.begin(), twinList.end(), twin);
				int idx = (int)distance(twinList.begin(), it);

				//
				// Previous edge in CCW order
				//
				int nextIdx = (idx - 1 + (int)twinList.size()) % (int)twinList.size();
				halfEdges[h].next = twinList[nextIdx];
			}
		}

		vector<Cycle> cycles;

		//
		// Walk faces
		//
		for (size_t h = 0; h < halfEdges.size(); ++h)
		{
			if (halfEdges[h].visited)
			{
				continue;
			}

			Cycle cycle;

			int start = (int)h;
			int cur = start;

			do
			{
				halfEdges[cur].visited = true;

				cycle.vis.push_back(halfEdges[cur].v[0]);
				cycle.eis.push_back(halfEdges[cur].e);

				cur = halfEdges[cur].next;

			} while (cur != start);

			//
			// Compute signed area
			//
			double area = 0.0;

			int n = (int)cycle.vis.size();

			for (int i = 0; i < n; ++i)
			{
				const auto& v0 = graph.vs[cycle.vis[i]];
				const auto& v1 = graph.vs[cycle.vis[(i + 1) % n]];

				area += v0.x * v1.y - v0.y * v1.x;
			}

			cycle.area = area * 0.5;
			cycle.interiorPoint = SamplePoint(graph, cycle);

			cycles.push_back(move(cycle));
		}

		return cycles;
	}

	bool PointOnSegment(Vertex const& v, Vertex const& a, Vertex const& b)
	{
		if (Cross(a, b, v) != 0)
		{
			return false;
		}

		return
			v.x >= min(a.x, b.x) &&
			v.x <= max(a.x, b.x) &&
			v.y >= min(a.y, b.y) &&
			v.y <= max(a.y, b.y);
	}

	int PointInPolygon(Vertex const& v, Path64 const& poly)
	{
		int winding = 0;
		auto n = (int)poly.size();

		for (int i = 0; i < n; ++i)
		{
			int j = (i + 1) % n;

			Vertex a = { poly[i].x / 1000.f, poly[i].y / 1000.0f };
			Vertex b = { poly[j].x / 1000.f, poly[j].y / 1000.0f };

			if (PointOnSegment(v, a, b))
			{
				return -1;
			}

			if (a.y <= v.y)
			{
				if (b.y > v.y)
				{
					if (Cross(a, b, v) > 0)
					{
						++winding;
					}
				}
			}
			else
			{
				if (b.y <= v.y)
				{
					if (Cross(a, b, v) < 0)
					{
						--winding;
					}
				}
			}
		}

		return winding == 0 ? 0 : 1;
	}

	int PointInCycle(Vertex const& v, Cycle const& cycle, PSLG const& graph)
	{
		int winding = 0;
		auto n = (int)cycle.vis.size();

		for (int i = 0; i < n; ++i)
		{
			int j = (i + 1) % n;

			Vertex const& a = graph.vs[cycle.vis[i]];
			Vertex const& b = graph.vs[cycle.vis[j]];

			if (PointOnSegment(v, a, b))
			{
				return -1;
			}

			if (a.y <= v.y)
			{
				if (b.y > v.y)
				{
					if (Cross(a, b, v) > 0)
					{
						++winding;
					}
				}
			}
			else
			{
				if (b.y <= v.y)
				{
					if (Cross(a, b, v) < 0)
					{
						--winding;
					}
				}
			}
		}

		return winding == 0 ? 0 : 1;
	}

	bool PointInFace(Vertex const& v, Face const& face, std::vector<Cycle> const& cycles, PSLG const& graph)
	{
		auto r = PointInCycle(v, cycles[face.polygon], graph);

		if (r <= 0)
		{
			return false;
		}

		for (auto hole : face.holes)
		{
			r = PointInCycle(v, cycles[hole], graph);

			if (r > 0)
			{
				return false;
			}
		}

		return true;
	}

	struct Box
	{
		double minx;
		double miny;
		double maxx;
		double maxy;
	};

	Box GetBounds(
		const PSLG& graph,
		const Cycle& cycle)
	{
		Box b;

		auto const& v0 = graph.vs[cycle.vis[0]];

		b.minx = b.maxx = v0.x;
		b.miny = b.maxy = v0.y;

		for (int vi : cycle.vis)
		{
			auto const& v = graph.vs[vi];

			b.minx = min(b.minx, v.x);
			b.maxx = max(b.maxx, v.x);

			b.miny = min(b.miny, v.y);
			b.maxy = max(b.maxy, v.y);
		}

		return b;
	}

	bool ContainsBox(Box const& outer, Box const& inner)
	{
		return
			outer.minx < inner.minx &&
			outer.maxx > inner.maxx &&
			outer.miny < inner.miny &&
			outer.maxy > inner.maxy;
	}

	bool PointInCycle(PSLG const& graph, Cycle const& cycle, Vertex const& v)
	{
		bool inside = false;
		auto n = (int)cycle.vis.size();

		for (int i = 0, j = n - 1; i < n; j = i++)
		{
			auto a = graph.vs[cycle.vis[i]];
			auto b = graph.vs[cycle.vis[j]];

			bool intersect = ((a.y > v.y) != (b.y > v.y)) && (v.x < (b.x - a.x) * (v.y - a.y) / (b.y - a.y) + a.x);

			if (intersect)
			{
				inside = !inside;
			}
		}

		return inside;
	}


	vector<PolygonNode> BuildPolygonHierarchy(PSLG const& graph, vector<Cycle>& cycles)
	{
		int n = (int)cycles.size();

		vector<PolygonNode> nodes(n);
		vector<Box> boxes(n);

		for (int i = 0; i < n; ++i)
		{
			nodes[i].cycleIndex = i;
			boxes[i] = GetBounds(graph, cycles[i]);
		}

		//
		// Find immediate parent
		//
		for (int i = 0; i < n; ++i)
		{
			auto sample = SamplePoint(graph, cycles[i]);
			double bestArea = numeric_limits<double>::max();
			int bestParent = -1;

			for (int j = 0; j < n; ++j)
			{
				if (i == j)
				{
					continue;
				}

				if (cycles[j].area < 0)
				{
					continue;
				}

				if (!ContainsBox(boxes[j], boxes[i]))
				{
					continue;
				}

				if (!PointInCycle(graph, cycles[j], sample))
				{
					continue;
				}

				double area = abs(cycles[j].area);

				if (area < bestArea)
				{
					bestArea = area;
					bestParent = j;
				}
			}

			nodes[i].parent = bestParent;

			if (nodes[i].parent < 0 && cycles[i].area < 0)
			{
				cycles[i].bounded = false;
			}
		}

		for (int i = 0; i < n; ++i)
		{
			auto parent = nodes[i].parent;

			if (parent >= 0)
			{
				// All cycles which were given an unbounded cycle as parent need to be fixed
				if (!cycles[nodes[parent].cycleIndex].bounded)
				{
					nodes[i].parent = -1;
				}

				// All cycles with positive area and whose parent has a positive area must be detached
				if (cycles[nodes[i].cycleIndex].area > 0 && cycles[nodes[parent].cycleIndex].area > 0)
				{
					nodes[i].parent = -1;
				}
			}
		}

		//
		// Build child lists
		//
		for (int i = 0; i < n; ++i)
		{
			int parent = nodes[i].parent;

			if (parent >= 0)
			{
				nodes[parent].children.push_back(i);
			}
		}

		return nodes;
	}

	vector<Face> BuildFaces(vector<PolygonNode> const& nodes, vector<Cycle> const& cycles)
	{
		auto n = (int)nodes.size();
		vector<Face> faces;

		for (int i = 0; i < n; ++i)
		{
			// Only create faces for positive
			if (cycles[nodes[i].cycleIndex].area < 0)
			{
				continue;
			}

			Face face;

			face.polygon = nodes[i].cycleIndex;

			for (auto cIndex : nodes[i].children)
			{
				face.holes.push_back(nodes[cIndex].cycleIndex);
			}

			faces.push_back(face);
		}

		return faces;
	}

	vector<Face> CalculateOwningPolygons(vector<Face> const& faces, vector<bw::core::Clipper2Polygon> const& polygons, vector<Cycle> const& cycles, PSLG& graph, vector<bw::core::Primitive*> const& primitives)
	{
		vector<Face> keepFaces;
		auto n = (int)faces.size();
		auto c = (int)polygons.size();

		vector<int> holeFaceIndices;

		for (int i = 0; i < n; ++i)
		{
			auto& cycle = cycles[faces[i].polygon];
			auto sample = SamplePoint(graph, cycle);

			Face f = faces[i];

			// Test sample point against all input polygons
			int prevOwner{ -1 };

			for (int j = 0; j < c; ++j)
			{
				if (PointInPolygon(sample, polygons[j].path))
				{
					// Assume that polygons are ordered, so if this polygon contains the point,
					// it is the new owner
					auto prim = primitives[polygons[j].primitiveIndex];

					if (prim->getOperation() == bw::core::Primitive::Operation::Difference)
					{
						// Revert to previous owning polygon
						f.holePolygon = j;
						f.owningPolygon = prevOwner;
					}
					else
					{
						prevOwner = f.owningPolygon;
						f.owningPolygon = j;
					}
				}
			}

			if (f.owningPolygon < 0)
			{
				holeFaceIndices.push_back(i);
				continue;
			}

			auto prim = primitives[polygons[f.owningPolygon].primitiveIndex];

			// Update edges
			for (int edgeIndex : cycle.eis)
			{
				if (graph.es[edgeIndex].fi[0] < 0)
				{
					graph.es[edgeIndex].fi[0] = i;
				}
				else
				{
					graph.es[edgeIndex].fi[1] = i;
				}
			}

			for (auto const& hole : f.holes)
			{
				auto const& holeCycle = cycles[hole];

				for (int edgeIndex : holeCycle.eis)
				{
					if (graph.es[edgeIndex].fi[0] < 0)
					{
						graph.es[edgeIndex].fi[0] = i;
					}
					else
					{
						graph.es[edgeIndex].fi[1] = i;
					}
				}
			}

			if (prim->getOperation() != bw::core::Primitive::Operation::Difference)
			{
				keepFaces.push_back(f);
			}
		}

		// Set the edges of holes which were removed at the end.  They are not set above,
		// because they are not part of the face list any more
		for (auto& holeFaceIndex : holeFaceIndices)
		{
			auto const& cycle = cycles[faces[holeFaceIndex].polygon];

			// Update edges
			for (int ei : cycle.eis)
			{
				graph.es[ei].fi[0] = holeFaceIndex;
				graph.es[ei].fi[1] = -1;
			}
		}

		return keepFaces;
	}

	vector<FaceTriangle> BuildFaceTriangles(vector<Face> const& faces, std::vector<Cycle> const& cycles, PSLG const& graph)
	{
		using EarcutPoint = array<float, 2>;

		vector<FaceTriangle> tris;

		for (size_t i = 0; i < faces.size(); ++i)
		{
			vector<vector<EarcutPoint>> inputPolygons;
			vector<int> vertexIndices;

			auto const& cycle = cycles[faces[i].polygon];

			// Add main face
			vector<EarcutPoint> mainFace;

			for (auto const& vi : cycle.vis)
			{
				auto const& v = graph.vs[vi];

				mainFace.push_back({ (float)v.x, (float)v.y });
				vertexIndices.push_back(vi);
			}

			inputPolygons.push_back(mainFace);

			// Add children, treating islands as holes
			for (auto ic : faces[i].holes)
			{
				vector<EarcutPoint> face;

				for (auto const& vi : cycles[ic].vis)
				{
					auto const& v = graph.vs[vi];

					face.push_back({ (float)v.x, (float)v.y });
					vertexIndices.push_back(vi);
				}

				inputPolygons.push_back(face);
			}

			// Perform triangulation
			vector<uint32_t> triangleIndices = mapbox::earcut<uint32_t>(inputPolygons);
			auto numTriangleIndices = (uint32_t)triangleIndices.size();

			for (uint32_t j = 0; j < numTriangleIndices; j += 3)
			{
				auto v0 = (int)triangleIndices[j + 0];
				auto v1 = (int)triangleIndices[j + 1];
				auto v2 = (int)triangleIndices[j + 2];

				tris.push_back({
					{ 
						vertexIndices[v0], 
						vertexIndices[v1], 
						vertexIndices[v2] 
					},
					(int)i
				});
			}
		}

		return tris;
	}
}