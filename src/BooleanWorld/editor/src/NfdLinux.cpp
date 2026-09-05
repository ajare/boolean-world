#ifndef _WIN32

#include <nfd/nfd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/wait.h>

namespace {
nfdresult_t runDialog(char const* command, nfdu8char_t** outPath) {
  *outPath = nullptr;
  FILE* pipe = popen(command, "r");
  if (!pipe) return NFD_ERROR;

  std::string path;
  char buffer[1024];
  while (fgets(buffer, sizeof(buffer), pipe)) path += buffer;
  int status = pclose(pipe);
  while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
    path.pop_back();

  if (WIFEXITED(status) && WEXITSTATUS(status) == 0 && !path.empty()) {
    *outPath = strdup(path.c_str());
    return *outPath ? NFD_OKAY : NFD_ERROR;
  }
  return NFD_CANCEL;
}
}  // namespace

extern "C" {
nfdresult_t NFD_Init(void) { return NFD_OKAY; }
void NFD_Quit(void) {}
void NFD_FreePathU8(nfdu8char_t* path) { free(path); }
void NFD_FreePathN(nfdnchar_t* path) { free(path); }

nfdresult_t NFD_OpenDialogU8(nfdu8char_t** outPath,
                             nfdu8filteritem_t const*, nfdfiltersize_t,
                             nfdu8char_t const*) {
  return runDialog("zenity --file-selection 2>/dev/null", outPath);
}

nfdresult_t NFD_SaveDialogU8(nfdu8char_t** outPath,
                             nfdu8filteritem_t const*, nfdfiltersize_t,
                             nfdu8char_t const*, nfdu8char_t const*) {
  return runDialog("zenity --file-selection --save --confirm-overwrite 2>/dev/null",
                   outPath);
}
}
#endif
