#pragma once

#include <willpower/common/Vector2.h>
#include <willpower/common/BoundingBox.h>

#include "core/Serializable.h"


namespace bw
{
	namespace core
	{

		enum struct WorldTriggerLineSide
		{
			Red,
			Blue,
			Both
		};

		class WorldTriggerLine : public Serializable
		{
			uint32_t mId;

			uint8_t mLayer;

			uint32_t mTriggerCount[2];

			wp::Vector2 mPoints[2];

			wp::BoundingBox mBounds;

			WorldTriggerLineSide mSide;

		private:

			bool childrenModified() const override;

			void updateBounds();

		protected:

			void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

			bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

		public:

			WorldTriggerLine();

			WorldTriggerLine(uint8_t layer, wp::Vector2 const& p0, wp::Vector2 const& p1, WorldTriggerLineSide side = WorldTriggerLineSide::Both);

			void setId(uint32_t id);

			uint32_t getId() const;

			void setLayer(uint8_t layer);

			[[nodiscard]] uint8_t getLayer() const;

			uint32_t getTriggerCount(WorldTriggerLineSide side) const;

			uint32_t getTotalTriggerCount() const;

			void setPoint(uint32_t index, wp::Vector2 const& position);

			wp::Vector2 const& getPoint(uint32_t index) const;

			void setSide(WorldTriggerLineSide side);

			WorldTriggerLineSide getSide() const;

			wp::BoundingBox const& getBounds() const;

			bool checkCollide(wp::Vector2 const& oldPos, wp::Vector2 const& newPos, float radius);
		};

	} // core
} // bw