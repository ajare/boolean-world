#pragma once

#include "willpower/firepower/Platform.h"
#include "willpower/firepower/BeamResource.h"

namespace WP_NAMESPACE
{
	namespace firepower
	{
		class WP_FIREPOWER_API BeamDefaultDefinitionFactory : public BeamResourceDefinitionFactory
		{
		public:

			BeamDefaultDefinitionFactory();

			void create(application::resourcesystem::Resource* resource, application::resourcesystem::ResourceManager* resourceMgr, utils::XmlNode* node) override;
		};

	} // firepower
} // WP_NAMESPACE

