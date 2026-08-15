#pragma once

#include <vector>

#include <willpower/common/Vector2.h>

#include "core/Platform.h"
#include "core/Primitive.h"


namespace bw
{
	namespace core
	{

		class BW_API PathPolygon : public Primitive
		{
			friend class World; // Only World can call the default constructor (during deserialization)

		public:

			enum struct JoinType
			{
				Bevel,
				Mitre,
				Round,
				Square
			};

			enum struct EndType
			{
				Butt,
				Joined,
				Polygon,
				Round,
				Square
			};

		private:

			std::vector<wp::Vector2> mPoints;
			
			float mWidth;
				
			JoinType mJoinType;
				
			EndType mEndType;

		protected:

			PathPolygon();

			void copyFrom(PathPolygon const& other);

			void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

			bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

			std::vector<ComplexPolygon> generateVerticesImpl() override;

		public:

			PathPolygon(Operation operation, FillRule fillType, std::vector<wp::Vector2> const& points, float width, JoinType joinType = JoinType::Mitre, EndType endType = EndType::Square);

			PathPolygon(PathPolygon const& other);

			PathPolygon& operator=(PathPolygon const& other);

			Primitive* copy() const override;

			std::string getType() const override;

			float getRadius() const override;

			void setPoints(std::vector<wp::Vector2> const& points);

			std::vector<wp::Vector2> const& getPoints() const;

			void setWidth(float width);

			float getWidth() const;

			void setJoinType(JoinType joinType);

			JoinType getJoinType() const;

			void setEndType(EndType endType);

			EndType getEndType() const;
		};


	} // core
} // bw