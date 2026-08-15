#include <algorithm>

#include "willpower/common/Exceptions.h"

#include "willpower/serialization/Serializer.h"

namespace WP_NAMESPACE
{
	namespace serialization
	{
		using namespace std;
		using namespace WP_NAMESPACE;

		Serializer::~Serializer()
		{
			deleteChunks();
		}

		void Serializer::addDefaultChunkFactories(uint32_t vertexComponents)
		{
			WP_UNUSED(vertexComponents);

			/*
			addChunkFactory("Info",
				[](string desc, int index) -> SerializerChunk* { return new SerializerInfoChunk(desc, index); });

			addChunkFactory("Vertex",
				[vertexComponents](string desc, int index) -> SerializerChunk* { return new SerializerVertexChunk(vertexComponents, desc, index); });

			addChunkFactory("Mesh",
				[](string desc, int index) -> SerializerChunk* { return new SerializerMeshChunk(desc, index); });
			*/
		}

		void Serializer::addChunkFactory(string const& name, SerializerChunkFactory factory)
		{
			if (mChunkFactories.find(name) != mChunkFactories.end())
			{
				throw Exception("Chunk factory '" + name + "' already registered.");
			}

			mChunkFactories[name] = factory;
		}

		void Serializer::deleteChunks()
		{
			for (auto chunk: mChunks)
			{
				if (chunk->ownedBySerializer())
				{
					delete chunk;
				}
			}

			mChunks.clear();
		}

		SerializerChunk* Serializer::createChunk(string const& name, string const& type, string const& description)
		{
			if (getChunkByName(name))
			{
				throw Exception("chunk with name '" + name + "' already created.");
			}

			if (mChunkFactories.find(type) == mChunkFactories.end())
			{
				throw Exception("could not create chunk of type '" + type + "'.");
			}

			auto chunk = mChunkFactories[type](name, description);

			mChunks.push_back(chunk);
			return chunk;
		}

		void Serializer::addChunk(SerializerChunk* chunk)
		{
			if (getChunkByName(chunk->getName()))
			{
				throw Exception("chunk with name '" + chunk->getName() + "' already added.");
			}

			mChunks.push_back(chunk);
		}

		int Serializer::getNumChunks() const
		{
			return (int)mChunks.size();
		}

		SerializerChunk const* Serializer::getChunkByName(string const& name) const
		{
			auto it = find_if(mChunks.begin(), mChunks.end(), [name](SerializerChunk* chunk) -> bool { return chunk->getName() == name; });
			return it == mChunks.end() ? nullptr : *it;
		}

		SerializerChunk const* Serializer::getChunkByIndex(int index) const
		{
			return mChunks[index];
		}

		vector<SerializerChunk const*> Serializer::getChunksByDescription(string const& description) const
		{
			vector<SerializerChunk const*> chunks;

			for (auto chunk : mChunks)
			{
				if (chunk->getDescription() == description)
				{
					chunks.push_back(chunk);
				}
			}

			return chunks;
		}

		SerializerChunk* Serializer::peekChunk(string const& filename, uint32_t index) const
		{
			return peekChunk(filename, mChunks[index]->getName());
		}
		
	} // serialization
} // WP_NAMESPACE
