#pragma once

#include <mpp/Colour.h>
#include <mpp/ModelRenderParams.h>

#include "willpower/viz/Platform.h"
#include "willpower/viz/RenderParams.h"

namespace WP_NAMESPACE
{
	namespace viz
	{
		class GeometryMeshRenderer;

		class WP_VIZ_API GeometryMeshRenderParams : public RenderParams
		{
			friend class GeometryMeshRenderer;

		private:

			std::shared_ptr<mpp::ModelRenderParams> mParams;

			std::shared_ptr<mpp::UniformCollection> mBackgroundUniforms, 
				mPolygonEdgeUniforms, 
				mBorderUniforms, 
				mTriangulationUniforms,
				mVertexUniforms,
				mPolygonEdgeNormalUniforms,
				mPolygonEdgeDirectionUniforms;

			// Vertex settings
			float mVertexSize;

			mpp::Colour mVertexColour;

			bool mRenderVertices;

			// Edge settings
			mpp::Colour mTriangulationColour, mPolygonEdgeColour, mBorderColour;

			bool mRenderTriangulation, mRenderPolygonEdges, mRenderBorder, mRenderEdgeNormals, mRenderEdgeDirections;

			// Background settings
			mpp::Colour mBackgroundColour;

			bool mRenderBackground;

		protected:

			explicit GeometryMeshRenderParams(std::shared_ptr<mpp::ModelRenderParams> params);

		public:

			// Vertices
			void setVertexSize(float size);

			float getVertexSize() const;

			void setVertexColour(mpp::Colour const& colour);

			mpp::Colour const& getVertexColour() const;

			void setRenderVertices(bool render);

			bool getRenderVertices() const;

			// Edges
			void setTriangulationColour(mpp::Colour const& colour);

			mpp::Colour const& getTriangulationColour() const;

			void setPolygonEdgeColour(mpp::Colour const& colour);

			mpp::Colour const& getPolygonEdgeColour() const;

			void setBorderColour(mpp::Colour const& colour);

			mpp::Colour const& getBorderColour() const;

			void setRenderTriangulation(bool render);

			bool getRenderTriangulation() const;

			void setRenderPolygonEdges(bool render);

			bool getRenderPolygonEdges() const;

			void setRenderBorder(bool render);

			bool getRenderBorder() const;

			void setRenderEdgeNormals(bool render);

			bool getRenderEdgeNormals() const;

			void setRenderEdgeDirections(bool render);

			bool getRenderEdgeDirections() const;

			// Background
			void setBackgroundUniforms(std::string const& name);

			void setBackgroundColour(mpp::Colour const& colour);

			mpp::Colour const& getBackgroundColour() const;

			void setRenderBackground(bool render);

			bool getRenderBackground() const;

			float getGridPadding() const;

		};

	} // viz
} // WP_NAMESPACE

