#pragma once

#include <willpower/common/Vector2.h>

#include <core/WorldData.h>

#include "imgui.h"

#include "Document.h"


namespace floored
{
	struct ScrollingBuffer
	{
		int MaxSize;
		int Offset;
		float CurMin, CurMax;
		ImVector<ImVec2> Data;

		ScrollingBuffer(int max_size = 2000)
		{
			MaxSize = max_size;
			Offset = 0;
			CurMin = 0;
			CurMax = 0;
			Data.reserve(MaxSize);
		}

		void AddPoint(float x, float y)
		{
			if (Data.size() < MaxSize)
			{
				Data.push_back(ImVec2(x, y));
			}
			else
			{
				Data[Offset] = ImVec2(x, y);
				Offset = (Offset + 1) % MaxSize;
			}

			if (y > CurMax)
			{
				CurMax = y;
			}
			if (y < CurMin)
			{
				CurMin = y;
			}
		}

		void Erase()
		{
			if (Data.size() > 0)
			{
				Data.shrink(0);
				Offset = 0;
			}

			CurMin = 0;
			CurMax = 0;
		}
	};

	bool mouseInteractingWithBackground();

	wp::Vector2 getMouseWorldPosition();

} // floored
