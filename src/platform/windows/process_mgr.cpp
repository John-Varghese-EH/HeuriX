#include "heurix/platform.hpp"
#include <windows.h>
#include <tlhelp32.h>
#include <string>

// Definitions for NtSuspendProcess (undocumented but commonly used for this)
typedef LONG(NTAPI* NtSuspendProcess)(IN HANDLE ProcessHandle);

class WindowsProcessMgr : public IProcessMgr {
public:
    int resolve_pid(const std::string& /*file_path*/) override {
        // Dummy implementation for brevity
        return -1;
    }

    bool suspend(int pid) override {
        HANDLE hProcess = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
        if (!hProcess) return false;

        NtSuspendProcess pfnNtSuspendProcess = (NtSuspendProcess)GetProcAddress(
            GetModuleHandleA("ntdll"), "NtSuspendProcess");

        bool success = false;
        if (pfnNtSuspendProcess) {
            success = (pfnNtSuspendProcess(hProcess) == 0);
        }
        
        CloseHandle(hProcess);
        return success;
    }

    bool terminate(int pid) override {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (!hProcess) return false;

        bool success = TerminateProcess(hProcess, 1);
        CloseHandle(hProcess);
        return success;
    }

    std::string process_name(int pid) override {
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) return "";

        PROCESSENTRY32 pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32);

        std::string name;
        if (Process32First(hSnapshot, &pe32)) {
            do {
                if (pe32.th32ProcessID == static_cast<DWORD>(pid)) {
                    name = pe32.szExeFile;
                    break;
                }
            } while (Process32Next(hSnapshot, &pe32));
        }

        CloseHandle(hSnapshot);
        return name;
    }

    std::vector<int> children_of(int /*pid*/) override { return {}; }
    bool kill_tree(int pid) override { return terminate(pid); }
    bool quarantine_file(const std::string& /*file_path*/, std::string& /*out*/) override { return false; }
    bool write_evidence(const Alert& /*alert*/) override { return false; }
};

std::unique_ptr<IProcessMgr> make_process_mgr(const std::string& /*watch_dir*/) {
    return std::make_unique<WindowsProcessMgr>();
}
