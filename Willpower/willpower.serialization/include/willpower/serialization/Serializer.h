#pragma once

#include <functional>
#include <map>
#include <vector>

#include "willpower/common/Platform.h"

#include "willpower/serialization/Platform.h"
#include "willpower/serialization/SerializerChunk.h"

namespace WP_NAMESPACE
{
	namespace serialization
	{

		class WP_SERIALIZATION_API Serializer
		{
		protected:

			static const uint32_t MAGIC = ('p' << 0) + ('b' << 8) + ('c' << 16) + ('o' << 24);

			static const uint16_t VERSION_MAJOR = 0;

			static const uint16_t VERSION_MINOR = 1;

		public:

			typedef std::function<SerializerChunk*(std::string, std::string)> SerializerChunkFactory;

		protected:

			struct ReadHeader
			{
				uint32_t magic;
				uint16_t versionMajor;
				uint16_t versionMinor;
				uint32_t mapFormat;
				int32_t directoryChunkOffset;
			};

		protected:

			std::map<std::string, SerializerChunkFactory> mChunkFactories;

			std::vector<SerializerChunk*> mChunks;

		protected:

			void addDefaultChunkFactories(uint32_t vertexComponents);

			void deleteChunks();

		public:

			Serializer() = default;

			virtual ~Serializer();

			void addChunkFactory(std::string const& name, SerializerChunkFactory factory);

			SerializerChunk* createChunk(std::string const& name, std::string const& type, std::string const& description);

			void addChunk(SerializerChunk* chunk);

			int getNumChunks() const;

			SerializerChunk const* getChunkByName(std::string const& name) const;

			SerializerChunk const* getChunkByIndex(int index) const;

			std::vector<SerializerChunk const*> getChunksByDescription(std::string const& description) const;

			virtual SerializerChunk* peekChunk(std::string const& filename, std::string const& name) const = 0;

			SerializerChunk* peekChunk(std::string const& filename, uint32_t index) const;

			virtual void serialize(std::string const& filename) = 0;

			virtual void deserialize(std::string const& filename) = 0;
		};

	} // serialization
} // WP_NAMESPACE
