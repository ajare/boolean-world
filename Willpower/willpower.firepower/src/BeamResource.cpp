#include <utils/StringUtils.h>
#include <utils/XmlReader.h>

#include "willpower/application/resourcesystem/ImageResource.h"

#include "willpower/common/Exceptions.h"

#include "willpower/firepower/BeamResource.h"

namespace WP_NAMESPACE
{
	namespace firepower
	{
		using namespace std;
		using namespace utils;
		using namespace application::resourcesystem;

		BeamResource::BeamResource(string const& name, string const& namesp, string const& source, map<string, string> const& tags, ResourceLocation* location)
			: Resource(name, namesp, "Beam", source, tags, location)
			, mSectionOptions(Beam::SectionOptions::Single)
			, mTexCoordOptions(Beam::TexCoordOptions::Unitised)
			, mHeadOffset(0)
			, mBodyOffset(0)
			, mBaseOffset(0)
		{
		}

		application::resourcesystem::ResourcePtr BeamResource::getImage()
		{
			return getDependentResource("Image");
		}

		Beam::SectionOptions BeamResource::getSectionOptions() const
		{
			return mSectionOptions;
		}

		Beam::TexCoordOptions BeamResource::getTexCoordOptions() const
		{
			return mTexCoordOptions;
		}

		int BeamResource::getHeadOffset() const
		{
			return mHeadOffset;
		}

		int BeamResource::getBodyOffset() const
		{
			return mBodyOffset;
		}

		int BeamResource::getBaseOffset() const
		{
			return mBaseOffset;
		}

		BeamResourceDefinitionFactory::BeamResourceDefinitionFactory(std::string const& factoryType)
			: ResourceDefinitionFactory("Beam", factoryType)
		{
		}

		void BeamResourceDefinitionFactory::setSectionOptions(BeamResource* resource, Beam::SectionOptions options)
		{
			resource->mSectionOptions = options;
		}

		void BeamResourceDefinitionFactory::setHeadOffset(BeamResource* resource, int offset)
		{
			resource->mHeadOffset = offset;
		}

		void BeamResourceDefinitionFactory::setBodyOffset(BeamResource* resource, int offset)
		{
			resource->mBodyOffset = offset;
		}

		void BeamResourceDefinitionFactory::setBaseOffset(BeamResource* resource, int offset)
		{
			resource->mBaseOffset = offset;
		}

		void BeamResourceDefinitionFactory::setTexCoordOptions(BeamResource* resource, Beam::TexCoordOptions options)
		{
			resource->mTexCoordOptions = options;
		}

	} // firepower
} // WP_NAMESPACE