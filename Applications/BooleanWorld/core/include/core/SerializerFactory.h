#pragma once

#include "Serializer.h"

namespace bw
{
	namespace core
	{

		class SerializerFactory
		{
		public:

			virtual ~SerializerFactory() = default;

			virtual Serializer* create(std::string const& target) = 0;

			virtual std::string description() = 0;

			virtual std::string extension() = 0;
		};

	} // core
} // bw