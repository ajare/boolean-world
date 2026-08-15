#include "willpower/viz/GeometryMeshRenderParams.h"

namespace WP_NAMESPACE
{
	namespace viz
	{
		using namespace wp;
		using namespace std;

		GeometryMeshRenderParams::GeometryMeshRenderParams(shared_ptr<mpp::ModelRenderParams> params)
			: RenderParams()
			, mParams(params)
			, mVertexSize(6)
			, mVertexColour(mpp::Colour::White)
			, mRenderVertices(false)
			, mTriangulationColour(mpp::Colour(0.7f, 0.7f, 0.4f))
			, mPolygonEdgeColour(mpp::Colour(0.5f, 0.5f, 0.2f))
			, mBorderColour(mpp::Colour(1.0f, 0.0f, 0.0f))
			, mRenderTriangulation(true)
			, mRenderPolygonEdges(false)
			, mRenderBorder(false)
			, mRenderEdgeNormals(false)
			, mRenderEdgeDirections(false)
			, mBackgroundColour(mpp::Colour(0.5f, 0.5f, 1.0f))
			, mRenderBackground(true)
		{
			// Set initial values
			mParams->setMeshPointSize("Vertices", mVertexSize);

			mBackgroundUniforms = make_shared<mpp::UniformCollection>();
			mPolygonEdgeUniforms = make_shared<mpp::UniformCollection>();
			mBorderUniforms = make_shared<mpp::UniformCollection>();
			mTriangulationUniforms = make_shared<mpp::UniformCollection>();
			mVertexUniforms = make_shared<mpp::UniformCollection>();
			mPolygonEdgeNormalUniforms = make_shared<mpp::UniformCollection>();
			mPolygonEdgeDirectionUniforms = make_shared<mpp::UniformCollection>();

			mBackgroundUniforms->setUniform("DIFFUSE", glm::vec4(mBackgroundColour.red, mBackgroundColour.green, mBackgroundColour.blue, mBackgroundColour.alpha));
			mPolygonEdgeUniforms->setUniform("DIFFUSE", glm::vec4(mPolygonEdgeColour.red, mPolygonEdgeColour.green, mPolygonEdgeColour.blue, mPolygonEdgeColour.alpha));
			mBorderUniforms->setUniform("DIFFUSE", glm::vec4(mBorderColour.red, mBorderColour.green, mBorderColour.blue, mBorderColour.alpha));
			mTriangulationUniforms->setUniform("DIFFUSE", glm::vec4(mTriangulationColour.red, mTriangulationColour.green, mTriangulationColour.blue, mTriangulationColour.alpha));
			mVertexUniforms->setUniform("DIFFUSE", glm::vec4(mVertexColour.red, mVertexColour.green, mVertexColour.blue, mVertexColour.alpha));
			mPolygonEdgeNormalUniforms->setUniform("DIFFUSE", glm::vec4(mPolygonEdgeColour.red, mPolygonEdgeColour.green, mPolygonEdgeColour.blue, mPolygonEdgeColour.alpha));
			mPolygonEdgeDirectionUniforms->setUniform("DIFFUSE", glm::vec4(mPolygonEdgeColour.red, mPolygonEdgeColour.green, mPolygonEdgeColour.blue, mPolygonEdgeColour.alpha));

			mParams->setMeshUniforms("PolygonEdges", mPolygonEdgeUniforms);
			mParams->setMeshUniforms("Border", mBorderUniforms);
			mParams->setMeshUniforms("Triangulation", mTriangulationUniforms);
			mParams->setMeshUniforms("Vertices", mVertexUniforms);
			mParams->setMeshUniforms("PolygonEdgeNormals", mPolygonEdgeNormalUniforms);
			mParams->setMeshUniforms("PolygonEdgeDirections", mPolygonEdgeDirectionUniforms);
		}

		void GeometryMeshRenderParams::setVertexSize(float size)
		{
			mVertexSize = size;
			mParams->setMeshPointSize("Vertices", mVertexSize);
		}

		float GeometryMeshRenderParams::getVertexSize() const
		{
			return mVertexSize;
		}

		void GeometryMeshRenderParams::setVertexColour(mpp::Colour const& colour)
		{
			mVertexColour = colour;
			mVertexUniforms->updateUniform("DIFFUSE", glm::vec4(mVertexColour.red, mVertexColour.green, mVertexColour.blue, mVertexColour.alpha));
		}

