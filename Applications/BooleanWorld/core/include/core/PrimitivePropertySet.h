#pragma once

#include <vector>

#include "core/Serializable.h"
#include "core/MaterialDefinition.h"


namespace bw
{
	namespace core
	{
		class Primitive;

		struct PrimitivePropertySet : public Serializable
		{
			float floorZ{ 0 }, ceilingZ{ 48 };

			uint32_t floorMaterialIndex;
			MaterialDefinition floorMaterialDef;

			uint32_t ceilingMaterialIndex;
			MaterialDefinition ceilingMaterialDef;

			uint32_t wallMaterialIndex;
			MaterialDefinition wallMaterialDef;

		public:

			bool childrenModified() const override;

			void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

			bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;
		};

	} // core
} // bw