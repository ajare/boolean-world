#pragma once

#include <mpp/Colour.h>

#include "willpower/common/IndexedTriangleBatchRenderable.h"

#include "willpower/firepower/Platform.h"
#include "willpower/firepower/Bomb.h"

namespace WP_NAMESPACE
{
	namespace firepower
	{

		struct ArcRenderData
		{
			int bombIndex;
			float angle;
			float size;
			int numSegments;
		};

		template<typename PosType, typename TexType, typename ColType, int ColSize>
		struct BombVertexDataBuilder
		{
			int operator()(std::vector<ArcRenderData> const& arcRenderData, std::vector<Bomb> const& bombs, float scale, mpp::Colour const& colour, int* triangleCount, int* vertexCount, IndexedTriangleBatchRenderable* renderable)
			{
				WP_UNUSED(scale);

				int renderUsage = 0;

				*triangleCount = 0;
				*vertexCount = 0;

				// Get pointers to buffers
				PosType* posPtr;
				ColType* colPtr;

				int8_t* posBuffer = (int8_t*)renderable->getPositionData();
				TexType* texBuffer = (TexType*)renderable->getTexcoordData();
				int8_t* colBuffer = (int8_t*)renderable->getColourData();
				uint32_t* indexBuffer = (uint32_t*)renderable->getIndexData();

				int indexBase = 0;
				for (auto const& ard: arcRenderData)
				{
					auto const& bomb = bombs[ard.bombIndex];

					Vector2 const& pos = bomb.getPosition();
					float radius = bomb.getRadius();
					float depth = bomb.getDepth();

					float angle = ard.angle;
					float dAngle = ard.size / ard.numSegments;

					for (int i = 0; i <= ard.numSegments; ++i)
					{
						// Position
						auto d1 = Vector2(0, 1).rotatedClockwiseCopy(angle);

						auto s0 = pos + d1 * max(0.0f, radius - depth);
						auto s1 = pos + d1 * radius;

						// Near vertex
						posPtr = (PosType*)posBuffer;
						*(posPtr + 0) = (PosType)s0.x;
						*(posPtr + 1) = (PosType)s0.y;
						posBuffer += renderable->getPositionStride();

						colPtr = (ColType*)colBuffer;
						for (int j = 0; j < ColSize; ++j)
						{
							*(colPtr + j) = (ColType)colour[j];
						}
						colBuffer += renderable->getPositionStride();

						*texBuffer++ = (TexType)(angle / 360.0f);
						*texBuffer++ = (TexType)0.0f;

						// Far vertex
						posPtr = (PosType*)posBuffer;
						*(posPtr + 0) = (PosType)s1.x;
						*(posPtr + 1) = (PosType)s1.y;
						posBuffer += renderable->getPositionStride();

						colPtr = (ColType*)colBuffer;
						for (int j = 0; j < ColSize; ++j)
						{
							*(colPtr + j) = (ColType)colour[j];
						}
						colBuffer += renderable->getPositionStride();

						*texBuffer++ = (TexType)(angle / 360.0f);
						*texBuffer++ = (TexType)1.0f;

						// Update stats
						*vertexCount += 2;
						renderUsage += 4 * sizeof(PosType);
						renderUsage += 2 * ColSize * sizeof(ColType);
						renderUsage += 4 * sizeof(TexType);

						// Indices
						if (i > 0)
						{
							int indexBytes = renderable->getIndexWidth() / 8;
							if (indexBytes == 2)
							{
								*indexBuffer++ = (indexBase + 0) + ((indexBase + 1) << 16);
								*indexBuffer++ = (indexBase + 2) + ((indexBase + 1) << 16);
								*indexBuffer++ = (indexBase + 3) + ((indexBase + 2) << 16);
							}
							else if (indexBytes == 4)
							{
								*indexBuffer++ = indexBase + 0;
								*indexBuffer++ = indexBase + 1;
								*indexBuffer++ = indexBase + 2;

								*indexBuffer++ = indexBase + 1;
								*indexBuffer++ = indexBase + 3;
								*indexBuffer++ = indexBase + 2;
							}

							indexBase += 2;

							// Update stats
							*triangleCount += 2;
						}

						// Next angle
						angle += dAngle;
					}

					indexBase += 2;
				}

				return renderUsage;
			}
		};

