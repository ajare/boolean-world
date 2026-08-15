#include "willpower/serialization/SerializerChunk.h"

using namespace std;

namespace WP_NAMESPACE
{
	namespace serialization
	{

		using namespace WP_NAMESPACE;

		SerializerChunk::SerializerChunk(string const& name, string const& type, string const& description)
			: mName(name)
			, mType(type)
			, mDescription(description)
			, mFileOffset(-1)
			, mOwnedBySerializer(true)
		{
		}

		string const& SerializerChunk::getName() const
		{
			return mName;
		}

		string const& SerializerChunk::getType() const
		{
			return mType;
		}

		string const& SerializerChunk::getDescription() const
		{
			return mDescription;
		}

		void SerializerChunk::releaseOwnership()
		{
			mOwnedBySerializer = false;
		}

		bool SerializerChunk::ownedBySerializer() const
		{
			return mOwnedBySerializer;
		}

		int32_t SerializerChunk::writeBinary(std::ostream& fp)
		{
			mFileOffset = (int32_t)fp.tellp();

			writeBinaryImpl(fp);

			return (int32_t)fp.tellp();
		}

		int32_t SerializerChunk::rewriteLastBinary(ostream& fp)
		{
			if (mFileOffset < 0)
			{
				return writeBinary(fp);
			}
			else
			{
				int32_t pos = (int32_t)fp.tellp();
				fp.seekp(mFileOffset);
				writeBinary(fp);
				fp.seekp(pos);
				return (int32_t)fp.tellp();
			}
		}

		int32_t SerializerChunk::readBinary(istream& fp)
		{
			if (mFileOffset >= 0)
			{
				fp.seekg(mFileOffset);
			}
			else
			{
				mFileOffset = (int32_t)fp.tellg();
			}

			readBinaryImpl(fp);

			return (int32_t)fp.tellg();
		}

		int32_t SerializerChunk::rereadLastBinary(istream& fp)
		{
			if (mFileOffset < 0)
			{
				return readBinary(fp);
			}
			else
			{
				int32_t pos = (int32_t)fp.tellg();
				fp.seekg(mFileOffset);
				readBinary(fp);
				fp.seekg(pos);
				return (int32_t)fp.tellg();
			}
		}

		void SerializerChunk::writeText(std::ostream& fp)
		{
			writeTextImpl(fp);
		}

		void SerializerChunk::readText(istream& fp)
		{
			readBinaryImpl(fp);
		}

	} // serialization 
} // WP_NAMESPACE
