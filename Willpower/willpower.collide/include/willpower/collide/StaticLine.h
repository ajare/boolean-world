#pragma once

#include "willpower/common/Vector2.h"

#include "willpower/collide/Platform.h"

namespace WP_NAMESPACE
{
	namespace collide
	{

		class WP_COLLIDE_API StaticLine
		{
			Vector2 mVertices[2];
			
			Vector2 mNormal;

			bool mDoubleSided;

			float mFriction;

			int32_t mUserData;

			bool mEnabled;

		public:

			StaticLine();

			StaticLine(Vector2 const& v0, Vector2 const& v1, bool doubleSided, float friction, int32_t userData = -1);

			Vector2 const& getVertex(int index) const;

			void getVertices(Vector2& v0, Vector2& v1) const;

			Vector2 const& getNormal() const;

			bool isDoubleSided() const;

			float getFriction() const;

			int32_t getUserData() const;

			void enable(bool enable);

			bool enabled() const;
		};

	} // collide
} // WP_NAMESPACE

