#pragma once

#include <exception>
#include <string>


namespace bw
{
	namespace core
	{

		class NotImplementedException : public std::exception
		{
		public:

			explicit NotImplementedException(std::string message)
				: std::exception(message.c_str())
			{
			}
		};

	} // core
} // bw