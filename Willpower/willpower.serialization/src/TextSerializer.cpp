#include <algorithm>

#include "willpower/common/Exceptions.h"

#include "willpower/serialization/TextSerializer.h"
#include "willpower/serialization/SerializerDirectoryChunk.h"
#include "willpower/serialization/SerializationUtils.h"

namespace WP_NAMESPACE
{
	namespace serialization
	{
		using namespace std;
		using namespace WP_NAMESPACE;

		SerializerChunk* TextSerializer::peekChunk(std::string const& filename, std::string const& name) const
		{
			WP_UNUSED(name);
			WP_UNUSED(filename);
			return nullptr;
		}

		void TextSerializer::serialize(string const& filename)
		{
			try
			{
				ofstream fp;

				fp.open(filename, ios_base::out);

				if (!fp.is_open())
				{
					throw Exception("could not open file for writing.");
				}

				// Write header
				fp << "# Map file version " << VERSION_MAJOR << "." << VERSION_MINOR << endl;
				fp << endl;

				// Write chunks
				for (uint32_t i = 0; i < mChunks.size(); ++i)
				{
					auto chunk = mChunks[i];
					fp << "#" << endl;
					fp << "# " << chunk->getDescription() << endl;
					fp << "#" << endl;
					fp << endl;
					fp << "Chunk " << i << ": " << chunk->getType() << endl;
					fp << endl;
					chunk->writeText(fp);
					fp << endl;
				}

				// Close file at end
				fp.close();

			}
			catch (Exception& e)
			{
				throw Exception("Could not serialize " + filename + " :: " + e.what());
			}
			catch (exception& e)
			{
				throw e;
			}
		}

		void TextSerializer::deserialize(string const& filename)
		{
			WP_UNUSED(filename);
		}

	} // serialization
} // WP_NAMESPACE
