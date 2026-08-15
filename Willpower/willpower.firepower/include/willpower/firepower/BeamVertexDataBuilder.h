#pragma once

#include <mpp/Colour.h>

#include "willpower/common/Exceptions.h"
#include "willpower/common/IndexedTriangleBatchRenderable.h"

#include "willpower/firepower/Platform.h"
#include "willpower/firepower/Beam.h"

namespace WP_NAMESPACE
{
	namespace firepower
	{

		struct ShardRenderData
		{
			int beamIndex;
			Vector2 nearVertices[2];
			Vector2 farVertices[2];
			float u[2], v[2];

			Beam::SectionOptions sectionOptions;
			int headOffset, bodyOffset, baseOffset, imageHeight;
		};

		template<typename PosType, typename TexType, typename ColType, int ColSize>
		struct BeamVertexDataBuilder
		{
			inline void setVertex(PosType* posBuffer, TexType* texBuffer, ColType* colBuffer, 
				PosType x, PosType y, TexType u, TexType v, mpp::Colour const& colour)
			{
				*(posBuffer + 0) = x;
				*(posBuffer + 1) = y;

				for (int i = 0; i < ColSize; ++i)
				{
					*(colBuffer + i) = (ColType)colour[i];
				}

				*(texBuffer + 0) = u;
				*(texBuffer + 1) = v;
			}

			int operator()(std::vector<ShardRenderData> const& shardRenderData, mpp::Colour const& colour, int* triangleCount, int* vertexCount, TriangleBatchRenderable* renderable)
			{
				*triangleCount = 0;
				*vertexCount = 0;

				int8_t* posBuffer = renderable->getPositionData();
				int8_t* texBuffer = renderable->getTexcoordData();
				int8_t* colBuffer = renderable->getColourData();

				int ps = renderable->getPositionStride();
				int cs = ps;
				int ts = renderable->getTexcoordStride();

				for (auto const& srd: shardRenderData)
				{
					switch (srd.sectionOptions)
					{
					case Beam::SectionOptions::Single:
						// First triangle
						setVertex((PosType*)posBuffer, (TexType*)texBuffer, (ColType*)colBuffer, (PosType)srd.nearVertices[0].x, (PosType)srd.nearVertices[0].y, (TexType)srd.u[0], (TexType)0.0f, colour);
						posBuffer += ps; colBuffer += cs; texBuffer += ts;

						setVertex((PosType*)posBuffer, (TexType*)texBuffer, (ColType*)colBuffer, (PosType)srd.nearVertices[1].x, (PosType)srd.nearVertices[1].y, (TexType)srd.u[1], (TexType)0.0f, colour);
						posBuffer += ps; colBuffer += cs; texBuffer += ts;

						setVertex((PosType*)posBuffer, (TexType*)texBuffer, (ColType*)colBuffer, (PosType)srd.farVertices[1].x, (PosType)srd.farVertices[1].y, (TexType)srd.u[1], (TexType)srd.v[1], colour);
						posBuffer += ps; colBuffer += cs; texBuffer += ts;

						// Second triangle
						setVertex((PosType*)posBuffer, (TexType*)texBuffer, (ColType*)colBuffer, (PosType)srd.farVertices[1].x, (PosType)srd.farVertices[1].y, (TexType)srd.u[1], (TexType)srd.v[1], colour);
						posBuffer += ps; colBuffer += cs; texBuffer += ts;

						setVertex((PosType*)posBuffer, (TexType*)texBuffer, (ColType*)colBuffer, (PosType)srd.farVertices[0].x, (PosType)srd.farVertices[0].y, (TexType)srd.u[0], (TexType)srd.v[1], colour);
						posBuffer += ps; colBuffer += cs; texBuffer += ts;

						setVertex((PosType*)posBuffer, (TexType*)texBuffer, (ColType*)colBuffer, (PosType)srd.nearVertices[0].x, (PosType)srd.nearVertices[0].y, (TexType)srd.u[0], (TexType)0.0f, colour);
						posBuffer += ps; colBuffer += cs; texBuffer += ts;

						*vertexCount += 6;
						*triangleCount += 2;
						break;

					case Beam::SectionOptions::Scale2:
						NOT_IMPLEMENTED_YET("scale2");
						break;
					case Beam::SectionOptions::Scale3:
						NOT_IMPLEMENTED_YET("scale3");
						break;
					case Beam::SectionOptions::Scale9:
						NOT_IMPLEMENTED_YET("scale9");
						break;
					default:
						NOT_IMPLEMENTED_YET("unknown scaling type");
					}
				}

				return *vertexCount * (ps + ts);
			}
		};

