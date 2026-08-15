#include "willpower/application/resourcesystem/ResourceExceptions.h"

#include "Game.h"

namespace applib
{

	using namespace std;
	using namespace wp;

	Game::Game(string const& name,
		string const& namesp,
		string const& source,
		map<string, string> const& tags,
		application::resourcesystem::ResourceLocation* location,
		shared_ptr<AnimationDatabase> animDatabase)
		: application::resourcesystem::Resource(name, namesp, "Game", source, tags, location)
		, mAnimDatabase(animDatabase)
	{
		mBulletData.resize(AnimationDatabase::MaxObjectTypes);
		for (int i = 0; i < AnimationDatabase::MaxObjectTypes; ++i)
		{
			auto& bd = mBulletData[i];

			bd.size = -1;
			bd.fireAnimationId = -1;
			bd.explodeAnimationId = -1;
		}
	}

	Game::~Game()
	{
	}

	void Game::create(application::resourcesystem::DataStreamPtr dataPtr, application::resourcesystem::ResourceManager* resourceMgr)
	{
		parseData(dataPtr);
		parseDefinition(resourceMgr);
	}

	application::resourcesystem::ResourcePtr Game::getBulletAnimationSet() const
	{
		return mBulletsAnimationSet;
	}

	Game::BulletData const& Game::getBulletData(int objectId) const
	{
		return mBulletData[objectId];
	}

} // applib