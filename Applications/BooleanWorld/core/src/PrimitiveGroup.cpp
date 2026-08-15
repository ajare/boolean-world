#include "core/PrimitiveGroup.h"


namespace bw
{
	namespace core
	{

		using namespace std;

		PrimitiveGroup::PrimitiveGroup()
		{
		}

		PrimitiveGroup::PrimitiveGroup(vector<Primitive*> primitives)
		{
			mPrimitives = primitives;

			for (auto primitive : mPrimitives)
			{
				primitive->setParent(this);
			}
		}

		PrimitiveGroup::PrimitiveGroup(PrimitiveGroup const& other)
		{
			copyFrom(other);
		}

		PrimitiveGroup& PrimitiveGroup::operator=(PrimitiveGroup const& other)
		{
			copyFrom(other);
			return *this;
		}

		void PrimitiveGroup::copyFrom(PrimitiveGroup const& other)
		{
			VertexTransformerObject::copyFrom(other);
		
			// DON'T copy over primitives, as these are shared.
		}

		void PrimitiveGroup::invalidatePostTransform(bool recalculateBounds, bool notifyWorld) const
		{
			for (auto primitive : mPrimitives)
			{
				primitive->invalidatePostTransform(recalculateBounds, notifyWorld);
			}
		}

		vector<Primitive*> const& PrimitiveGroup::getPrimitives() const
		{
			return mPrimitives;
		}

		void PrimitiveGroup::addPrimitive(Primitive* primitive)
		{
			mPrimitives.push_back(primitive);

			primitive->setParent(this);
		}

		void PrimitiveGroup::removePrimitive(Primitive* primitive, bool failIfNotFound)
		{
			auto numPrimitives = (uint32_t)mPrimitives.size();
			for (uint32_t i = 0; i < numPrimitives; ++i)
			{
				if (mPrimitives[i] == primitive)
				{
					mPrimitives[i]->setParent(nullptr);

					for (uint32_t j = i; j < numPrimitives - 1; ++j)
					{
						mPrimitives[j] = mPrimitives[j + 1];
					}

					return;
				}
			}

			if (failIfNotFound)
			{
				throw exception("Primitive not found in group");
			}
		}

		void PrimitiveGroup::removeAllPrimitives()
		{
			for (auto primitive : mPrimitives)
			{
				primitive->setParent(nullptr);
			}

			mPrimitives.clear();
		}

	} // core
} // bw