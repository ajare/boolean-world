#pragma once

#include "willpower/common/Vector2.h"

namespace WP_NAMESPACE
{
	namespace collide
	{

		struct SweepResult
		{
			Vector2 oldPosition;
			Vector2 newPosition;
			Vector2 movementDesired;
			Vector2 movementDone;
			Vector2 movementLeft;
			float distanceMoved;
			float timeTaken;
		};

	} // collide
} // WP_NAMESPACE