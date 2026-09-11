#pragma once
#include "types.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

class IFsMonitor {
public:
    virtual ~IFsMonitor() = default;
    using Callback = std::function<void(FsEvent)>;
    virtual bool start(const std::string& path, Callback cb) = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;
};

class IProcessMgr {
public:
    virtual ~IProcessMgr() = default;
    virtual int resolve_pid(const std::string& file_path) = 0;
    virtual bool suspend(int pid) = 0;
    virtual bool terminate(int pid) = 0;
    virtual std::string process_name(int pid) = 0;
    virtual std::vector<int> children_of(int pid) = 0;
    virtual bool kill_tree(int pid) = 0;
    virtual bool quarantine_file(const std::string& file_path, std::string& out_quarantine_path) = 0;
    virtual bool write_evidence(const Alert& alert) = 0;
};

class ISysStats {
public:
    virtual ~ISysStats() = default;
    virtual SystemStats snapshot() = 0;
};

std::unique_ptr<IFsMonitor> make_fs_monitor();
std::unique_ptr<IProcessMgr> make_process_mgr(const std::string& watch_dir = "");
std::unique_ptr<ISysStats> make_sys_stats();
