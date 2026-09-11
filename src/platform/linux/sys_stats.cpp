#include "heurix/platform.hpp"
#include <fstream>
#include <string>
#include <filesystem>
#include <chrono>

class LinuxSysStats : public ISysStats {
    uint64_t prev_idle = 0;
    uint64_t prev_total = 0;
    uint64_t prev_read_sectors = 0;
    uint64_t prev_write_sectors = 0;
    std::chrono::steady_clock::time_point prev_io_time{};

public:
    SystemStats snapshot() override {
        SystemStats stats{0.0, 0.0, 0, 0, 0.0, 0.0};

        // CPU usage
        std::ifstream stat_file("/proc/stat");
        std::string cpu;
        uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
        if (stat_file >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal) {
            uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal;
            uint64_t idle_all = idle + iowait;

            if (prev_total != 0) {
                uint64_t totald = total - prev_total;
                uint64_t idled = idle_all - prev_idle;
                if (totald > 0) {
                    stats.cpu_percent = (totald - idled) * 100.0 / totald;
                }
            }
            prev_total = total;
            prev_idle = idle_all;
        }

        // Memory usage
        std::ifstream meminfo("/proc/meminfo");
        std::string key;
        uint64_t val;
        std::string unit;
        uint64_t mem_total = 0, mem_avail = 0;
        while (meminfo >> key >> val >> unit) {
            if (key == "MemTotal:") mem_total = val;
            else if (key == "MemAvailable:") mem_avail = val;
        }
        if (mem_total > 0) {
            stats.mem_total_mb = mem_total / 1024;
            stats.mem_used_mb = (mem_total - mem_avail) / 1024;
            stats.mem_percent = (double)(mem_total - mem_avail) / mem_total * 100.0;
        }

        // Disk I/O stats
        // /proc/diskstats format per line:
        //   major minor name reads_completed reads_merged sectors_read time_reading
        //   writes_completed writes_merged sectors_written time_writing ...
        std::ifstream diskstats("/proc/diskstats");
        unsigned major, minor;
        std::string name;
        uint64_t r_comp, r_merge, r_sect, r_time, w_comp, w_merge, w_sect, w_time;
        uint64_t total_read = 0, total_write = 0;

        while (diskstats >> major >> minor >> name
                      >> r_comp >> r_merge >> r_sect >> r_time
                      >> w_comp >> w_merge >> w_sect >> w_time) {
            // Only count whole-disk block devices (skip partitions like nvme0n1p1)
            bool is_disk = name.rfind("sd", 0) == 0
                        || name.rfind("vd", 0) == 0
                        || name.rfind("nvme", 0) == 0
                        || name.rfind("hd", 0) == 0;
            if (is_disk) {
                total_read += r_sect;
                total_write += w_sect;
            }
        }

        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - prev_io_time).count();

        if (prev_io_time.time_since_epoch().count() != 0 && dt > 0.0) {
            uint64_t read_delta = total_read - prev_read_sectors;
            uint64_t write_delta = total_write - prev_write_sectors;
            // Sectors are 512 bytes; convert to MB/s
            const double bytes_per_mb = 1024.0 * 1024.0;
            stats.io_read_mb = (read_delta * 512.0 / bytes_per_mb) / dt;
            stats.io_write_mb = (write_delta * 512.0 / bytes_per_mb) / dt;
        }
        prev_read_sectors = total_read;
        prev_write_sectors = total_write;
        prev_io_time = now;

        return stats;
    }
};

std::unique_ptr<ISysStats> make_sys_stats() {
    return std::make_unique<LinuxSysStats>();
}
