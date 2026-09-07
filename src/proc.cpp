#include "gpudirectfabric/proc.hpp"

#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace gpudirectfabric {
namespace detail {

ProcHandle spawn_process(const std::string& exe, const std::vector<std::string>& args) {
  std::string cmd = "\"" + exe + "\"";
  for (const auto& a : args) {
    cmd += " \"" + a + "\"";
  }
  std::vector<char> cmdline(cmd.begin(), cmd.end());
  cmdline.push_back(0);
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
  if (!ok || pi.hProcess == nullptr) return {};
  ProcHandle r;
  r.pid = static_cast<std::uint64_t>(pi.dwProcessId);
  r.handle = static_cast<void*>(pi.hProcess);
  CloseHandle(pi.hThread);
  return r;
}

bool terminate_process(const ProcHandle& p) {
  if (p.handle == nullptr) return false;
  HANDLE h = static_cast<HANDLE>(p.handle);
  BOOL ok = TerminateProcess(h, 1);
  WaitForSingleObject(h, INFINITE);
  CloseHandle(h);
  return ok != 0;
}

}  // namespace detail
}  // namespace gpudirectfabric
