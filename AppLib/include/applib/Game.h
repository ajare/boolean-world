#pragma once

#include <string>

#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"
#include "willpower/application/resourcesystem/ResourceDefinitionFactory.h"

#include "Platform.h"
#include "AnimationDatabase.h"

namespace applib
{

	class APPLIB_API Game : public wp::application::resourcesystem::Resource
	{
		friend class GameResourceDefinitionFactory;

	public:

		struct BulletData
		{
			float size;
			int32_t fireAnimationId;
			int32_t explodeAnimationId;
		};

	private:

		std::shared_ptr<AnimationDatabase> mAnimDatabase;

		wp::application::resourcesystem::ResourcePtr mBulletsAnimationSet;

		std::vector<BulletData> mBulletData;

	private:

		void create(wp::application::resourcesystem::DataStreamPtr dataPtr, wp::application::resourcesystem::ResourceManager* resourceMgr) override;

	public:

		Game(std::string const& name,
			std::string const& namesp,
			std::string const& source,
			std::map<std::string, std::string> const& tags,
			wp::application::resourcesystem::ResourceLocation* location,
			std::shared_ptr<AnimationDatabase> animDatabase);

		~Game();

		wp::application::resourcesystem::ResourcePtr getBulletAnimationSet() const;

		BulletData const& getBulletData(int objectId) const;

		virtual uint32_t getBulletReferenceId(std::string const& bulletName) = 0;

		virtual uint32_t getBulletAnimationReferenceId(std::string const& animationType) = 0;
	};

} // applib