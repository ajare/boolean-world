#pragma once

#include "willpower/common/Platform.h"

#include "willpower/serialization/Serializer.h"

namespace WP_NAMESPACE
{
	namespace serialization
	{

		class WP_SERIALIZATION_API TextSerializer : public Serializer
		{
		public:

			TextSerializer() = default;

			~TextSerializer() = default;

			SerializerChunk* peekChunk(std::string const& filename, std::string const& name) const;

			void serialize(std::string const& filename);

			void deserialize(std::string const& filename);
		};

	} // serialization
} // WP_NAMESPACE