		template<typename PosType, typename TexType, int ColSize>
		struct BombVertexDataBuilder<PosType, TexType, uint8_t, ColSize>
		{
			int operator()(std::vector<ArcRenderData> const& arcRenderData, std::vector<Bomb> const& bombs, float scale, mpp::Colour const& colour, int* triangleCount, int* vertexCount, IndexedTriangleBatchRenderable* renderable)
			{
				WP_UNUSED(scale);

				int renderUsage = 0;

				*triangleCount = 0;
				*vertexCount = 0;

				// Get pointers to buffers
				PosType* posPtr;
				uint8_t* colPtr;

				int8_t* posBuffer = (int8_t*)renderable->getPositionData();
				TexType* texBuffer = (TexType*)renderable->getTexcoordData();
				int8_t* colBuffer = (int8_t*)renderable->getColourData();
				uint32_t* indexBuffer = (uint32_t*)renderable->getIndexData();

				int indexBase = 0;
				for (auto const& ard: arcRenderData)
				{
					auto const& bomb = bombs[ard.bombIndex];

					Vector2 const& pos = bomb.getPosition();
					float radius = bomb.getRadius();
					float depth = bomb.getDepth();

					float angle = ard.angle;
					float dAngle = ard.size / ard.numSegments;

					for (int i = 0; i <= ard.numSegments; ++i)
					{
						// Position
						auto d1 = Vector2(0, 1).rotatedClockwiseCopy(angle);

						auto s0 = pos + d1 * max(0.0f, radius - depth);
						auto s1 = pos + d1 * radius;

						// Near vertex
						posPtr = (PosType*)posBuffer;
						*(posPtr + 0) = (PosType)s0.x;
						*(posPtr + 1) = (PosType)s0.y;
						posBuffer += renderable->getPositionStride();

						colPtr = (uint8_t*)colBuffer;
						for (int j = 0; j < ColSize; ++j)
						{
							*(colPtr + j) = (uint8_t)(colour[j] * 255.0f);
						}
						colBuffer += renderable->getPositionStride();

						*texBuffer++ = (TexType)(angle / 360.0f);
						*texBuffer++ = (TexType)0.0f;

						// Far vertex
						posPtr = (PosType*)posBuffer;
						*(posPtr + 0) = (PosType)s1.x;
						*(posPtr + 1) = (PosType)s1.y;
						posBuffer += renderable->getPositionStride();

						colPtr = (uint8_t*)colBuffer;
						for (int j = 0; j < ColSize; ++j)
						{
							*(colPtr + j) = (uint8_t)(colour[j] * 255.0f);
						}
						colBuffer += renderable->getPositionStride();

						*texBuffer++ = (TexType)(angle / 360.0f);
						*texBuffer++ = (TexType)1.0f;

						// Update stats
						*vertexCount += 2;
						renderUsage += 4 * sizeof(PosType);
						renderUsage += 2 * ColSize * sizeof(uint8_t);
						renderUsage += 4 * sizeof(TexType);

						// Indices
						if (i > 0)
						{
							int indexBytes = renderable->getIndexWidth() / 8;
							if (indexBytes == 2)
							{
								*indexBuffer++ = (indexBase + 0) + ((indexBase + 1) << 16);
								*indexBuffer++ = (indexBase + 2) + ((indexBase + 1) << 16);
								*indexBuffer++ = (indexBase + 3) + ((indexBase + 2) << 16);
							}
							else if (indexBytes == 4)
							{
								*indexBuffer++ = indexBase + 0;
								*indexBuffer++ = indexBase + 1;
								*indexBuffer++ = indexBase + 2;

								*indexBuffer++ = indexBase + 1;
								*indexBuffer++ = indexBase + 3;
								*indexBuffer++ = indexBase + 2;
							}

							indexBase += 2;

							// Update stats
							*triangleCount += 2;
						}

						// Next angle
						angle += dAngle;
					}

					indexBase += 2;
				}

				return renderUsage;
			}
		};

