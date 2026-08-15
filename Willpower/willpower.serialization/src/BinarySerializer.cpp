#include <algorithm>

#include "willpower/common/Exceptions.h"

#include "willpower/serialization/BinarySerializer.h"
#include "willpower/serialization/SerializerDirectoryChunk.h"
#include "willpower/serialization/SerializationUtils.h"

namespace WP_NAMESPACE
{
	namespace serialization
	{
		using namespace std;
		using namespace WP_NAMESPACE;

		uint32_t BinarySerializer::writeBinaryHeader(ostream& fp, uint32_t mapFormat)
		{
			// Write magic
			fp.write((char const*)&MAGIC, sizeof(uint32_t));

			// Write major version
			fp.write((char const*)&VERSION_MAJOR, sizeof(uint16_t));

			// Write minor version
			fp.write((char const*)&VERSION_MINOR, sizeof(uint16_t));

			// Write map format
			fp.write((char const*)&mapFormat, sizeof(uint32_t));

			// Write dummy directory chunk offset
			uint32_t directoryChunkOffsetLocation = (uint32_t)fp.tellp();

			uint32_t directoryChunkOffset = 0;

			fp.write((char const*)&directoryChunkOffset, sizeof(uint32_t));

			return directoryChunkOffsetLocation;
		}

		Serializer::ReadHeader BinarySerializer::readBinaryHeader(istream& fp) const
		{
			ReadHeader header;

			// Read magic
			fp.read((char*)&header.magic, sizeof(uint32_t));

			// Read major version
			fp.read((char*)&header.versionMajor, sizeof(uint16_t));

			// Read minor version
			fp.read((char*)&header.versionMinor, sizeof(uint16_t));

			// Read map format
			fp.read((char*)&header.mapFormat, sizeof(uint32_t));

			// Read directory chunk offset
			fp.read((char*)&header.directoryChunkOffset, sizeof(int32_t));

			// Error check
			if (header.magic != MAGIC)
			{
				throw Exception("bad magic.");
			}

			if (header.versionMajor > VERSION_MAJOR || header.versionMinor > VERSION_MINOR)
			{
				throw Exception("version of file is higher than version of engine.");
			}

			return header;
		}

		SerializerChunk* BinarySerializer::peekChunk(std::string const& filename, std::string const& name) const
		{
			try
			{
				ifstream fp;

				fp.open(filename, ios_base::in | ios_base::binary);

				if (!fp.is_open())
				{
					throw Exception("could not open file for reading.");
				}

				// Read header
				ReadHeader header = readBinaryHeader(fp);

				// Read directory chunk
				SerializerDirectoryChunk directoryChunk("Chunk directory");

				fp.seekg(header.directoryChunkOffset);
				directoryChunk.readBinary(fp);

				SerializerChunk* chunk = nullptr;
				for (int i = 0; i < directoryChunk.getNumEntries(); ++i)
				{
					SerializerDirectoryChunk::Entry const& entry = directoryChunk.getEntry(i);
					
					if (entry.name == name)
					{
						auto it = mChunkFactories.find(entry.type);
						if (it == mChunkFactories.end())
						{
							throw Exception("could not create chunk of type '" + entry.type + "'.");
						}
						
						chunk = it->second(entry.name, entry.description);

						fp.seekg(entry.offset);
						chunk->readBinary(fp);
						break;
					}
				}
				
				// Close file at end
				fp.close();
				return chunk;
			}
			catch (Exception& e)
			{
				throw Exception("Could not deserialize " + filename + " :: " + e.what());
			}
			catch (exception& e)
			{
				throw e;
			}
		}

		void BinarySerializer::serialize(string const& filename)
		{
			try
			{
				ofstream fp;

				fp.open(filename, ios_base::out | ios_base::binary);

				if (!fp.is_open())
				{
					throw Exception("could not open file for writing.");
				}

				// Write header
				uint32_t mapFormat = 0;
				int directoryChunkOffsetLocation = writeBinaryHeader(fp, mapFormat);

				SerializerDirectoryChunk directoryChunk("Chunk directory");

				int chunkOffset;
				for (uint32_t i = 0; i < mChunks.size(); ++i)
				{
					auto chunk = mChunks[i];

					chunkOffset = (int)fp.tellp();
					chunk->writeBinary(fp);

					directoryChunk.addChunk(
						chunk->getName(),
						chunk->getType(),
						chunk->getDescription(),
						i,
						chunkOffset);
				}

				// Write directory chunk last of all.
				chunkOffset = (int)fp.tellp();
				directoryChunk.writeBinary(fp);

				// Set directory chunk location
				fp.seekp(directoryChunkOffsetLocation);
				SerializationUtils::writeUint32(fp, chunkOffset);

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

		void BinarySerializer::deserialize(string const& filename)
		{
			deleteChunks();

			try
			{
				ifstream fp;

				fp.open(filename, ios_base::in | ios_base::binary);

				if (!fp.is_open())
				{
					throw Exception("could not open file for reading.");
				}

				// Read header
				ReadHeader header = readBinaryHeader(fp);

				// Read directory chunk
				SerializerDirectoryChunk directoryChunk("Chunk directory");

				fp.seekg(header.directoryChunkOffset);
				directoryChunk.readBinary(fp);

				for (int i = 0; i < directoryChunk.getNumEntries(); ++i)
				{
					SerializerDirectoryChunk::Entry const& entry = directoryChunk.getEntry(i);

					fp.seekg(entry.offset);

					SerializerChunk* chunk = nullptr;
					try
					{
						chunk = createChunk(entry.name, entry.type, entry.description);
					}
					catch (exception)
					{
						// Ignore chunk.  We may get errors later on if other chunks
						// rely on this chunk.
						string warning = "Ignoring chunk '" + entry.description + "' of type '" + entry.type + "' because there is no reader specified for it.";
						// ...
					}

					if (chunk)
					{
						chunk->readBinary(fp);
					}
				}

				// Close file at end
				fp.close();

			}
			catch (Exception& e)
			{
				throw Exception("Could not deserialize " + filename + " :: " + e.what());
			}
			catch (exception& e)
			{
				throw e;
			}
		}

	} // serialization
} // WP_NAMESPACE