		template<typename PosType, typename TexType, int ColSize>
		struct BeamVertexDataBuilder<PosType, TexType, uint8_t, ColSize>
		{
			inline void setVertex(PosType* posBuffer, TexType* texBuffer, uint8_t* colBuffer,
				PosType x, PosType y, TexType u, TexType v, mpp::Colour const& colour)
			{
				*(posBuffer + 0) = x;
				*(posBuffer + 1) = y;

				for (int i = 0; i < ColSize; ++i)
				{
					*(colBuffer + i) = (uint8_t)(colour[i] * 255.0f);
				}

				*(texBuffer + 0) = u;
				*(texBuffer + 1) = v;
			}

			int operator()(std::vector<ShardRenderData> const& shardRenderData, mpp::Colour const& colour, int* triangleCount, int* vertexCount, TriangleBatchRenderable* renderable)
			{
				int renderUsage = 0;

				*triangleCount = 0;
				*vertexCount = 0;

				int8_t* posBuffer = (int8_t*)renderable->getPositionData();
				TexType* texBuffer = (TexType*)renderable->getTexcoordData();
				int8_t* colBuffer = (int8_t*)renderable->getColourData();

				int posStride = renderable->getPositionStride();
				int colStride = posStride;
				int texStride = renderable->getTexcoordStride() / sizeof(TexType);

				for (auto const& srd : shardRenderData)
				{
					switch (srd.sectionOptions)
					{
					case Beam::SectionOptions::Single:
						// First triangle
						setVertex((PosType*)posBuffer, texBuffer, (uint8_t*)colBuffer, (PosType)srd.nearVertices[0].x, (PosType)srd.nearVertices[0].y, (TexType)srd.u[0], (TexType)0.0f, colour);
						posBuffer += posStride;
						colBuffer += colStride;
						texBuffer += texStride;

						setVertex((PosType*)posBuffer, texBuffer, (uint8_t*)colBuffer, (PosType)srd.nearVertices[1].x, (PosType)srd.nearVertices[1].y, (TexType)srd.u[1], (TexType)0.0f, colour);
						posBuffer += posStride;
						colBuffer += colStride;
						texBuffer += texStride;

						setVertex((PosType*)posBuffer, texBuffer, (uint8_t*)colBuffer, (PosType)srd.farVertices[1].x, (PosType)srd.farVertices[1].y, (TexType)srd.u[1], (TexType)srd.v[1], colour);
						posBuffer += posStride;
						colBuffer += colStride;
						texBuffer += texStride;

						// Second triangle
						setVertex((PosType*)posBuffer, texBuffer, (uint8_t*)colBuffer, (PosType)srd.farVertices[1].x, (PosType)srd.farVertices[1].y, (TexType)srd.u[1], (TexType)srd.v[1], colour);
						posBuffer += posStride;
						colBuffer += colStride;
						texBuffer += texStride;

						setVertex((PosType*)posBuffer, texBuffer, (uint8_t*)colBuffer, (PosType)srd.farVertices[0].x, (PosType)srd.farVertices[0].y, (TexType)srd.u[0], (TexType)srd.v[1], colour);
						posBuffer += posStride;
						colBuffer += colStride;
						texBuffer += texStride;

						setVertex((PosType*)posBuffer, texBuffer, (uint8_t*)colBuffer, (PosType)srd.nearVertices[0].x, (PosType)srd.nearVertices[0].y, (TexType)srd.u[0], (TexType)0.0f, colour);
						posBuffer += posStride;
						colBuffer += colStride;
						texBuffer += texStride;

						*vertexCount += 6;
						*triangleCount += 2;
						renderUsage += 12 * sizeof(PosType);
						renderUsage += 6 * ColSize * sizeof(uint8_t);
						renderUsage += 12 * sizeof(TexType);
						break;

					case Beam::SectionOptions::Scale2:
						NOT_IMPLEMENTED_YET("scale2");
						break;
					case Beam::SectionOptions::Scale3:
						NOT_IMPLEMENTED_YET("scale3");
						break;
					case Beam::SectionOptions::Scale9:
						NOT_IMPLEMENTED_YET("scale9");
						break;
					default:
						NOT_IMPLEMENTED_YET("unknown scaling type");
					}
				}

				return renderUsage;
			}
		};