		template<typename PosType, typename TexType>
		struct BombVertexDataBuilder<PosType, TexType, void, 0>
		{
			int operator()(std::vector<ArcRenderData> const& arcRenderData, std::vector<Bomb> const& bombs, float scale, mpp::Colour const& colour, int* triangleCount, int* vertexCount, IndexedTriangleBatchRenderable* renderable)
			{
				WP_UNUSED(scale);
				WP_UNUSED(colour);

				int renderUsage = 0;

				*triangleCount = 0;
				*vertexCount = 0;

				// Get pointers to buffers
				PosType* posBuffer = (PosType*)renderable->getPositionData();
				TexType* texBuffer = (TexType*)renderable->getTexcoordData();
				uint32_t* indexBuffer = (uint32_t*)renderable->getIndexData();

				int indexBase = 0;
				for (auto const& ard : arcRenderData)
				{
					auto const& bomb = bombs[ard.bombIndex];

					Vector2 const& pos = bomb.getPosition();
					float radius = bomb.getRadius();
					float depth = bomb.getDepth();

					float angle = ard.angle;
					float dAngle = ard.size / ard.numSegments;

					for (int i = 0; i <= ard.numSegments; ++i)
					{
						// Position
						auto d1 = Vector2(0, 1).rotatedClockwiseCopy(angle);

						auto s0 = pos + d1 * max(0.0f, radius - depth);
						auto s1 = pos + d1 * radius;

						// Near vertex
						*posBuffer++ = (PosType)s0.x;
						*posBuffer++ = (PosType)s0.y;
						
						*texBuffer++ = (TexType)(angle / 360.0f);
						*texBuffer++ = (TexType)0.0f;

						// Far vertex
						*posBuffer++ = (PosType)s1.x;
						*posBuffer++ = (PosType)s1.y;

						*texBuffer++ = (TexType)(angle / 360.0f);
						*texBuffer++ = (TexType)1.0f;

						// Update stats
						*vertexCount += 2;
						renderUsage += 4 * sizeof(PosType);
						renderUsage += 4 * sizeof(TexType);

						// Indices
						if (i > 0)
						{
							int indexBytes = renderable->getIndexWidth() / 8;
							if (indexBytes == 2)
							{
								*indexBuffer++ = (indexBase + 0) + ((indexBase + 1) << 16);
								*indexBuffer++ = (indexBase + 2) + ((indexBase + 1) << 16);
								*indexBuffer++ = (indexBase + 3) + ((indexBase + 2) << 16);
							}
							else if (indexBytes == 4)
							{
								*indexBuffer++ = indexBase + 0;
								*indexBuffer++ = indexBase + 1;
								*indexBuffer++ = indexBase + 2;

								*indexBuffer++ = indexBase + 1;
								*indexBuffer++ = indexBase + 3;
								*indexBuffer++ = indexBase + 2;
							}

							indexBase += 2;

							// Update stats
							*triangleCount += 2;
						}

						// Next angle
						angle += dAngle;
					}

					indexBase += 2;
				}

				return renderUsage;
			}
		};

