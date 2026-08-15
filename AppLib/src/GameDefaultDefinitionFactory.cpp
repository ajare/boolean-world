#include <utils/StringUtils.h>

#include "willpower/application/resourcesystem/ResourceExceptions.h"
#include "willpower/common/Exceptions.h"

#include "Game.h"
#include "GameDefaultDefinitionFactory.h"

namespace applib {
using namespace std;
using namespace utils;
using namespace wp;

GameDefaultDefinitionFactory::GameDefaultDefinitionFactory()
    : GameResourceDefinitionFactory("") {
}

void GameDefaultDefinitionFactory::create(application::resourcesystem::Resource* resource, application::resourcesystem::ResourceManager* resourceMgr, wp::DataNode* node) {
  auto gameRes = static_cast<Game*>(resource);

  // Bullets
  auto animSetRes = gameRes->getDependentResource("AnimationSet.Bullets");
  setBulletsAnimationSet(gameRes, animSetRes);

  auto bulletsNode = node->getOptionalChild("Bullets");
  if (bulletsNode) {
    auto bulletNode = bulletsNode->getOptionalChild("Bullet");

    if (bulletNode) {
      do {
        auto bulletName = bulletNode->getProperty("name");
        auto fireAnimName = bulletNode->getProperty("fire");

        string explodeAnimName;
        bulletNode->getOptionalProperty("explode", explodeAnimName);

        string sizeStr;
        bulletNode->getOptionalProperty("size", sizeStr);

        uint32_t sizeX, sizeY;
        static_cast<application::resourcesystem::AnimationSetResource*>(animSetRes.get())->getMaximumDimensions(sizeX, sizeY);

        float size = (float)max(sizeX, sizeY);

        if (sizeStr != "") {
          size = (float)utils::StringUtils::parseFloat(sizeStr);
        }

        auto bulletId = getBulletIdFromName(bulletName);
        registerBullet(
            gameRes,
            static_cast<application::resourcesystem::AnimationSetResource*>(animSetRes.get()),
            bulletId,
            fireAnimName,
            explodeAnimName,
            size);
      } while (bulletNode->next());
    }
  }
}

}  // namespace applib
