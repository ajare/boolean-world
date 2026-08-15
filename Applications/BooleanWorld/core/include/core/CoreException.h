#pragma once

#include <exception>
#include <string>


namespace bw
{
	namespace core
	{

		class CoreException : public std::exception
		{
			std::string mMessage;

		public:

			explicit CoreException(std::string message)
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