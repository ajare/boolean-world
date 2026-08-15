#include <utils/StringUtils.h>
#include <utils/XmlReader.h>

#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/ResourceExceptions.h"

#include "willpower/firepower/BeamDefaultDefinitionFactory.h"

namespace WP_NAMESPACE
{
	namespace firepower
	{
		using namespace std;
		using namespace utils;

		BeamDefaultDefinitionFactory::BeamDefaultDefinitionFactory()
			: BeamResourceDefinitionFactory("")
		{
		}

		void BeamDefaultDefinitionFactory::create(application::resourcesystem::Resource* resource, application::resourcesystem::ResourceManager* resourceMgr, utils::XmlNode* node)
		{
			WP_UNUSED(resourceMgr);

			auto beamRes = static_cast<BeamResource*>(resource);

			// Section options
			string sectionsTag = beamRes->getTag("sections");
			if (sectionsTag == "Single")
			{
				setSectionOptions(beamRes, Beam::SectionOptions::Single);
			}
			else if (sectionsTag == "Scale2")
			{
				setSectionOptions(beamRes, Beam::SectionOptions::Scale2);

				setHeadOffset(beamRes, StringUtils::parseInt(node->getChild("HeadOffset")->getValue()));
				setBodyOffset(beamRes, StringUtils::parseInt(node->getChild("BodyOffset")->getValue()));

				NOT_IMPLEMENTED_YET("scale 2");
			}
			else if (sectionsTag == "Scale3")
			{
				setSectionOptions(beamRes, Beam::SectionOptions::Scale3);

				setHeadOffset(beamRes, StringUtils::parseInt(node->getChild("HeadOffset")->getValue()));
				setBodyOffset(beamRes, StringUtils::parseInt(node->getChild("BodyOffset")->getValue()));
				setBaseOffset(beamRes, StringUtils::parseInt(node->getChild("BaseOffset")->getValue()));

				NOT_IMPLEMENTED_YET("scale 3");
			}
			else if (sectionsTag == "Scale9")
			{
				setSectionOptions(beamRes, Beam::SectionOptions::Scale9);
				NOT_IMPLEMENTED_YET("scale 9");
			}
			else
			{
				NOT_IMPLEMENTED_YET("unknown scaling type");
			}

			// TexCoord options
			string texCoordTag = beamRes->getTag("texcoords");
			if (texCoordTag == "Unitised")
			{
				setTexCoordOptions(beamRes, Beam::TexCoordOptions::Unitised);
			}
			else if (texCoordTag == "Scaled")
			{
				setTexCoordOptions(beamRes, Beam::TexCoordOptions::Scaled);
			}
			else
			{
				NOT_IMPLEMENTED_YET("unknown texcoord type");
			}
		}

	} // firepower
} // WP_NAMESPACE