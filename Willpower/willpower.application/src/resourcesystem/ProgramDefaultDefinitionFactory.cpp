#include <utils/StringUtils.h>

#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/ProgramDefaultDefinitionFactory.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			using namespace std;
			using namespace utils;

			ProgramDefaultDefinitionFactory::ProgramDefaultDefinitionFactory()
				: ProgramResourceDefinitionFactory("")
			{
			}

			void ProgramDefaultDefinitionFactory::create(Resource* resource, ResourceManager* resourceMgr, utils::XmlNode* node)
			{
				WP_UNUSED(resourceMgr);

				auto programRes = static_cast<ProgramResource*>(resource);

				auto attribsNode = node->getChild("Attribs");
				parseAttribs(programRes, attribsNode);

				auto meshSpecNode = node->getChild("MeshSpecification");
				parseMeshSpecification(programRes, meshSpecNode);
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE