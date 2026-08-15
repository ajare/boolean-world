#pragma once

#include "imgui.h"


namespace editor
{
	namespace widgets
	{

		void PushDisabled();

		void PopDisabled();

		bool ToggleButton(const char* str_id, const char* title, bool v);

		bool ToggleButton(char const* str_id, const char* title, bool* v);

		void HelpMarker(char const* desc);

	} // widgets

} // editor
