// Process-wide Windows error reporting must be non-interactive.  This source is
// linked into every executable by bw_target_defaults(), including test
// executables, so a CRT assertion or unhandled fault cannot block automation
// behind a native dialog.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifdef _DEBUG
#include <crtdbg.h>
#endif

namespace {

class NonInteractiveErrorMode {
public:
  NonInteractiveErrorMode() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
                 SEM_NOOPENFILEERRORBOX);

#ifdef _DEBUG
    setReportMode(_CRT_WARN);
    setReportMode(_CRT_ERROR);
    setReportMode(_CRT_ASSERT);
#endif
  }

private:
#ifdef _DEBUG
  static void setReportMode(int reportType) {
    _CrtSetReportMode(reportType, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(reportType, _CRTDBG_FILE_STDERR);
  }
#endif
};

// Run before ordinary translation-unit globals, whose initializers can report
// errors before main() (including test registration).
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4073)  // intentional non-default init segment
#pragma init_seg(lib)
#endif
NonInteractiveErrorMode nonInteractiveErrorMode;
#ifdef _MSC_VER
#pragma warning(pop)
#endif

}  // namespace
#endif