		template<typename PosType, typename TexType>
		struct BeamVertexDataBuilder<PosType, TexType, void, 0>
		{
			inline void setVertex(PosType* posBuffer, TexType* texBuffer, PosType x, PosType y, TexType u, TexType v)
			{
				*(posBuffer + 0) = x;
				*(posBuffer + 1) = y;

				*(texBuffer + 0) = u;
				*(texBuffer + 1) = v;
			}

			int operator()(std::vector<ShardRenderData> const& shardRenderData, mpp::Colour const& colour, int* triangleCount, int* vertexCount, TriangleBatchRenderable* renderable)
			{
				WP_UNUSED(colour);

				int renderUsage = 0;

				*triangleCount = 0;
				*vertexCount = 0;

				int8_t* posBuffer = (int8_t*)renderable->getPositionData();
				TexType* texBuffer = (TexType*)renderable->getTexcoordData();

				int posStride = renderable->getPositionStride();
				int texStride = renderable->getTexcoordStride() / sizeof(TexType);

				for (auto const& srd: shardRenderData)
				{
					switch (srd.sectionOptions)
					{
					case Beam::SectionOptions::Single:
						// First triangle
						setVertex((PosType*)posBuffer, texBuffer, (PosType)srd.nearVertices[0].x, (PosType)srd.nearVertices[0].y, (TexType)srd.u[0], (TexType)0.0f);
						posBuffer += posStride;
						texBuffer += texStride;

						setVertex((PosType*)posBuffer, texBuffer, (PosType)srd.nearVertices[1].x, (PosType)srd.nearVertices[1].y, (TexType)srd.u[1], (TexType)0.0f);
						posBuffer += posStride;
						texBuffer += texStride;

						setVertex((PosType*)posBuffer, texBuffer, (PosType)srd.farVertices[1].x, (PosType)srd.farVertices[1].y, (TexType)srd.u[1], (TexType)srd.v[1]);
						posBuffer += posStride;
						texBuffer += texStride;

						// Second triangle
						setVertex((PosType*)posBuffer, texBuffer, (PosType)srd.farVertices[1].x, (PosType)srd.farVertices[1].y, (TexType)srd.u[1], (TexType)srd.v[1]);
						posBuffer += posStride;
						texBuffer += texStride;

						setVertex((PosType*)posBuffer, texBuffer, (PosType)srd.farVertices[0].x, (PosType)srd.farVertices[0].y, (TexType)srd.u[0], (TexType)srd.v[1]);
						posBuffer += posStride;
						texBuffer += texStride;

						setVertex((PosType*)posBuffer, texBuffer, (uint8_t*)colBuffer, (PosType)srd.nearVertices[0].x, (PosType)srd.nearVertices[0].y, (TexType)srd.u[0], (TexType)0.0f, colour);
						posBuffer += posStride;
						texBuffer += texStride;

						*vertexCount += 6;
						*triangleCount += 2;
						renderUsage += 12 * sizeof(PosType);
						renderUsage += 12 * sizeof(TexType);
						break;

					case Beam::SectionOptions::Scale2:
						NOT_IMPLEMENTED_YET("scale2");
						break;
					case Beam::SectionOptions::Scale3:
						NOT_IMPLEMENTED_YET("scale3");
						break;
					case Beam::SectionOptions::Scale9:
						NOT_IMPLEMENTED_YET("scale9");
						break;
					default:
						NOT_IMPLEMENTED_YET("unknown scaling type");
					}
				}

				return renderUsage;
			}
		};

		template<typename PosType, typename ColType, int ColSize>
		struct BeamVertexDataBuilder<PosType, void, ColType, ColSize>
		{
			inline void setVertex(PosType* posBuffer, ColType* colBuffer,
				PosType x, PosType y, mpp::Colour const& colour)
			{
				*(posBuffer + 0) = x;
				*(posBuffer + 1) = y;

				for (int i = 0; i < ColSize; ++i)
				{
					*(colBuffer + i) = (ColType)colour[i];
				}
			}

			int operator()(std::vector<ShardRenderData> const& shardRenderData, mpp::Colour const& colour, int* triangleCount, int* vertexCount, TriangleBatchRenderable* renderable)
			{
				int renderUsage = 0;

				*triangleCount = 0;
				*vertexCount = 0;

				int8_t* posBuffer = (int8_t*)renderable->getPositionData();
				int8_t* colBuffer = (int8_t*)renderable->getColourData();

				int posStride = renderable->getPositionStride();
				int colStride = posStride;

				for (auto const& srd : shardRenderData)
				{
					switch (srd.sectionOptions)
					{
					case Beam::SectionOptions::Single:
						// First triangle
						setVertex((PosType*)posBuffer, (ColType*)colBuffer, (PosType)srd.nearVertices[0].x, (PosType)srd.nearVertices[0].y, colour);
						posBuffer += posStride;
						colBuffer += colStride;

						setVertex((PosType*)posBuffer, (ColType*)colBuffer, (PosType)srd.nearVertices[1].x, (PosType)srd.nearVertices[1].y, colour);
						posBuffer += posStride;
						colBuffer += colStride;

						setVertex((PosType*)posBuffer, (ColType*)colBuffer, (PosType)srd.farVertices[1].x, (PosType)srd.farVertices[1].y, colour);
						posBuffer += posStride;
						colBuffer += colStride;

						// Second triangle
						setVertex((PosType*)posBuffer, (ColType*)colBuffer, (PosType)srd.farVertices[1].x, (PosType)srd.farVertices[1].y, colour);
						posBuffer += posStride;
						colBuffer += colStride;

						setVertex((PosType*)posBuffer, (ColType*)colBuffer, (PosType)srd.farVertices[0].x, (PosType)srd.farVertices[0].y, colour);
						posBuffer += posStride;
						colBuffer += colStride;

						setVertex((PosType*)posBuffer, (ColType*)colBuffer, (PosType)srd.nearVertices[0].x, (PosType)srd.nearVertices[0].y, colour);
						posBuffer += posStride;
						colBuffer += colStride;

						*vertexCount += 6;
						*triangleCount += 2;
						renderUsage += 12 * sizeof(PosType);
						renderUsage += 6 * ColSize * sizeof(ColType);
						break;

					case Beam::SectionOptions::Scale2:
						NOT_IMPLEMENTED_YET("scale 2");
						break;
					case Beam::SectionOptions::Scale3:
						NOT_IMPLEMENTED_YET("scale 3");
						break;
					case Beam::SectionOptions::Scale9:
						NOT_IMPLEMENTED_YET("scale 9");
						break;
					default:
						NOT_IMPLEMENTED_YET("unknown scaling type");
					}
				}

				return renderUsage;
			}
		};

		template<typename PosType, int ColSize>
		struct BeamVertexDataBuilder<PosType, void, uint8_t, ColSize>
		{
			inline void setVertex(PosType* posBuffer, uint8_t* colBuffer,
				PosType x, PosType y, mpp::Colour const& colour)
			{
				*(posBuffer + 0) = x;
				*(posBuffer + 1) = y;

				for (int i = 0; i < ColSize; ++i)
				{
					*(colBuffer + i) = (uint8_t)(colour[i] * 255.0f);
				}
			}

			int operator()(std::vector<ShardRenderData> const& shardRenderData, mpp::Colour const& colour, int* triangleCount, int* vertexCount, TriangleBatchRenderable* renderable)
			{
				int renderUsage = 0;

				*triangleCount = 0;
				*vertexCount = 0;

				int8_t* posBuffer = (int8_t*)renderable->getPositionData();
				int8_t* colBuffer = (int8_t*)renderable->getColourData();

				int posStride = renderable->getPositionStride();
				int colStride = posStride;

				for (auto const& srd : shardRenderData)
				{
					switch (srd.sectionOptions)
					{
					case Beam::SectionOptions::Single:
						// First triangle
						setVertex((PosType*)posBuffer, (uint8_t*)colBuffer, (PosType)srd.nearVertices[0].x, (PosType)srd.nearVertices[0].y, colour);
						posBuffer += posStride;
						colBuffer += colStride;

						setVertex((PosType*)posBuffer, (uint8_t*)colBuffer, (PosType)srd.nearVertices[1].x, (PosType)srd.nearVertices[1].y, colour);
						posBuffer += posStride;
						colBuffer += colStride;

						setVertex((PosType*)posBuffer, (uint8_t*)colBuffer, (PosType)srd.farVertices[1].x, (PosType)srd.farVertices[1].y, colour);
						posBuffer += posStride;
						colBuffer += colStride;

						// Second triangle
						setVertex((PosType*)posBuffer, (uint8_t*)colBuffer, (PosType)srd.farVertices[1].x, (PosType)srd.farVertices[1].y, colour);
						posBuffer += posStride;
						colBuffer += colStride;

						setVertex((PosType*)posBuffer, (uint8_t*)colBuffer, (PosType)srd.farVertices[0].x, (PosType)srd.farVertices[0].y, colour);
						posBuffer += posStride;
						colBuffer += colStride;

						setVertex((PosType*)posBuffer, (uint8_t*)colBuffer, (PosType)srd.nearVertices[0].x, (PosType)srd.nearVertices[0].y, colour);
						posBuffer += posStride;
						colBuffer += colStride;

						*vertexCount += 6;
						*triangleCount += 2;
						renderUsage += 12 * sizeof(PosType);
						renderUsage += 6 * ColSize * sizeof(uint8_t);
						break;

					case Beam::SectionOptions::Scale2:
						NOT_IMPLEMENTED_YET("scale 2");
						break;
					case Beam::SectionOptions::Scale3:
						NOT_IMPLEMENTED_YET("scale 3");
						break;
					case Beam::SectionOptions::Scale9:
						NOT_IMPLEMENTED_YET("scale 9");
						break;
					default:
						NOT_IMPLEMENTED_YET("unknown scaling type");
					}
				}

				return renderUsage;
			}
		};

