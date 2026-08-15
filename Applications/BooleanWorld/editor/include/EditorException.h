#pragma once

#include <exception>
#include <string>


class EditorException : public std::exception
{
	std::string mMessage;

public:

	explicit EditorException(std::string message)
		: std::exception(message.c_str())
		, mMessage(message)
	{
	}

	std::string const& getMessage() const
	{
		return mMessage;
	}
};