		template<typename PosType, typename ColType, int ColSize>
		struct BombVertexDataBuilder<PosType, void, ColType, ColSize>
		{
			int operator()(std::vector<ArcRenderData> const& arcRenderData, std::vector<Bomb> const& bombs, float scale, mpp::Colour const& colour, int* triangleCount, int* vertexCount, IndexedTriangleBatchRenderable* renderable)
			{
				WP_UNUSED(scale);

				int renderUsage = 0;

				*triangleCount = 0;
				*vertexCount = 0;

				// Get pointers to buffers
				PosType* posPtr;
				ColType* colPtr;

				int8_t* posBuffer = (int8_t*)renderable->getPositionData();
				int8_t* colBuffer = (int8_t*)renderable->getColourData();
				uint32_t* indexBuffer = (uint32_t*)renderable->getIndexData();

				int indexBase = 0;
				for (auto const& ard : arcRenderData)
				{
					auto const& bomb = bombs[ard.bombIndex];

					Vector2 const& pos = bomb.getPosition();
					float radius = bomb.getRadius();
					float depth = bomb.getDepth();

					float angle = ard.angle;
					float dAngle = ard.size / ard.numSegments;

					for (int i = 0; i <= ard.numSegments; ++i)
					{
						// Position
						auto d1 = Vector2(0, 1).rotatedClockwiseCopy(angle);

						auto s0 = pos + d1 * max(0.0f, radius - depth);
						auto s1 = pos + d1 * radius;

						// Near vertex
						posPtr = (PosType*)posBuffer;
						*(posPtr + 0) = (PosType)s0.x;
						*(posPtr + 1) = (PosType)s0.y;
						posBuffer += renderable->getPositionStride();

						colPtr = (ColType*)colBuffer;
						for (int j = 0; j < ColSize; ++j)
						{
							*(colPtr + j) = (ColType)colour[j];
						}
						colBuffer += renderable->getPositionStride();

						// Far vertex
						posPtr = (PosType*)posBuffer;
						*(posPtr + 0) = (PosType)s1.x;
						*(posPtr + 1) = (PosType)s1.y;
						posBuffer += renderable->getPositionStride();

						colPtr = (ColType*)colBuffer;
						for (int j = 0; j < ColSize; ++j)
						{
							*(colPtr + j) = (ColType)colour[j];
						}
						colBuffer += renderable->getPositionStride();
						
						// Update stats
						*vertexCount += 2;
						renderUsage += 4 * sizeof(PosType);
						renderUsage += 2 * ColSize * sizeof(ColType);

						// Indices
						if (i > 0)
						{
							int indexBytes = renderable->getIndexWidth() / 8;
							if (indexBytes == 2)
							{
								*indexBuffer++ = (indexBase + 0) + ((indexBase + 1) << 16);
								*indexBuffer++ = (indexBase + 2) + ((indexBase + 1) << 16);
								*indexBuffer++ = (indexBase + 3) + ((indexBase + 2) << 16);
							}
							else if (indexBytes == 4)
							{
								*indexBuffer++ = indexBase + 0;
								*indexBuffer++ = indexBase + 1;
								*indexBuffer++ = indexBase + 2;

								*indexBuffer++ = indexBase + 1;
								*indexBuffer++ = indexBase + 3;
								*indexBuffer++ = indexBase + 2;
							}

							indexBase += 2;

							// Update stats
							*triangleCount += 2;
						}

						// Next angle
						angle += dAngle;
					}

					indexBase += 2;
				}

				return renderUsage;
			}
		};

