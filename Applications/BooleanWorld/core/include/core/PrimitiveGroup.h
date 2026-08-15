#pragma once

#include <vector>

#include "core/Primitive.h"
#include "core/VertexTransformerObject.h"


namespace bw
{
	namespace core
	{

		class BW_API PrimitiveGroup : public VertexTransformerObject
		{
			std::vector<Primitive*> mPrimitives;

		protected:

			void invalidatePostTransform(bool recalculateBounds, bool notifyWorld) const override;

			void copyFrom(PrimitiveGroup const& other);

		public:

			PrimitiveGroup();

			explicit PrimitiveGroup(std::vector<Primitive*> primitives);

			PrimitiveGroup(PrimitiveGroup const& other);

			PrimitiveGroup& operator=(PrimitiveGroup const& other);

			std::vector<Primitive*> const& getPrimitives() const;

			void addPrimitive(Primitive* primitive);

			void removePrimitive(Primitive* primitive, bool failIfNotFound = true);

			void removeAllPrimitives();
		};

	} // core
} // bw