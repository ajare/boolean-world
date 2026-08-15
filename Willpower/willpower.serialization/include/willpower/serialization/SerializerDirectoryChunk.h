#pragma once

#include <map>
#include <functional>

#include "willpower/common/Platform.h"

#include "willpower/serialization/Platform.h"
#include "willpower/serialization/SerializerChunk.h"

namespace WP_NAMESPACE
{
	namespace serialization
	{

		class WP_SERIALIZATION_API SerializerDirectoryChunk : public SerializerChunk
		{
		public:

			struct Entry
			{
				std::string name;
				std::string type;
				std::string description;
				int index;
				uint32_t offset;
			};

		private:

			std::vector<Entry> mEntries;

		private:

			void writeBinaryImpl(std::ostream& fp);

			void readBinaryImpl(std::istream& fp);

			void writeTextImpl(std::ostream& fp);

			void readTextImpl(std::istream& fp);

		public:

			SerializerDirectoryChunk(std::string const& description);

			void addChunk(std::string const& name, std::string const& type, std::string const& description, int index, uint32_t offset);

			int getNumEntries() const;

			Entry const& getEntry(int index) const;
		};

	} // serialization 
} // WP_NAMESPACE
