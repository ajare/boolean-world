#pragma once

#include "core/Platform.h"
#include "core/Primitive.h"


namespace bw
{
	namespace core
	{

		class BW_API TorusSegmentPolygon : public Primitive
		{
			friend class World; // Only World can call the default constructor (during deserialization)

		protected:

			static const uint32_t BaseResolution = 64;

			float mThickness;

			float mArcLength;

			float mResolution;

			uint32_t mNumSides;

		protected:

			TorusSegmentPolygon();

			void copyFrom(TorusSegmentPolygon const& other);

			void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

			bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

			std::vector<ComplexPolygon> generateVerticesImpl() override;

		public:

			TorusSegmentPolygon(Operation operation, FillRule fillType, float thickness, float arcLength, float resolution);

			TorusSegmentPolygon(TorusSegmentPolygon const& other);

			TorusSegmentPolygon& operator=(TorusSegmentPolygon const& other);

			Primitive* copy() const override;

			std::string getType() const override;

			float getRadius() const override;

			void setThickness(float thickness);

			float getThickness() const;

			void setArcLength(float arcLength);

			float getArcLength() const;

			void setResolution(float resolution);

			float getResolution() const;

			void setNumSides(uint32_t numSides);

			uint32_t getNumSides() const;

		};

	} // core
} // bw