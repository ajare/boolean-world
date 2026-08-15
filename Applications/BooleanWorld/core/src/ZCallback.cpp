#pragma once

#include "core/Defines.h"
#include "core/ClipperDefines.h"
#include "core/ZCallback.h"
#include "core/PrimitivePropertySet.h"


namespace bw
{
	namespace core
	{
		namespace clipper2
		{

			using namespace std;

			ZCallback::ZCallback(vector<WorldVertexData> const& vertexWorldData, uint32_t flags)
				: mWorldVertexData(vertexWorldData)
				, mNumInterpolatedVertices(0)
				, mClipType(Clipper2Lib::ClipType::NoClip)
				, mFlags(flags)
			{
			}

			vector<WorldVertexData>& ZCallback::getVertexWorldData()
			{
				return mWorldVertexData;
			}

			uint32_t ZCallback::getNumInterpolatedVertices() const
			{
				return mNumInterpolatedVertices;
			}

			void ZCallback::setClipType(Clipper2Lib::ClipType const& clipType)
			{
				mClipType = clipType;
			}

			void ZCallback::interpolateVertex(Clipper2Lib::Point64 const& v00, Clipper2Lib::Point64 const& v01,
				Clipper2Lib::Point64 const& v10, Clipper2Lib::Point64 const& v11, Clipper2Lib::Point64& p)
			{
				// Create new entry right at the start, to avoid issues with vector resizing
				// behind the scenes and invalidating our pointers.
				uint32_t newVertexIndex = (uint32_t)mWorldVertexData.size();
								
				mWorldVertexData.push_back({});

				p.z = BW_VERTEX_Z_PACK_VERTEX_INDEX(0, newVertexIndex);
				p.z = BW_VERTEX_Z_SET_INTERPOLATED(p.z, 1);
				p.z = BW_VERTEX_Z_SET_PREV_PROP(p.z, 0);
				p.z = BW_VERTEX_Z_SET_NEXT_PROP(p.z, 0);

				if (mFlags & BW_CLIPPER_LERP_PROPERTIES)
				{
					// We don't care about the Primitive, but do care about interpolating its properties.
					//datap->prim = nullptr;

					// Get vertex indices.  In the case of the CellWorldDataGenerator [now removed], we may
					// be receiving clipping vertices which are not part of the World, and which
					// will therefore not have any z-information.  In this case, we ignore those
					// two (and it should always be a matching pair).
					auto i00 = BW_VERTEX_Z_UNPACK_VERTEX_INDEX(v00.z);
					auto i01 = BW_VERTEX_Z_UNPACK_VERTEX_INDEX(v01.z);
					auto i10 = BW_VERTEX_Z_UNPACK_VERTEX_INDEX(v10.z);
					auto i11 = BW_VERTEX_Z_UNPACK_VERTEX_INDEX(v11.z);

					if (i00 == BW_VERTEX_RECTCLIP_ID && i10 == BW_VERTEX_RECTCLIP_ID)
					{
						// Both intersecting lines are clip lines.  Ignore.
					}
					else if (i10 == BW_VERTEX_RECTCLIP_ID)
					{
						// Interpolate vertex data
						auto const* data00 = &mWorldVertexData[i00];
						auto const* data01 = &mWorldVertexData[i01];

						// Calculate P distances
						int64_t d00 = (p.x - v00.x) * (p.x - v00.x) + (p.y - v00.y) * (p.y - v00.y);
						int64_t d01 = (p.x - v01.x) * (p.x - v01.x) + (p.y - v01.y) * (p.y - v01.y);

						// Get total distance
						int64_t d0 = (v00.x - v01.x) * (v00.x - v01.x) + (v00.y - v01.y) * (v00.y - v01.y);
						double dt = sqrt(d0);

						double sd00 = sqrt(d00);
						double sd01 = sqrt(d01);

						// Distances to each point in [0, 1]: can be used as weights in interpolation
						double t00 = 1.0 - sd00 / dt;
						double t01 = 1.0 - sd01 / dt;

					}
					else if (i00 == BW_VERTEX_RECTCLIP_ID)
					{
						// Interpolate vertex data
						auto const* data10 = &mWorldVertexData[i10];
						auto const* data11 = &mWorldVertexData[i11];

						// Calculate P distances
						int64_t d10 = (p.x - v10.x) * (p.x - v10.x) + (p.y - v10.y) * (p.y - v10.y);
						int64_t d11 = (p.x - v11.x) * (p.x - v11.x) + (p.y - v11.y) * (p.y - v11.y);

						// Get total distance
						int64_t d1 = (v10.x - v11.x) * (v10.x - v11.x) + (v10.y - v11.y) * (v10.y - v11.y);
						double dt = sqrt(d1);

						double sd10 = sqrt(d10);
						double sd11 = sqrt(d11);

						// Distances to each point in [0, 1]: can be used as weights in interpolation
						double t10 = 1.0 - sd10 / dt;
						double t11 = 1.0 - sd11 / dt;
					}
					else
					{
						// Interpolate vertex data
						auto const* data00 = &mWorldVertexData[i00];
						auto const* data01 = &mWorldVertexData[i01];
						auto const* data10 = &mWorldVertexData[i10];
						auto const* data11 = &mWorldVertexData[i11];

						// Calculate P distances
						int64_t d00 = (p.x - v00.x) * (p.x - v00.x) + (p.y - v00.y) * (p.y - v00.y);
						int64_t d01 = (p.x - v01.x) * (p.x - v01.x) + (p.y - v01.y) * (p.y - v01.y);
						int64_t d10 = (p.x - v10.x) * (p.x - v10.x) + (p.y - v10.y) * (p.y - v10.y);
						int64_t d11 = (p.x - v11.x) * (p.x - v11.x) + (p.y - v11.y) * (p.y - v11.y);

						// Get total distance
						int64_t d0 = (v00.x - v01.x) * (v00.x - v01.x) + (v00.y - v01.y) * (v00.y - v01.y);
						int64_t d1 = (v10.x - v11.x) * (v10.x - v11.x) + (v10.y - v11.y) * (v10.y - v11.y);
						double dt = sqrt(d0) + sqrt(d1);

						double sd00 = sqrt(d00);
						double sd01 = sqrt(d01);
						double sd10 = sqrt(d10);
						double sd11 = sqrt(d11);

						// Distances to each point in [0, 1]
						double t00 = (1.0 - sd00 / dt) / 3.0;
						double t01 = (1.0 - sd01 / dt) / 3.0;
						double t10 = (1.0 - sd10 / dt) / 3.0;
						double t11 = (1.0 - sd11 / dt) / 3.0;

						// Weighted value is then:
						//v = v(00) * t(00) + v(01) * t(01) + v(10) * t(10) + v(11) * t(11)	
					}
				}
				
				// Set primitive index
				uint32_t primIndex = BW_WORLD_PRIMITIVE_NO_INDEX;

				if (mFlags & BW_CLIPPER_SET_PRIMITIVE)
				{
					// Just calculate which Primitive the vertex belongs to.
					switch (mClipType)
					{
					case Clipper2Lib::ClipType::Union:
						// No primitive
						break;

					case Clipper2Lib::ClipType::Difference:
					case Clipper2Lib::ClipType::Intersection:
					case Clipper2Lib::ClipType::Xor:
						primIndex = BW_VERTEX_Z_UNPACK_PRIMITIVE_INDEX(v00.z);
						break;

					default:
						break;
					}
				}

				auto datap = &mWorldVertexData[newVertexIndex];

				datap->primitiveIndex = primIndex;
				p.z = BW_VERTEX_Z_PACK_PRIMITIVE_INDEX(p.z, primIndex);

				mNumInterpolatedVertices++;
			}

		} // clipper2
	} // core
} // bw
