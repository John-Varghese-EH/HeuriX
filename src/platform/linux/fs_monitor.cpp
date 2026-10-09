#include <iostream>
#include "heurix/platform.hpp"
#include <thread>
#include <atomic>
#include <sys/inotify.h>
#include <unistd.h>
#include <filesystem>
#include <chrono>
#include <vector>
#include <unordered_map>
#include <mutex>

class LinuxFsMonitor : public IFsMonitor {
    int fd = -1;
    std::atomic<bool> run{false};
    std::thread worker;
    std::mutex watches_mutex;
    std::unordered_map<int, std::string> watch_descriptors;
    Callback callback;

public:
    ~LinuxFsMonitor() { stop(); }

    bool start(const std::string& path, Callback cb) override {
        fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (fd < 0) return false;

        callback = cb;
        watch_descriptors.clear();

        // Add recursive watches starting from base path
        add_recursive_watches(path);

        run = true;
        worker = std::thread([this]() {
            char buf[32768]
                __attribute__ ((aligned(__alignof__(struct inotify_event))));

            while (run) {
                ssize_t len = read(fd, buf, sizeof(buf));
                if (len > 0) {
                    process_events(buf, len);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
        return true;
    }

    void stop() override {
        if (run) {
            run = false;
            if (worker.joinable()) worker.join();
            if (fd >= 0) close(fd);
            fd = -1;
        }
    }

    bool running() const override { return run; }

private:
    void add_recursive_watches(const std::string& base_path) {
        try {
            // Watch the base directory
            add_watch(base_path);

            // Recursively watch subdirectories
            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                     base_path, std::filesystem::directory_options::skip_permission_denied)) {
                if (entry.is_directory()) {
                    add_watch(entry.path().string());
                }
            }
        } catch (const std::exception& e) {
            // Silently continue - some directories may be inaccessible
        }
    }

    void add_watch(const std::string& path) {
        if (fd < 0) return;

        int wd = inotify_add_watch(fd, path.c_str(),
            IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM);

        if (wd >= 0) {
            std::lock_guard<std::mutex> lock(watches_mutex);
            watch_descriptors[wd] = path;
        }
    }

    void process_events(char* buf, ssize_t len) {
        std::lock_guard<std::mutex> lock(watches_mutex);

        for (char* ptr = buf; ptr < buf + len; ) {
            struct inotify_event* event = reinterpret_cast<struct inotify_event*>(ptr);
            if (event->len) {
                EventType type = EventType::unknown;
                if (event->mask & IN_CLOSE_WRITE) type = EventType::modify;
                else if (event->mask & IN_CREATE) type = EventType::create;
                else if (event->mask & IN_DELETE) type = EventType::del;
                else if (event->mask & IN_MOVED_TO || event->mask & IN_MOVED_FROM) type = EventType::rename;

                std::string watch_path;
                auto it = watch_descriptors.find(event->wd);
                if (it != watch_descriptors.end()) {
                    watch_path = it->second;
                } else {
                    watch_path = ".";
                }

                // Ensure path ends with /
                if (!watch_path.empty() && watch_path.back() != '/') {
                    watch_path += '/';
                }

                uint64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

                callback({watch_path + event->name, type, ms});

                // If a new directory was created, add a watch for it
                if (event->mask & IN_CREATE && (event->mask & IN_ISDIR)) {
                    std::string new_dir = watch_path + event->name;
                    watch_new_dir(new_dir);
                }
            }
            ptr += sizeof(struct inotify_event) + event->len;
        }
    }

    void unlock_and_watch(const std::string& path) {
        watches_mutex.unlock();
        add_recursive_watches(path);
        watches_mutex.lock();
    }

    // Non-locking version called from process_events (already holds lock)
    void watch_new_dir(const std::string& path) {
        if (fd < 0) return;
        int wd = inotify_add_watch(fd, path.c_str(),
            IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM);
        if (wd >= 0) {
            std::cout << "[HeuriX] Added watch for new dir: " << path << std::endl;
            watch_descriptors[wd] = path;
        } else {
            std::cerr << "[HeuriX] Failed to add watch for new dir: " << path << " error: " << errno << std::endl;
        }
    }
};

std::unique_ptr<IFsMonitor> make_fs_monitor() {
    return std::make_unique<LinuxFsMonitor>();
}