		template<typename PosType>
		struct BeamVertexDataBuilder<PosType, void, void, 0>
		{
			int operator()(std::vector<ShardRenderData> const& shardRenderData, mpp::Colour const& colour, int* triangleCount, int* vertexCount, TriangleBatchRenderable* renderable)
			{
				WP_UNUSED(colour);

				int renderUsage = 0;

				*triangleCount = 0;
				*vertexCount = 0;

				// Get pointers to buffers
				PosType* posBuffer = (PosType*)renderable->getPositionData();

				for (auto const& srd : shardRenderData)
				{
					switch (srd.sectionOptions)
					{
					case Beam::SectionOptions::Single:
						// First triangle
						*posBuffer++ = (PosType)srd.nearVertices[0].x;
						*posBuffer++ = (PosType)srd.nearVertices[0].y;

						*posBuffer++ = (PosType)srd.nearVertices[1].x;
						*posBuffer++ = (PosType)srd.nearVertices[1].y;

						*posBuffer++ = (PosType)srd.farVertices[1].x;
						*posBuffer++ = (PosType)srd.farVertices[1].y;

						// Second triangle
						*posBuffer++ = (PosType)srd.farVertices[1].x;
						*posBuffer++ = (PosType)srd.farVertices[1].y;

						*posBuffer++ = (PosType)srd.nearVertices[1].x;
						*posBuffer++ = (PosType)srd.nearVertices[1].y;

						*posBuffer++ = (PosType)srd.nearVertices[0].x;
						*posBuffer++ = (PosType)srd.nearVertices[0].y;

						*vertexCount += 6;
						*triangleCount += 2;
						renderUsage += 12 * sizeof(PosType);
						break;

					case Beam::SectionOptions::Scale2:
						NOT_IMPLEMENTED_YET("scale 2");
						break;
					case Beam::SectionOptions::Scale3:
						NOT_IMPLEMENTED_YET("scale 3");
						break;
					case Beam::SectionOptions::Scale9:
						NOT_IMPLEMENTED_YET("scale 9");
						break;
					default:
						NOT_IMPLEMENTED_YET("unknown scaling type");
					}
				}

				return renderUsage;
			}
		};

		template<>
		struct BeamVertexDataBuilder<float, void, void, 0>
		{
			int operator()(std::vector<ShardRenderData> const& shardRenderData, mpp::Colour const& colour, int* triangleCount, int* vertexCount, TriangleBatchRenderable* renderable)
			{
				WP_UNUSED(colour);

				int renderUsage = 0;

				*triangleCount = 0;
				*vertexCount = 0;

				// Get pointers to buffers
				float* posBuffer = (float*)renderable->getPositionData();

				for (auto const& srd : shardRenderData)
				{
					switch (srd.sectionOptions)
					{
					case Beam::SectionOptions::Single:
						// First triangle
						*posBuffer++ = (float)srd.nearVertices[0].x;
						*posBuffer++ = (float)srd.nearVertices[0].y;

						*posBuffer++ = (float)srd.nearVertices[1].x;
						*posBuffer++ = (float)srd.nearVertices[1].y;

						*posBuffer++ = (float)srd.farVertices[1].x;
						*posBuffer++ = (float)srd.farVertices[1].y;

						// Second triangle
						*posBuffer++ = (float)srd.farVertices[1].x;
						*posBuffer++ = (float)srd.farVertices[1].y;

						*posBuffer++ = (float)srd.nearVertices[1].x;
						*posBuffer++ = (float)srd.nearVertices[1].y;

						*posBuffer++ = (float)srd.nearVertices[0].x;
						*posBuffer++ = (float)srd.nearVertices[0].y;

						*vertexCount += 6;
						*triangleCount += 2;
						renderUsage += 12 * sizeof(float);
						break;

					case Beam::SectionOptions::Scale2:
						NOT_IMPLEMENTED_YET("scale 2");
						break;
					case Beam::SectionOptions::Scale3:
						NOT_IMPLEMENTED_YET("scale 3");
						break;
					case Beam::SectionOptions::Scale9:
						NOT_IMPLEMENTED_YET("scale 9");
						break;
					default:
						NOT_IMPLEMENTED_YET("unknown scaling type");
					}
				}

				return renderUsage;
			}
		};
	} // firepower
} // WP_NAMESPACE