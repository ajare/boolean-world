#pragma once

#include <exception>
#include <string>


class ExitApplicationException : public std::exception
{
	int mExitCode;

public:

	ExitApplicationException(int exitCode, std::string message)
		: std::exception(message.c_str())
		, mExitCode(exitCode)
	{
	}

	int getExitCode() const
	{
		return mExitCode;
	}

	std::string getMessage() const
	{
		return what();
	}
};