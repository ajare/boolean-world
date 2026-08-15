#pragma once

#include "willpower/common/Platform.h"

#include "willpower/serialization/Serializer.h"

namespace WP_NAMESPACE
{
	namespace serialization
	{

		class WP_SERIALIZATION_API BinarySerializer : public Serializer
		{
			uint32_t writeBinaryHeader(std::ostream& fp, uint32_t mapFormat);

			ReadHeader readBinaryHeader(std::istream& fp) const;

		public:

			BinarySerializer() = default;

			~BinarySerializer() = default;

			SerializerChunk* peekChunk(std::string const& filename, std::string const& name) const;

			void serialize(std::string const& filename);

			void deserialize(std::string const& filename);
		};

	} // serialization
} // WP_NAMESPACE
