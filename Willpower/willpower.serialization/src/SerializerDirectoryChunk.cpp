#include "willpower/serialization/SerializerDirectoryChunk.h"
#include "willpower/serialization/SerializationUtils.h"

using namespace std;

namespace WP_NAMESPACE
{
	namespace serialization
	{

		using namespace WP_NAMESPACE;

		SerializerDirectoryChunk::SerializerDirectoryChunk(string const& description)
			: SerializerChunk("Directory", "Directory", description)
		{
		}

		void SerializerDirectoryChunk::writeBinaryImpl(ostream& fp)
		{
			// Write chunk count
			SerializationUtils::writeUint32(fp, (uint32_t)mEntries.size());

			// Write chunks
			for (auto const& entry : mEntries)
			{
				// Write chunk name
				SerializationUtils::writeString(fp, entry.name);

				// Write chunk type
				SerializationUtils::writeString(fp, entry.type);

				// Write chunk description
				SerializationUtils::writeString(fp, entry.description);

				// Write chunk index
				SerializationUtils::writeInt32(fp, entry.index);

				// Write chunk file offset
				SerializationUtils::writeUint32(fp, entry.offset);
			}
		}

		void SerializerDirectoryChunk::readBinaryImpl(istream& fp)
		{
			mEntries.clear();

			// Read chunk count
			uint32_t entryCount = SerializationUtils::readUint32(fp);

			// Write chunks
			for (uint32_t i = 0; i < entryCount; ++i)
			{
				Entry entry;

				// Read chunk name
				entry.name = SerializationUtils::readString(fp);

				// Read chunk type
				entry.type = SerializationUtils::readString(fp);

				// Read chunk description
				entry.description = SerializationUtils::readString(fp);

				// Read chunk index
				entry.index = SerializationUtils::readInt32(fp);

				// Read chunk file offset
				entry.offset = SerializationUtils::readUint32(fp);

				mEntries.push_back(entry);
			}
		}

		void SerializerDirectoryChunk::writeTextImpl(ostream& fp)
		{
			WP_UNUSED(fp);
		}

		void SerializerDirectoryChunk::readTextImpl(istream& fp)
		{
			WP_UNUSED(fp);
		}

		void SerializerDirectoryChunk::addChunk(string const& name, string const& type, string const& description, int index, uint32_t offset)
		{
			Entry entry;

			entry.name = name;
			entry.type = type;
			entry.description = description;
			entry.index = index;
			entry.offset = offset;

			mEntries.push_back(entry);
		}

		int SerializerDirectoryChunk::getNumEntries() const
		{
			return (int)mEntries.size();
		}

		SerializerDirectoryChunk::Entry const& SerializerDirectoryChunk::getEntry(int index) const
		{
			return mEntries[index];
		}

	} // serialization 
} // WP_NAMESPACE
