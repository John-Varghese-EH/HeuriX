#include "heurix/platform.hpp"
#include <signal.h>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <dirent.h>
#include <thread>
#include <unistd.h>
#include <sys/stat.h>
#include <algorithm>
#include <cstring>
#include <queue>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cctype>

static std::string sanitise_name(const std::string& s) {
    std::string out = s;
    for (char& c : out) if (c == '/' || c == '\0') c = '_';
    return out;
}

static int read_ppid(int pid) {
    std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
    if (!stat) return -1;
    std::string line;
    std::getline(stat, line);
    size_t rp = line.rfind(')');
    if (rp == std::string::npos) return -1;
    std::istringstream iss(line.substr(rp + 1));
    char state; int ppid;
    if (!(iss >> state >> ppid)) return -1;
    return ppid;
}

class LinuxProcessMgr : public IProcessMgr {
    std::string watch_dir_;
public:
    explicit LinuxProcessMgr(std::string watch_dir = "") : watch_dir_(std::move(watch_dir)) {}

    void set_watch_dir(const std::string& d) { watch_dir_ = d; }

    int resolve_pid(const std::string& file_path) override {
        std::filesystem::path abs_path = std::filesystem::absolute(file_path);
        std::string target_path = abs_path.string();
        DIR* proc_dir = opendir("/proc");
        if (!proc_dir) return -1;
        struct dirent* entry;
        while ((entry = readdir(proc_dir)) != nullptr) {
            if (entry->d_type != DT_DIR) continue;
            const char* name = entry->d_name;
            if (!std::all_of(name, name + strlen(name), ::isdigit)) continue;
            int pid = std::stoi(name);
            if (pid <= 1) continue;
            std::string fd_path = "/proc/" + std::to_string(pid) + "/fd/";
            DIR* fd_dir = opendir(fd_path.c_str());
            if (!fd_dir) continue;
            struct dirent* fd_entry;
            while ((fd_entry = readdir(fd_dir)) != nullptr) {
                if (fd_entry->d_type != DT_LNK) continue;
                std::string link_path = fd_path + fd_entry->d_name;
                char resolved[PATH_MAX];
                ssize_t len = readlink(link_path.c_str(), resolved, sizeof(resolved) - 1);
                if (len > 0) {
                    resolved[len] = '\0';
                    if (std::string(resolved) == target_path) {
                        closedir(fd_dir); closedir(proc_dir); return pid;
                    }
                    if (target_path != "/" && std::string(resolved).find(target_path) == 0) {
                        if (std::string(resolved) == target_path ||
                            std::string(resolved).at(target_path.length()) == '/') {
                            closedir(fd_dir); closedir(proc_dir); return pid;
                        }
                    }
                }
            }
            closedir(fd_dir);
        }
        closedir(proc_dir);
        return -1;
    }

    bool suspend(int pid) override {
        if (pid <= 1) return false;
        return kill(pid, SIGSTOP) == 0;
    }

    bool terminate(int pid) override {
        if (pid <= 1) return false;
        if (kill(pid, SIGTERM) == 0) {
            for (int i = 0; i < 10; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (kill(pid, 0) != 0) return true;
            }
            return kill(pid, SIGKILL) == 0;
        }
        return false;
    }

    std::string process_name(int pid) override {
        std::ifstream comm("/proc/" + std::to_string(pid) + "/comm");
        std::string name;
        if (comm >> name) return name;
        return "";
    }

    std::vector<int> children_of(int pid) override {
        std::vector<int> kids;
        if (pid <= 1) return kids;
        DIR* proc_dir = opendir("/proc");
        if (!proc_dir) return kids;
        struct dirent* entry;
        while ((entry = readdir(proc_dir)) != nullptr) {
            if (entry->d_type != DT_DIR) continue;
            const char* name = entry->d_name;
            if (!std::all_of(name, name + strlen(name), ::isdigit)) continue;
            int other = std::stoi(name);
            if (other <= 1 || other == pid) continue;
            int ppid = read_ppid(other);
            if (ppid == pid) kids.push_back(other);
        }
        closedir(proc_dir);
        return kids;
    }

    bool kill_tree(int pid) override {
        if (pid <= 1) return false;
        std::vector<int> all;
        std::queue<int> q;
        q.push(pid);
        while (!q.empty()) {
            int cur = q.front(); q.pop();
            all.push_back(cur);
            for (int child : children_of(cur)) q.push(child);
        }
        for (int p : all) kill(p, SIGSTOP);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        for (int p : all) kill(p, SIGTERM);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        bool ok = true;
        for (int p : all) {
            if (kill(p, 0) == 0) {
                if (kill(p, SIGKILL) != 0) ok = false;
            }
        }
        return ok;
    }

    bool quarantine_file(const std::string& file_path, std::string& out_quarantine_path) override {
        std::filesystem::path src(file_path);
        std::error_code ec;
        if (!std::filesystem::exists(src, ec)) return false;
        std::string base_dir = watch_dir_.empty() ? std::string(".") : watch_dir_;
        std::filesystem::path qdir = std::filesystem::path(base_dir) / ".heurix-quarantine";
        std::filesystem::create_directories(qdir, ec);
        chmod(qdir.c_str(), 0700);

        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::string fname = std::to_string(now_ms) + "-" + sanitise_name(src.filename().string());
        std::filesystem::path dst = qdir / fname;

        std::filesystem::rename(src, dst, ec);
        if (ec) {
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) return false;
            std::filesystem::remove(src, ec);
            if (ec) { std::filesystem::remove(dst, ec); return false; }
        }
        chmod(dst.c_str(), 0400);
        out_quarantine_path = dst.string();
        return true;
    }

    bool write_evidence(const Alert& alert) override {
        std::string base_dir = watch_dir_.empty() ? std::string(".") : watch_dir_;
        std::filesystem::path edir = std::filesystem::path(base_dir) / ".heurix-evidence";
        std::error_code ec;
        std::filesystem::create_directories(edir, ec);
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::filesystem::path fpath = edir / (std::to_string(now_ms) + ".jsonl");
        std::ofstream out(fpath, std::ios::app);
        if (!out) return false;
        out << alert.to_json() << "\n";
        return true;
    }
};

std::unique_ptr<IProcessMgr> make_process_mgr(const std::string& watch_dir) {
    return std::make_unique<LinuxProcessMgr>(watch_dir);
}
