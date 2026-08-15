#include "willpower/application/resourcesystem/ResourceExceptions.h"
#include "willpower/common/Exceptions.h"

#include "GameResourceDefinitionFactory.h"

namespace applib
{

	using namespace std;
	using namespace wp;

	GameResourceDefinitionFactory::GameResourceDefinitionFactory(string const& factoryType)
		: ResourceDefinitionFactory("Game", factoryType)
	{
	}

	void GameResourceDefinitionFactory::registerBullet(Game* game, application::resourcesystem::AnimationSetResource* animSetRes, uint32_t bulletId, string const& fireAnimName, string const& explodeAnimName, float size)
	{
		auto fireId = game->mAnimDatabase->registerAnimation(animSetRes, fireAnimName);
		uint32_t explodeId = explodeAnimName != "" ? game->mAnimDatabase->registerAnimation(animSetRes, explodeAnimName) : ~0u;

		// Set up bullet data
		auto& bd = game->mBulletData[bulletId];

		bd.fireAnimationId = fireId;
		bd.explodeAnimationId = explodeId;
		bd.size = size;
	}

	uint32_t GameResourceDefinitionFactory::getBulletIdFromName(string const& name)
	{
		throw NotImplementedException();
	}

	void GameResourceDefinitionFactory::setBulletsAnimationSet(Game* game, application::resourcesystem::ResourcePtr resource)
	{
		game->mBulletsAnimationSet = resource;
	}

} // applib