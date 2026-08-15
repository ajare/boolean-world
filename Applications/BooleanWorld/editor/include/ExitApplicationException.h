#pragma once

#include "EditorException.h"


class ExitApplicationException : public EditorException
{
	int mExitCode;

public:

	ExitApplicationException(int exitCode, std::string message)
		: EditorException(message)
		, mExitCode(exitCode)
	{
	}

	int getExitCode() const
	{
		return mExitCode;
	}
};