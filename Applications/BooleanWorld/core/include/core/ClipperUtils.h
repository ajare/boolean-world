#pragma once

#include <vector>

#include <clipper2/clipper.h>

#include "core/Platform.h"
#include "core/Clipper2Polygon.h"
#include "core/Vertex.h"
#include "core/Clipper.h"
#include "core/WorldVertexData.h"


namespace bw
{
	namespace core
	{

		class BW_API ClipperUtils
		{
		public:

			static void interpolatePathVertices(Clipper2Lib::Path64& path, PrimitivePropertySet const& clipProperties, std::vector<WorldVertexData>& vertexData);

			static void traverseNonHole(Clipper2Lib::PolyPath64 const* polyPath, std::vector<Clipper2Polygon>& outPaths, bool interpolateVertices, PrimitivePropertySet const& clipProperties, std::vector<WorldVertexData>& vertexData);

			static void traverseHole(Clipper2Lib::PolyPath64 const* polyPath, std::vector<Clipper2Polygon>& outPaths, bool interpolateVertices, PrimitivePropertySet const& clipProperties, std::vector<WorldVertexData>& vertexData);

			static void traverseTree(Clipper2Lib::PolyTree64 const* polyTree, std::vector<Clipper2Polygon>& outPaths, bool interpolateVertices, PrimitivePropertySet const& clipProperties, std::vector<WorldVertexData>& vertexData);

			static std::vector<ClippedPolygon> convertClipper2PolygonsToClippedPolygons(std::vector<Clipper2Polygon> const& polygons, uint32_t* numVerticesGenerated);

			static std::vector<Clipper2Polygon> convertPathsToClippedPolygons(Clipper2Lib::Paths64 const& path);

			static std::vector<Clipper2Polygon> convertPrimitiveToClipperPolygons(Primitive const* primitive);

			static Clipper2Lib::Paths64 convertComplexPolygonsToPath(std::vector<ComplexPolygon> const& complexPolygons);

			static Clipper2Lib::Paths64 convertComplexPolygonsToPath(Primitive const* primitive);

			static Clipper2Lib::Paths64 convertClippedPolygonsToPath(std::vector<ClippedPolygon> const& polygons);

			static Clipper2Lib::Paths64 convertClipper2PolygonsToPath(std::vector<Clipper2Polygon> const& polygons);

			static std::vector<ComplexPolygon> convertClippedToComplexPolygons(std::vector<ClippedPolygon> const& clippedPolygons, wp::BoundingBox* bounds);

			static bool arePolygonsRotation(Clipper2Lib::Path64 const& a, Clipper2Lib::Path64 const& b);

			static void canonicalisePolygon(Clipper2Lib::Path64& polygon);

		};

	} // core
} // bw