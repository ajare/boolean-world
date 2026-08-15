#pragma once

#include <exception>
#include <string>


namespace bw
{
	namespace core
	{

		class SerializationException : public std::exception
		{
			std::string mMessage;

		public:

			explicit SerializationException(std::string message)
				: std::exception(message.c_str())
				, mMessage(message)
			{
			}

			std::string const& getMessage() const
			{
				return mMessage;
			}
		};

	} // core
} // bw