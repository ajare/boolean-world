#pragma once

#include <string>
#include <set>
#include <vector>

#include <willpower/common/Vector2.h>

#include "core/World.h"

#include "Expr.h"

namespace floored
{

	class Document
	{
		std::shared_ptr<bw::core::World> mWorld;

		std::vector<bw::core::Clipper2Polygon> mPolygons;

		expr::PSLG mPSLG;

		std::vector<expr::Cycle> mCycles;
			
		std::vector<expr::PolygonNode> mHierarchy;

		std::vector<expr::Face> mFaces;

		std::vector<expr::FaceTriangle> mFaceTriangles;

		static Document* msInstance;

	private:

		void buildExpr();

	public:

		Document();

		virtual ~Document();

		static Document* instance();

		bool isActive() const;

		std::shared_ptr<bw::core::World> getWorld();

		std::vector<bw::core::Clipper2Polygon> const& getPolygons();

		expr::PSLG const& getPSLG() const;

		std::vector<expr::Cycle> const& getCycles() const;

		std::vector<expr::PolygonNode> const& getHierarchy() const;

		std::vector<expr::Face> const& getFaces() const;

		std::vector<expr::FaceTriangle> const& getFaceTriangles() const;

		bool openWorld(std::string const& filepath);
	};

} // floored
