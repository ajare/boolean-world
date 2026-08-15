#include "core/CirclePolygon.h"


namespace bw
{
	namespace core
	{

		using namespace std;

		CirclePolygon::CirclePolygon()
			: RegularPolygon()
			, mResolution(1.0f)
		{
			mNumSides = BaseResolution;
			generateVertices();
		}

		CirclePolygon::CirclePolygon(Operation operation, FillRule fillType, float resolution)
			: RegularPolygon(operation, fillType, (uint32_t)(resolution * BaseResolution))
			, mResolution(resolution)
		{
			generateVertices();
		}

		CirclePolygon::CirclePolygon(CirclePolygon const& other)
		{
			copyFrom(other);
		}

		CirclePolygon& CirclePolygon::operator=(CirclePolygon const& other)
		{
			copyFrom(other);
			return *this;
		}

		void CirclePolygon::copyFrom(CirclePolygon const& other)
		{
			RegularPolygon::copyFrom(other);
			
			mResolution = other.mResolution;
		}

		void CirclePolygon::serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const
		{
			RegularPolygon::serializeImpl(serializer, workData);

			serializer->beginMap("circlePolygon");
			{
				serializer->writeFloat("resolution", mResolution);

				serializer->endMap(); // circlePolygon
			}
		}

		bool CirclePolygon::deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData)
		{
			if (!RegularPolygon::deserializeImpl(serializer, workData))
			{
				return false;
			}

			float resolution;

			try
			{
				serializer->beginMap("circlePolygon");
				{
					resolution = serializer->readFloat("resolution");

					serializer->endMap(); // regularPolygon
				}
			}
			catch (exception& e)
			{
				addDeserializationError(e.what());
				return false;
			}

			// Commit
			mResolution = resolution;

			return true;
		}

		Primitive* CirclePolygon::copy() const
		{
			return new CirclePolygon(*this);
		}

		string CirclePolygon::getType() const
		{
			return "Circle";
		}

		void CirclePolygon::setNumSides(uint32_t numSides)
		{
			mResolution = numSides / (float)BaseResolution;
			RegularPolygon::setNumSides(numSides);  // calls generateVertices()
		}

		void CirclePolygon::setResolution(float resolution)
		{
			mResolution = resolution;
			mNumSides = (uint32_t)(resolution * BaseResolution);
			generateVertices();
		}

		float CirclePolygon::getResolution() const
		{
			return mResolution;
		}

	} // core
} // bw