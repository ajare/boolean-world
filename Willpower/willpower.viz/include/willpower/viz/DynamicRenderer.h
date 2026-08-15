#pragma once

#include <functional>

#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>
#include <mpp/Material.h>
#include <mpp/Batch.h>

#include "willpower/common/Platform.h"
#include "willpower/common/Renderable.h"

#include "willpower/viz/Platform.h"
#include "willpower/viz/RenderParams.h"
#include "willpower/viz/Renderer.h"

namespace WP_NAMESPACE
{
	namespace viz
	{

		class WP_VIZ_API DynamicRenderer : public Renderer
		{
		public:

			DynamicRenderer(std::string const& name, std::string const& type, mpp::ResourceManager* renderResourceMgr);
		};

	} // viz
} // WP_NAMESPACE

