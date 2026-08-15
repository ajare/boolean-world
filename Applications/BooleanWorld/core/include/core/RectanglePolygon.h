#pragma once

#include "core/Platform.h"
#include "core/Primitive.h"


namespace bw
{
	namespace core
	{

		class BW_API RectanglePolygon : public Primitive
		{
			friend class World; // Only World can call the default constructor (during deserialization)

		protected:

			float mXyRatio;

		protected:

			RectanglePolygon();

			void copyFrom(RectanglePolygon const& other);

			void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

			bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

			std::vector<ComplexPolygon> generateVerticesImpl() override;

		public:

			RectanglePolygon(Operation operation, FillRule fillType, float xyRatio);

			RectanglePolygon(RectanglePolygon const& other);

			RectanglePolygon& operator=(RectanglePolygon const& other);

			Primitive* copy() const override;

			std::string getType() const override;

			float getRadius() const override;

			void setXyRatio(float xyRatio);

			float getXyRatio() const;
		};

	} // core
} // bw