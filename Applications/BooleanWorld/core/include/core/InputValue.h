#pragma once

#include <vector>

#include "core/Platform.h"
#include "core/WorldTriggerLine.h"


namespace bw
{
	namespace core
	{

		struct InputValue
		{
			float entityInfluenceDistance;

			float entityInfluenceAngle;

			float entityGlobalAngle;

			bool playerMove, playerTurn;

			float user[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

			std::vector<WorldTriggerLine*>* triggerLines;

		private:

			void copyFrom(InputValue const& other)
			{
				entityInfluenceDistance = other.entityInfluenceDistance;
				entityInfluenceAngle = other.entityInfluenceAngle;
				entityGlobalAngle = other.entityGlobalAngle;
				playerMove = other.playerMove;
				playerTurn = other.playerTurn;

				for (int i = 0; i < 4; ++i)
				{
					user[i] = other.user[i];
				}

				triggerLines = other.triggerLines;
			}

		public:

			InputValue()
				: entityInfluenceDistance(1.0f)
				, entityInfluenceAngle(0.0f)
				, entityGlobalAngle(0.0f)
				, playerMove(false)
				, playerTurn(false)
				, triggerLines(nullptr)
			{
				for (int i = 0; i < 4; ++i)
				{
					user[i] = 0.0f;
				}
			}

			InputValue(InputValue const& other)
			{
				copyFrom(other);
			}

			InputValue& operator=(InputValue const& other)
			{
				copyFrom(other);
				return *this;
			}
		};

	} // bw
} // core
