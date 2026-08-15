#pragma once

#include "core/Platform.h"
#include "core/Primitive.h"


namespace bw
{
	namespace core
	{

		class BW_API TorusPolygon : public Primitive
		{
			friend class World; // Only World can call the default constructor (during deserialization)

		protected:

			static const uint32_t BaseResolution = 64;

			float mThickness;

			float mResolution;

			uint32_t mNumSides;

		protected:

			TorusPolygon();

			void copyFrom(TorusPolygon const& other);

			void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

			bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

			std::vector<ComplexPolygon> generateVerticesImpl() override;

		public:

			TorusPolygon(Operation operation, FillRule fillType, float thickness, float resolution);

			TorusPolygon(TorusPolygon const& other);

			TorusPolygon& operator=(TorusPolygon const& other);

			Primitive* copy() const override;

			std::string getType() const override;

			float getRadius() const override;

			void setThickness(float thickness);

			float getThickness() const;
			
			void setResolution(float resolution);

			float getResolution() const;

			void setNumSides(uint32_t numSides);

			uint32_t getNumSides() const;

		};

	} // core
} // bw