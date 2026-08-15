#include "Defines.h"
#include "UiHelpers.h"

extern wp::Vector2 gViewOffset;


namespace editor
{
	using namespace std;

	bool mouseInteractingWithBackground()
	{
		auto worldPos = getMouseWorldPosition();

		auto const& io = ImGui::GetIO();
		return !io.WantCaptureMouse;
	}

	wp::Vector2 getMouseWorldPosition()
	{
		auto mouseScreenPos = ImGui::GetMousePos();

		return {
			(mouseScreenPos.x + gViewOffset.x) - ED_WINDOW_WIDTH / 2.0f,
			((ED_WINDOW_HEIGHT - mouseScreenPos.y) + gViewOffset.y) - ED_WINDOW_HEIGHT / 2.0f
		};
	}

	uint32_t getHoveredPrimitiveIndex(editor::Document* doc, Settings const& settings)
	{
		if (!mouseInteractingWithBackground())
		{
			return ~0u;
		}

		return doc->getHoveredPrimitiveIndex(getMouseWorldPosition(), settings);
	}

	vector<uint32_t> getHoveredPrimitiveIndices(editor::Document* doc, Settings const& settings)
	{
		if (!mouseInteractingWithBackground())
		{
			return vector<uint32_t>{};
		}

		return doc->getHoveredPrimitiveIndices(getMouseWorldPosition(), settings);
	}

	uint32_t getHoveredTriggerLineIndex(editor::Document* doc, Settings const& settings)
	{
		if (!mouseInteractingWithBackground())
		{
			return ~0u;
		}

		return doc->getHoveredTriggerLineIndex(getMouseWorldPosition(), settings);
	}

	uint32_t getHoveredWorldVertexIndex(editor::Document* doc, Settings const& settings, bw::core::WorldData const* worldData)
	{
		if (!mouseInteractingWithBackground())
		{
			return ~0u;
		}

		return (uint32_t)worldData->getNearestWorldVertexIndex(getMouseWorldPosition(), 3);
	}

	void generateClipping(editor::Document* doc, Settings const& settings, int flag)
	{
		if (settings.configFlags & flag)
		{
			auto world = doc->getWorld();
			auto culling = world->getWorldDataGenerator()->getNarrowPhaseCulling();

			world->generateClipping(culling, true);
		}
	}

} // editor