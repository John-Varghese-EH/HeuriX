#include "heurix/platform.hpp"
#include <windows.h>

class WindowsSysStats : public ISysStats {
    ULARGE_INTEGER prev_idle;
    ULARGE_INTEGER prev_kernel;
    ULARGE_INTEGER prev_user;
    bool init = false;

    uint64_t fileTimeToInt64(const FILETIME& ft) {
        ULARGE_INTEGER li;
        li.LowPart = ft.dwLowDateTime;
        li.HighPart = ft.dwHighDateTime;
        return li.QuadPart;
    }

public:
    SystemStats snapshot() override {
        SystemStats stats{0.0, 0.0, 0, 0};

        FILETIME idleTime, kernelTime, userTime;
        if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
            if (init) {
                uint64_t sys_idle = fileTimeToInt64(idleTime) - prev_idle.QuadPart;
                uint64_t sys_kernel = fileTimeToInt64(kernelTime) - prev_kernel.QuadPart;
                uint64_t sys_user = fileTimeToInt64(userTime) - prev_user.QuadPart;
                
                uint64_t total_sys = sys_kernel + sys_user;
                if (total_sys > 0) {
                    stats.cpu_percent = (total_sys - sys_idle) * 100.0 / total_sys;
                }
            }
            prev_idle.QuadPart = fileTimeToInt64(idleTime);
            prev_kernel.QuadPart = fileTimeToInt64(kernelTime);
            prev_user.QuadPart = fileTimeToInt64(userTime);
            init = true;
        }

        MEMORYSTATUSEX statex;
        statex.dwLength = sizeof(statex);
        if (GlobalMemoryStatusEx(&statex)) {
            stats.mem_percent = statex.dwMemoryLoad;
            stats.mem_total_mb = statex.ullTotalPhys / (1024 * 1024);
            stats.mem_used_mb = (statex.ullTotalPhys - statex.ullAvailPhys) / (1024 * 1024);
        }

        return stats;
    }
};

std::unique_ptr<ISysStats> make_sys_stats() {
    return std::make_unique<WindowsSysStats>();
}
