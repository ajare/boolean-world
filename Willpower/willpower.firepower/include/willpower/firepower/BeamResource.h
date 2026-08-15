#pragma once


#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/resourcesystem/ResourceDefinitionFactory.h"

#include "willpower/firepower/Platform.h"
#include "willpower/firepower/Beam.h"

namespace WP_NAMESPACE
{
	namespace firepower
	{
		class WP_FIREPOWER_API BeamResource : public application::resourcesystem::Resource
		{
			friend class BeamResourceDefinitionFactory;

		private:

			Beam::SectionOptions mSectionOptions;

			Beam::TexCoordOptions mTexCoordOptions;

			int mHeadOffset, mBodyOffset, mBaseOffset;

		public:

			BeamResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, application::resourcesystem::ResourceLocation* location);

			application::resourcesystem::ResourcePtr getImage();

			Beam::SectionOptions getSectionOptions() const;

			Beam::TexCoordOptions getTexCoordOptions() const;

			int getHeadOffset() const;

			int getBodyOffset() const;

			int getBaseOffset() const;
		};

		class WP_FIREPOWER_API BeamResourceDefinitionFactory : public application::resourcesystem::ResourceDefinitionFactory
		{
		protected:

			void setSectionOptions(BeamResource* resource, Beam::SectionOptions options);

			void setHeadOffset(BeamResource* resource, int offset);

			void setBodyOffset(BeamResource* resource, int offset);

			void setBaseOffset(BeamResource* resource, int offset);

			void setTexCoordOptions(BeamResource* resource, Beam::TexCoordOptions options);

		public:

			explicit BeamResourceDefinitionFactory(std::string const& factoryType);
		};

	} // firepower
} // WP_NAMESPACE

