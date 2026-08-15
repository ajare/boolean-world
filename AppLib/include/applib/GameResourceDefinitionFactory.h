#pragma once

#include <string>

#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"
#include "willpower/application/resourcesystem/ResourceDefinitionFactory.h"

#include "Platform.h"
#include "Game.h"

namespace applib
{

	class APPLIB_API GameResourceDefinitionFactory : public wp::application::resourcesystem::ResourceDefinitionFactory
	{
	protected:

		void setBulletsAnimationSet(Game* game, wp::application::resourcesystem::ResourcePtr resource);

		void registerBullet(Game* game, wp::application::resourcesystem::AnimationSetResource* animSetRes, uint32_t bulletId, std::string const& fireAnimName, std::string const& explodeAnimName, float size);

		virtual uint32_t getBulletIdFromName(std::string const& name);

	public:

		explicit GameResourceDefinitionFactory(std::string const& factoryType);
	};

} // applib