#include "heurix/platform.hpp"
#include <windows.h>
#include <thread>
#include <atomic>
#include <string>

class WindowsFsMonitor : public IFsMonitor {
    HANDLE hDir = INVALID_HANDLE_VALUE;
    std::atomic<bool> run{false};
    std::thread worker;

public:
    ~WindowsFsMonitor() { stop(); }

    bool start(const std::string& path, Callback cb) override {
        hDir = CreateFileA(
            path.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            NULL
        );

        if (hDir == INVALID_HANDLE_VALUE) return false;

        run = true;
        worker = std::thread([this, cb]() {
            char buffer[1024 * 64];
            DWORD bytesReturned;
            OVERLAPPED overlapped;
            memset(&overlapped, 0, sizeof(overlapped));
            overlapped.hEvent = CreateEvent(NULL, FALSE, 0, NULL);

            while (run) {
                if (ReadDirectoryChangesW(
                        hDir, buffer, sizeof(buffer), TRUE,
                        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                        FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
                        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SECURITY,
                        &bytesReturned, &overlapped, NULL)) {
                    
                    if (WaitForSingleObject(overlapped.hEvent, 1000) == WAIT_OBJECT_0) {
                        FILE_NOTIFY_INFORMATION* fni = (FILE_NOTIFY_INFORMATION*)buffer;
                        do {
                            std::wstring wpath(fni->FileName, fni->FileNameLength / sizeof(WCHAR));
                            std::string path_str(wpath.begin(), wpath.end()); // simplified conversion
                            
                            EventType type = EventType::unknown;
                            switch (fni->Action) {
                                case FILE_ACTION_ADDED: type = EventType::create; break;
                                case FILE_ACTION_REMOVED: type = EventType::del; break;
                                case FILE_ACTION_MODIFIED: type = EventType::modify; break;
                                case FILE_ACTION_RENAMED_OLD_NAME: type = EventType::rename; break;
                                case FILE_ACTION_RENAMED_NEW_NAME: type = EventType::rename; break;
                            }
                            
                            uint64_t ms = GetTickCount64();
                            cb({path_str, type, ms});
                            
                            if (fni->NextEntryOffset == 0) break;
                            fni = (FILE_NOTIFY_INFORMATION*)((uint8_t*)fni + fni->NextEntryOffset);
                        } while (true);
                    }
                }
            }
            CloseHandle(overlapped.hEvent);
        });

        return true;
    }

    void stop() override {
        if (run) {
            run = false;
            if (hDir != INVALID_HANDLE_VALUE) {
                CancelIo(hDir);
                CloseHandle(hDir);
                hDir = INVALID_HANDLE_VALUE;
            }
            if (worker.joinable()) worker.join();
        }
    }

    bool running() const override { return run; }
};

std::unique_ptr<IFsMonitor> make_fs_monitor() {
    return std::make_unique<WindowsFsMonitor>();
}
