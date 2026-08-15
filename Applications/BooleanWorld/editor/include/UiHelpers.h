#pragma once

#include <willpower/common/Vector2.h>

#include <core/WorldData.h>

#include "imgui.h"

#include "Defines.h"
#include "Document.h"
#include "Settings.h"


namespace editor
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

	uint32_t getHoveredPrimitiveIndex(editor::Document* doc, Settings const& settings);

	std::vector<uint32_t> getHoveredPrimitiveIndices(editor::Document* doc, Settings const& settings);

	uint32_t getHoveredWorldVertexIndex(editor::Document* doc, Settings const& settings, bw::core::WorldData const* worldData);

	uint32_t getHoveredTriggerLineIndex(editor::Document* doc, Settings const& settings);

	void generateClipping(editor::Document* doc, Settings const& settings, int flag);

} // editor
