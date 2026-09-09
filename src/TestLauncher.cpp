// Launch CTest executables from a process that has disabled Windows error UI.
// This must remain free of project DLL dependencies: it also has to run when a
// test executable cannot start because one of its DLLs is missing.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <string>

namespace {

constexpr DWORD statusInvalidImageFormat = 0xC000007B;
constexpr DWORD statusDllNotFound = 0xC0000135;
constexpr DWORD statusDllInitFailed = 0xC0000142;
constexpr DWORD statusEntryPointNotFound = 0xC0000139;

void reportLoaderFailure(DWORD exitCode) {
  wchar_t const* reason = nullptr;
  switch (exitCode) {
    case statusInvalidImageFormat:
      reason = L"invalid executable or DLL image (often an architecture mismatch)";
      break;
    case statusDllNotFound:
      reason = L"required DLL not found";
      break;
    case statusDllInitFailed:
      reason = L"DLL initialization failed";
      break;
    case statusEntryPointNotFound:
      reason = L"required DLL entry point not found";
      break;
    default:
      return;
  }

  std::fwprintf(stderr,
                L"Test failed during Windows loader startup: %ls "
                L"(status 0x%08lX). Check the test target's runtime DLL "
                L"dependencies and staging.\n",
                reason, exitCode);
}

std::wstring quoteArgument(std::wstring const& argument) {
  std::wstring quoted = L"\"";
  std::size_t backslashes = 0;

  for (auto const character : argument) {
    if (character == L'\\') {
      ++backslashes;
      continue;
    }

    if (character == L'\"') {
      quoted.append(backslashes * 2 + 1, L'\\');
      quoted.push_back(character);
    } else {
      quoted.append(backslashes, L'\\');
      quoted.push_back(character);
    }
    backslashes = 0;
  }

  quoted.append(backslashes * 2, L'\\');
  quoted.push_back(L'\"');
  return quoted;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) {
    std::fputs("usage: boolean_world_test_launcher <test> [arguments...]\n",
               stderr);
    return 2;
  }

  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
               SEM_NOOPENFILEERRORBOX);

  std::wstring commandLine;
  for (int i = 1; i < argc; ++i) {
    if (!commandLine.empty()) {
      commandLine.push_back(L' ');
    }
    commandLine += quoteArgument(argv[i]);
  }

  STARTUPINFOW startupInfo{};
  startupInfo.cb = sizeof(startupInfo);
  PROCESS_INFORMATION processInfo{};
  if (!CreateProcessW(argv[1], commandLine.data(), nullptr, nullptr, TRUE, 0,
                      nullptr, nullptr, &startupInfo, &processInfo)) {
    std::fwprintf(stderr, L"Could not start test '%ls' (Windows error %lu)\n",
                  argv[1], GetLastError());
    return 1;
  }

  auto const waitResult = WaitForSingleObject(processInfo.hProcess, INFINITE);
  DWORD exitCode = 1;
  if (waitResult == WAIT_OBJECT_0) {
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
  } else {
    std::fprintf(stderr, "Could not wait for test process (Windows error %lu)\n",
                 GetLastError());
  }

  reportLoaderFailure(exitCode);
  CloseHandle(processInfo.hThread);
  CloseHandle(processInfo.hProcess);
  return static_cast<int>(exitCode);
}