		template<typename PosType, int ColSize>
		struct BombVertexDataBuilder<PosType, void, uint8_t, ColSize>
		{
			int operator()(std::vector<ArcRenderData> const& arcRenderData, std::vector<Bomb> const& bombs, float scale, mpp::Colour const& colour, int* triangleCount, int* vertexCount, IndexedTriangleBatchRenderable* renderable)
			{
				WP_UNUSED(scale);

				int renderUsage = 0;

				*triangleCount = 0;
				*vertexCount = 0;

				// Get pointers to buffers
				// Get pointers to buffers
				PosType* posPtr;
				uint8_t* colPtr;

				int8_t* posBuffer = (int8_t*)renderable->getPositionData();
				int8_t* colBuffer = (int8_t*)renderable->getColourData();
				uint32_t* indexBuffer = (uint32_t*)renderable->getIndexData();

				int indexBase = 0;
				for (auto const& ard : arcRenderData)
				{
					auto const& bomb = bombs[ard.bombIndex];

					Vector2 const& pos = bomb.getPosition();
					float radius = bomb.getRadius();
					float depth = bomb.getDepth();

					float angle = ard.angle;
					float dAngle = ard.size / ard.numSegments;

					for (int i = 0; i <= ard.numSegments; ++i)
					{
						// Position
						auto d1 = Vector2(0, 1).rotatedClockwiseCopy(angle);

						auto s0 = pos + d1 * max(0.0f, radius - depth);
						auto s1 = pos + d1 * radius;

						// Near vertex
						posPtr = (PosType*)posBuffer;
						*(posPtr + 0) = (PosType)s0.x;
						*(posPtr + 1) = (PosType)s0.y;
						posBuffer += renderable->getPositionStride();

						colPtr = (uint8_t*)colBuffer;
						for (int j = 0; j < ColSize; ++j)
						{
							*(colPtr + j) = (uint8_t)(255 * colour[j]);
						}
						colBuffer += renderable->getPositionStride();

						// Far vertex
						posPtr = (PosType*)posBuffer;
						*(posPtr + 0) = (PosType)s1.x;
						*(posPtr + 1) = (PosType)s1.y;
						posBuffer += renderable->getPositionStride();

						colPtr = (uint8_t*)colBuffer;
						for (int j = 0; j < ColSize; ++j)
						{
							*(colPtr + j) = (uint8_t)(255 * colour[j]);
						}
						colBuffer += renderable->getPositionStride();

						// Update stats
						*vertexCount += 2;
						renderUsage += 4 * sizeof(PosType);
						renderUsage += 2 * ColSize * sizeof(uint8_t);

						// Indices
						if (i > 0)
						{
							int indexBytes = renderable->getIndexWidth() / 8;
							if (indexBytes == 2)
							{
								*indexBuffer++ = (indexBase + 0) + ((indexBase + 1) << 16);
								*indexBuffer++ = (indexBase + 2) + ((indexBase + 1) << 16);
								*indexBuffer++ = (indexBase + 3) + ((indexBase + 2) << 16);
							}
							else if (indexBytes == 4)
							{
								*indexBuffer++ = indexBase + 0;
								*indexBuffer++ = indexBase + 1;
								*indexBuffer++ = indexBase + 2;

								*indexBuffer++ = indexBase + 1;
								*indexBuffer++ = indexBase + 3;
								*indexBuffer++ = indexBase + 2;
							}

							indexBase += 2;

							// Update stats
							*triangleCount += 2;
						}

						// Next angle
						angle += dAngle;
					}

					indexBase += 2;
				}

				return renderUsage;
			}
		};

		template<typename PosType>
		struct BombVertexDataBuilder<PosType, void, void, 0>
		{
			int operator()(std::vector<ArcRenderData> const& arcRenderData, std::vector<Bomb> const& bombs, float scale, mpp::Colour const& colour, int* triangleCount, int* vertexCount, IndexedTriangleBatchRenderable* renderable)
			{
				WP_UNUSED(scale);
				WP_UNUSED(colour);

				int renderUsage = 0;

				*triangleCount = 0;
				*vertexCount = 0;

				// Get pointers to buffers
				PosType* posBuffer = (PosType*)renderable->getPositionData();
				uint32_t* indexBuffer = (uint32_t*)renderable->getIndexData();

				int indexBase = 0;
				for (auto const& ard : arcRenderData)
				{
					auto const& bomb = bombs[ard.bombIndex];

					Vector2 const& pos = bomb.getPosition();
					float radius = bomb.getRadius();
					float depth = bomb.getDepth();

					float angle = ard.angle;
					float dAngle = ard.size / ard.numSegments;

					for (int i = 0; i <= ard.numSegments; ++i)
					{
						// Position
						auto d1 = Vector2(0, 1).rotatedClockwiseCopy(angle);

						auto s0 = pos + d1 * max(0.0f, radius - depth);
						auto s1 = pos + d1 * radius;

						// Near vertex
						*posBuffer++ = (PosType)s0.x;
						*posBuffer++ = (PosType)s0.y;

						// Far vertex
						*posBuffer++ = (PosType)s1.x;
						*posBuffer++ = (PosType)s1.y;

						// Update stats
						*vertexCount += 2;
						renderUsage += 4 * sizeof(PosType);

						// Indices
						if (i > 0)
						{
							int indexBytes = renderable->getIndexWidth() / 8;
							if (indexBytes == 2)
							{
								*indexBuffer++ = (indexBase + 0) + ((indexBase + 1) << 16);
								*indexBuffer++ = (indexBase + 2) + ((indexBase + 1) << 16);
								*indexBuffer++ = (indexBase + 3) + ((indexBase + 2) << 16);
							}
							else if (indexBytes == 4)
							{
								*indexBuffer++ = indexBase + 0;
								*indexBuffer++ = indexBase + 1;
								*indexBuffer++ = indexBase + 2;

								*indexBuffer++ = indexBase + 1;
								*indexBuffer++ = indexBase + 3;
								*indexBuffer++ = indexBase + 2;
							}

							indexBase += 2;

							// Update stats
							*triangleCount += 2;
						}

						// Next angle
						angle += dAngle;
					}

					indexBase += 2;
				}

				return renderUsage;
			}
		};

