#include <utils/StringUtils.h>
#include <utils/XmlReader.h>

#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/ImageResource.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"

#include "willpower/firepower/BulletDefaultDefinitionFactory.h"

namespace WP_NAMESPACE
{
	namespace firepower
	{
		using namespace std;
		using namespace utils;

		BulletDefaultDefinitionFactory::BulletDefaultDefinitionFactory()
			: BulletResourceDefinitionFactory("")
		{
		}

		void BulletDefaultDefinitionFactory::create(application::resourcesystem::Resource* resource, utils::XmlNode* node)
		{
			auto bulletRes = static_cast<BulletResource*>(resource);

			// Get image size
			auto imageRes = static_cast<application::resourcesystem::ImageResource*>(bulletRes->getDependentResource("Image").get());
			int imageWidth = imageRes->getWidth();
			int imageHeight = imageRes->getHeight();

			int maxBulletCount = imageWidth / imageHeight;

			auto bulletsNode = node->getChild("Bullets");
			auto bulletNode = bulletsNode->getOptionalChild("Bullet");
			if (bulletNode)
			{
				do
				{
					if ((int)getNumDefinitions(bulletRes) >= maxBulletCount)
					{
						string errMsg = "Too many bullet definitions.";
						throw Exception(errMsg);
					}

					string name = bulletNode->getAttribute("name");
					int radius = StringUtils::parseInt(bulletNode->getAttribute("radius"));

					BulletResource::BulletDefinition d
					{
						name,
						radius,
						imageHeight
					};

					addDefinition(bulletRes, d);
				} while (bulletNode->next());
			}

		}

	} // firepower
} // WP_NAMESPACE