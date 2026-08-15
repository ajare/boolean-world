#include "core/TorusPolygon.h"


namespace bw
{
	namespace core
	{

		using namespace std;

		TorusPolygon::TorusPolygon()
			: Primitive()
			, mThickness(0.5f)
			, mResolution(1.0f)
			, mNumSides(BaseResolution)
		{
		}

		TorusPolygon::TorusPolygon(Operation operation, FillRule fillType, float thickness, float resolution)
			: Primitive(operation, fillType)
			, mThickness(thickness)
			, mResolution(resolution)
			, mNumSides((uint32_t)(resolution * BaseResolution))
		{
			generateVertices();
		}

		TorusPolygon::TorusPolygon(TorusPolygon const& other)
		{
			copyFrom(other);
		}

		TorusPolygon& TorusPolygon::operator=(TorusPolygon const& other)
		{
			copyFrom(other);
			return *this;
		}

		void TorusPolygon::copyFrom(TorusPolygon const& other)
		{
			Primitive::copyFrom(other);

			mThickness = other.mThickness;
			mResolution = other.mResolution;
			mNumSides = other.mNumSides;
		}

		Primitive* TorusPolygon::copy() const
		{
			return new TorusPolygon(*this);
		}

		string TorusPolygon::getType() const
		{
			return "Torus";
		}

		void TorusPolygon::serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const
		{
			Primitive::serializeImpl(serializer, workData);

			serializer->beginMap("torusPolygon");
			{
				serializer->writeFloat("thickness", mThickness);
				serializer->writeFloat("resolution", mResolution);
				serializer->writeUint32("numSides", mNumSides);

				serializer->endMap(); // torusPolygon
			}
		}

		bool TorusPolygon::deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData)
		{
			if (!Primitive::deserializeImpl(serializer, workData))
			{
				return false;
			}

			float thickness, resolution;
			uint32_t numSides;

			try
			{
				serializer->beginMap("torusPolygon");
				{
					thickness = serializer->readFloat("thickness");
					resolution = serializer->readFloat("resolution");
					numSides = serializer->readUint32("numSides");

					serializer->endMap(); // torusPolygon
				}
			}
			catch (exception& e)
			{
				addDeserializationError(e.what());
				return false;
			}

			// Commit
			mThickness = thickness;
			mResolution = resolution;
			mNumSides = numSides;
			
			return true;
		}

		vector<ComplexPolygon> TorusPolygon::generateVerticesImpl()
		{
			ClosedPolygon outerVertices(mNumSides), innerVertices(mNumSides);

			for (uint32_t i = 0; i < mNumSides; ++i)
			{
				float angle = 360.0f * i / (float)mNumSides;
				outerVertices[i] = {
					wp::Vector2::UNIT_Y.rotatedClockwiseCopy(angle),
					0
				};

				innerVertices[mNumSides - i - 1] = { outerVertices[i].p * (1.0f - mThickness), 0 };
			}

			return { { outerVertices, innerVertices } };
		}

		float TorusPolygon::getRadius() const
		{
			throw 1.0f;
		}

		void TorusPolygon::setThickness(float thickness)
		{
			mThickness = thickness;
			generateVertices();
		}

		float TorusPolygon::getThickness() const
		{
			return mThickness;
		}

		void TorusPolygon::setResolution(float resolution)
		{
			mResolution = resolution;
			mNumSides = (uint32_t)(resolution * BaseResolution);
			generateVertices();
		}

		float TorusPolygon::getResolution() const
		{
			return mResolution;
		}

		void TorusPolygon::setNumSides(uint32_t numSides)
		{
			mNumSides = numSides;
			mResolution = mNumSides / (float)BaseResolution;
			generateVertices();
		}

		uint32_t TorusPolygon::getNumSides() const
		{
			return mNumSides;
		}

	} // core
} // bw