		template<>
		struct BombVertexDataBuilder<float, void, void, 0>
		{
			int operator()(std::vector<ArcRenderData> const& arcRenderData, std::vector<Bomb> const& bombs, float scale, mpp::Colour const& colour, int* triangleCount, int* vertexCount, IndexedTriangleBatchRenderable* renderable)
			{
				WP_UNUSED(scale);
				WP_UNUSED(colour);

				int renderUsage = 0;

				*triangleCount = 0;
				*vertexCount = 0;

				// Get pointers to buffers
				float* posBuffer = (float*)renderable->getPositionData();
				uint32_t* indexBuffer = (uint32_t*)renderable->getIndexData();

				int indexBase = 0;
				for (auto const& ard : arcRenderData)
				{
					auto const& bomb = bombs[ard.bombIndex];

					Vector2 const& pos = bomb.getPosition();
					float radius = bomb.getRadius();
					float depth = bomb.getDepth();

					float angle = ard.angle;
					float dAngle = ard.size / ard.numSegments;

					for (int i = 0; i <= ard.numSegments; ++i)
					{
						// Position
						auto d1 = Vector2(0, 1).rotatedClockwiseCopy(angle);

						auto s0 = pos + d1 * std::max(0.0f, radius - depth);
						auto s1 = pos + d1 * radius;

						// Near vertex
						*posBuffer++ = (float)s0.x;
						*posBuffer++ = (float)s0.y;

						// Far vertex
						*posBuffer++ = (float)s1.x;
						*posBuffer++ = (float)s1.y;

						// Update stats
						*vertexCount += 2;
						renderUsage += 4 * sizeof(float);

						// Indices
						if (i > 0)
						{
							int indexBytes = renderable->getIndexWidth() / 8;
							if (indexBytes == 2)
							{
								*indexBuffer++ = (indexBase + 0) + ((indexBase + 1) << 16);
								*indexBuffer++ = (indexBase + 2) + ((indexBase + 1) << 16);
								*indexBuffer++ = (indexBase + 3) + ((indexBase + 2) << 16);
							}
							else if (indexBytes == 4)
							{
								*indexBuffer++ = indexBase + 0;
								*indexBuffer++ = indexBase + 1;
								*indexBuffer++ = indexBase + 2;

								*indexBuffer++ = indexBase + 1;
								*indexBuffer++ = indexBase + 3;
								*indexBuffer++ = indexBase + 2;
							}

							indexBase += 2;

							// Update stats
							*triangleCount += 2;
						}

						// Next angle
						angle += dAngle;
					}

					indexBase += 2;
				}

				return renderUsage;
			}
		};
	} // firepower
} // WP_NAMESPACE