#include "monitoring/resource_monitor.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <cstring>
#include <sys/statvfs.h>
#include <unistd.h>

namespace slonana {
namespace monitoring {

ResourceMonitor::ResourceMonitor(const ResourceMonitorConfig& config)
    : config_(config)
    , running_(false)
    , resource_pressure_(false) {
    
    if (config_.memory_headroom_mb == 0) {
        config_.memory_headroom_mb = 512; // Default 512MB
    }
}

ResourceMonitor::~ResourceMonitor() {
    stop();
}

bool ResourceMonitor::start() {
    if (running_.load()) {
        return true;
    }
    
    running_.store(true);
    resource_pressure_.store(false);
    
    try {
        monitor_thread_ = std::make_unique<std::thread>(&ResourceMonitor::monitor_loop, this);
        
        if (config_.enable_automatic_logging) {
            log_resource_usage("Resource monitoring started");
        }
        
        return true;
    } catch (const std::exception& e) {
        running_.store(false);
        std::cerr << "Failed to start resource monitor: " << e.what() << std::endl;
        return false;
    }
}

void ResourceMonitor::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    if (monitor_thread_ && monitor_thread_->joinable()) {
        monitor_thread_->join();
    }
    
    if (config_.enable_automatic_logging) {
        log_resource_usage("Resource monitoring stopped");
    }
}

ResourceUsage ResourceMonitor::get_current_usage() {
    ResourceUsage usage;
    usage.timestamp = std::chrono::system_clock::now();
    
    // Get memory information
    if (get_memory_info(usage.total_memory_bytes, usage.available_memory_bytes)) {
        usage.used_memory_bytes = usage.total_memory_bytes - usage.available_memory_bytes;
        if (usage.total_memory_bytes > 0) {
            usage.memory_usage_ratio = static_cast<double>(usage.used_memory_bytes) / usage.total_memory_bytes;
        }
    }
    
    // Get CPU usage
    usage.cpu_usage_percent = get_cpu_usage();
    
    // Get disk usage for current directory
    if (get_disk_usage(".", usage.total_disk_bytes, usage.available_disk_bytes)) {
        usage.disk_usage_ratio = static_cast<double>(usage.total_disk_bytes - usage.available_disk_bytes) / usage.total_disk_bytes;
    }
    
    return usage;
}

bool ResourceMonitor::ensure_memory_headroom(uint64_t required_mb) {
    uint64_t required_bytes = (required_mb > 0 ? required_mb : config_.memory_headroom_mb) * 1024 * 1024;
    
    uint64_t total_memory, available_memory;
    if (!get_memory_info(total_memory, available_memory)) {
        std::cerr << "❌ Cannot check memory headroom: unable to read memory information" << std::endl;
        return false;
    }
    
    if (available_memory < required_bytes) {
        double available_mb = static_cast<double>(available_memory) / (1024 * 1024);
        double required_mb_val = static_cast<double>(required_bytes) / (1024 * 1024);
        
        std::cerr << "❌ Insufficient memory headroom: " 
                  << std::fixed << std::setprecision(1) << available_mb 
                  << "MB available, need " 
                  << std::fixed << std::setprecision(1) << required_mb_val 
                  << "MB" << std::endl;
        return false;
    }
    
    return true;
}

bool ResourceMonitor::is_resource_pressure() {
    return resource_pressure_.load();
}

void ResourceMonitor::register_exhaustion_callback(ResourceExhaustionCallback callback) {
    exhaustion_callback_ = callback;
}

void ResourceMonitor::log_resource_usage(const std::string& message) {
    auto usage = get_current_usage();
    auto formatted = format_resource_usage(usage);
    
    std::string log_line;
    if (!message.empty()) {
        log_line = message + " - " + formatted;
    } else {
        log_line = formatted;
    }
    
    // Log to file if specified, otherwise to stdout
    if (!config_.log_file_path.empty()) {
        std::ofstream log_file(config_.log_file_path, std::ios::app);
        if (log_file.is_open()) {
            log_file << log_line << std::endl;
        }
    } else {
        std::cout << log_line << std::endl;
    }
}

std::string ResourceMonitor::format_resource_usage(const ResourceUsage& usage) {
    std::ostringstream oss;
    
    // Format timestamp
    auto time_t = std::chrono::system_clock::to_time_t(usage.timestamp);
    oss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
    
    // Format memory usage
    double total_mb = static_cast<double>(usage.total_memory_bytes) / (1024 * 1024);
    double available_mb = static_cast<double>(usage.available_memory_bytes) / (1024 * 1024);
    double used_mb = static_cast<double>(usage.used_memory_bytes) / (1024 * 1024);
    
    oss << " | Memory: " << std::fixed << std::setprecision(1) 
        << used_mb << "/" << total_mb << "MB (" 
        << std::setprecision(1) << (usage.memory_usage_ratio * 100) << "%)";
    
    // Format CPU usage
    oss << " | CPU: " << std::fixed << std::setprecision(1) << usage.cpu_usage_percent << "%";
    
    // Format disk usage
    if (usage.total_disk_bytes > 0) {
        double total_gb = static_cast<double>(usage.total_disk_bytes) / (1024 * 1024 * 1024);
        double available_gb = static_cast<double>(usage.available_disk_bytes) / (1024 * 1024 * 1024);
        
        oss << " | Disk: " << std::fixed << std::setprecision(1) 
            << available_gb << "/" << total_gb << "GB available (" 
            << std::setprecision(1) << ((1.0 - usage.disk_usage_ratio) * 100) << "%)";
    }
    
    return oss.str();
}

bool ResourceMonitor::get_memory_info(uint64_t& total_bytes, uint64_t& available_bytes) {
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo.is_open()) {
        return false;
    }
    