		mpp::Colour const& GeometryMeshRenderParams::getVertexColour() const
		{
			return mVertexColour;
		}

		void GeometryMeshRenderParams::setRenderVertices(bool render)
		{
			mRenderVertices = render;
		}

		bool GeometryMeshRenderParams::getRenderVertices() const
		{
			return mRenderVertices;
		}

		void GeometryMeshRenderParams::setTriangulationColour(mpp::Colour const& colour)
		{
			mTriangulationColour = colour;
			mTriangulationUniforms->updateUniform("DIFFUSE", glm::vec4(mTriangulationColour.red, mTriangulationColour.green, mTriangulationColour.blue, mTriangulationColour.alpha));
		}

		mpp::Colour const& GeometryMeshRenderParams::getTriangulationColour() const
		{
			return mTriangulationColour;
		}

		void GeometryMeshRenderParams::setPolygonEdgeColour(mpp::Colour const& colour)
		{
			mPolygonEdgeColour = colour;
			mPolygonEdgeUniforms->updateUniform("DIFFUSE", glm::vec4(mPolygonEdgeColour.red, mPolygonEdgeColour.green, mPolygonEdgeColour.blue, mPolygonEdgeColour.alpha));
		}

		mpp::Colour const& GeometryMeshRenderParams::getPolygonEdgeColour() const
		{
			return mPolygonEdgeColour;
		}

		void GeometryMeshRenderParams::setBorderColour(mpp::Colour const& colour)
		{
			mBorderColour = colour;
			mBorderUniforms->updateUniform("DIFFUSE", glm::vec4(mBorderColour.red, mBorderColour.green, mBorderColour.blue, mBorderColour.alpha));
		}

		mpp::Colour const& GeometryMeshRenderParams::getBorderColour() const
		{
			return mBorderColour;
		}

		void GeometryMeshRenderParams::setRenderTriangulation(bool render)
		{
			mRenderTriangulation = render;
		}

		bool GeometryMeshRenderParams::getRenderTriangulation() const
		{
			return mRenderTriangulation;
		}

		void GeometryMeshRenderParams::setRenderPolygonEdges(bool render)
		{
			mRenderPolygonEdges = render;
		}

		bool GeometryMeshRenderParams::getRenderPolygonEdges() const
		{
			return mRenderPolygonEdges;
		}

		void GeometryMeshRenderParams::setRenderBorder(bool render)
		{
			mRenderBorder = render;
		}

		bool GeometryMeshRenderParams::getRenderBorder() const
		{
			return mRenderBorder;
		}

		void GeometryMeshRenderParams::setRenderEdgeNormals(bool render)
		{
			mRenderEdgeNormals = render;
		}

		bool GeometryMeshRenderParams::getRenderEdgeNormals() const
		{
			return mRenderEdgeNormals;
		}

		void GeometryMeshRenderParams::setRenderEdgeDirections(bool render)
		{
			mRenderEdgeDirections = render;
		}

		bool GeometryMeshRenderParams::getRenderEdgeDirections() const
		{
			return mRenderEdgeDirections;
		}

		void GeometryMeshRenderParams::setBackgroundUniforms(string const& name)
		{
			mParams->setMeshUniforms(name, mBackgroundUniforms);
		}

		void GeometryMeshRenderParams::setBackgroundColour(mpp::Colour const& colour)
		{
			mBackgroundColour = colour;
			mBackgroundUniforms->updateUniform("DIFFUSE", glm::vec4(mBackgroundColour.red, mBackgroundColour.green, mBackgroundColour.blue, mBackgroundColour.alpha));
		}

		mpp::Colour const& GeometryMeshRenderParams::getBackgroundColour() const
		{
			return mBackgroundColour;
		}

		void GeometryMeshRenderParams::setRenderBackground(bool render)
		{
			mRenderBackground = render;
		}

		bool GeometryMeshRenderParams::getRenderBackground() const
		{
			return mRenderBackground;
		}

		float GeometryMeshRenderParams::getGridPadding() const
		{
			return (mVertexSize + 0.5f) * 2;
		}

	} // viz
} // WP_NAMESPACE