    std::string line;
    total_bytes = 0;
    available_bytes = 0;
    
    while (std::getline(meminfo, line)) {
        if (line.find("MemTotal:") == 0) {
            std::istringstream iss(line);
            std::string label, unit;
            iss >> label >> total_bytes >> unit;
            total_bytes *= 1024; // Convert from KB to bytes
        } else if (line.find("MemAvailable:") == 0) {
            std::istringstream iss(line);
            std::string label, unit;
            iss >> label >> available_bytes >> unit;
            available_bytes *= 1024; // Convert from KB to bytes
        }
        
        if (total_bytes > 0 && available_bytes > 0) {
            break;
        }
    }
    
    return total_bytes > 0 && available_bytes > 0;
}

double ResourceMonitor::get_cpu_usage() {
    static uint64_t prev_idle = 0, prev_total = 0;
    
    std::ifstream proc_stat("/proc/stat");
    if (!proc_stat.is_open()) {
        return 0.0;
    }
    
    std::string line;
    if (!std::getline(proc_stat, line)) {
        return 0.0;
    }
    
    std::istringstream iss(line);
    std::string cpu_label;
    uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
    
    iss >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    
    uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal;
    uint64_t total_diff = total - prev_total;
    uint64_t idle_diff = idle - prev_idle;
    
    double cpu_percent = 0.0;
    if (prev_total != 0 && total_diff > 0) {
        cpu_percent = 100.0 * (1.0 - static_cast<double>(idle_diff) / total_diff);
    }
    
    prev_idle = idle;
    prev_total = total;
    
    return cpu_percent;
}

bool ResourceMonitor::get_disk_usage(const std::string& path, uint64_t& total_bytes, uint64_t& available_bytes) {
    struct statvfs vfs;
    if (statvfs(path.c_str(), &vfs) != 0) {
        return false;
    }
    
    total_bytes = vfs.f_blocks * vfs.f_frsize;
    available_bytes = vfs.f_bavail * vfs.f_frsize;
    
    return true;
}

void ResourceMonitor::monitor_loop() {
    while (running_.load()) {
        try {
            auto usage = get_current_usage();
            check_thresholds(usage);
            
            if (config_.enable_automatic_logging) {
                // Log every 5 minutes during normal operation, more frequently under pressure
                static auto last_log = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                auto log_interval = resource_pressure_.load() ? 
                    std::chrono::seconds(30) : std::chrono::minutes(5);
                
                if (now - last_log >= log_interval) {
                    log_resource_usage("Resource monitor");
                    last_log = now;
                }
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Error in resource monitor loop: " << e.what() << std::endl;
        }
        
        std::this_thread::sleep_for(config_.check_interval);
    }
}

void ResourceMonitor::check_thresholds(const ResourceUsage& usage) {
    bool pressure_detected = false;
    std::string alert_message;
    
    // Check memory thresholds
    if (usage.memory_usage_ratio >= config_.memory_critical_threshold) {
        pressure_detected = true;
        alert_message = "CRITICAL: Memory usage at " + 
                       std::to_string(static_cast<int>(usage.memory_usage_ratio * 100)) + "%";
    } else if (usage.memory_usage_ratio >= config_.memory_warning_threshold) {
        pressure_detected = true;
        alert_message = "WARNING: Memory usage at " + 
                       std::to_string(static_cast<int>(usage.memory_usage_ratio * 100)) + "%";
    }
    
    // Check CPU thresholds
    if (usage.cpu_usage_percent >= config_.cpu_critical_threshold) {
        pressure_detected = true;
        if (!alert_message.empty()) alert_message += ", ";
        alert_message += "CRITICAL: CPU usage at " + 
                        std::to_string(static_cast<int>(usage.cpu_usage_percent)) + "%";
    } else if (usage.cpu_usage_percent >= config_.cpu_warning_threshold) {
        pressure_detected = true;
        if (!alert_message.empty()) alert_message += ", ";
        alert_message += "WARNING: CPU usage at " + 
                        std::to_string(static_cast<int>(usage.cpu_usage_percent)) + "%";
    }
    
    // Check disk thresholds
    if (usage.disk_usage_ratio >= config_.disk_critical_threshold) {
        pressure_detected = true;
        if (!alert_message.empty()) alert_message += ", ";
        alert_message += "CRITICAL: Disk usage at " + 
                        std::to_string(static_cast<int>(usage.disk_usage_ratio * 100)) + "%";
    } else if (usage.disk_usage_ratio >= config_.disk_warning_threshold) {
        pressure_detected = true;
        if (!alert_message.empty()) alert_message += ", ";
        alert_message += "WARNING: Disk usage at " + 
                        std::to_string(static_cast<int>(usage.disk_usage_ratio * 100)) + "%";
    }
    
    // Update pressure state
    bool prev_pressure = resource_pressure_.exchange(pressure_detected);
    
    // Call exhaustion callback if registered and we have alerts
    if (pressure_detected && exhaustion_callback_ && !alert_message.empty()) {
        exhaustion_callback_(usage, alert_message);
    }
    
    // Log state changes
    if (pressure_detected && !prev_pressure) {
        log_resource_usage("Resource pressure detected: " + alert_message);
    } else if (!pressure_detected && prev_pressure) {
        log_resource_usage("Resource pressure cleared");
    }
}

} // namespace monitoring
} // namespace slonana