#!/usr/bin/env python3
"""
Resource monitoring utilities for preventing SIGTERM from resource exhaustion.

This module provides utilities to check resource availability and detect
resource pressure before launching stress tests or chaos engineering scenarios.
"""

import psutil
import sys
import time
import subprocess
import shutil
from datetime import datetime
from typing import Dict, Optional, Tuple


class ResourceMonitor:
    """Resource monitoring utility for stress testing and chaos engineering."""
    
    def __init__(self, memory_headroom_mb: int = 512, cpu_threshold: float = 85.0, 
                 disk_threshold: float = 90.0):
        """
        Initialize resource monitor.
        
        Args:
            memory_headroom_mb: Minimum memory headroom in MB
            cpu_threshold: CPU usage warning threshold (%)
            disk_threshold: Disk usage warning threshold (%)
        """
        self.memory_headroom_mb = memory_headroom_mb
        self.cpu_threshold = cpu_threshold
        self.disk_threshold = disk_threshold
    
    def get_memory_info(self) -> Dict[str, float]:
        """Get current memory information."""
        memory = psutil.virtual_memory()
        return {
            'total_mb': memory.total / (1024 * 1024),
            'available_mb': memory.available / (1024 * 1024),
            'used_mb': memory.used / (1024 * 1024),
            'percent_used': memory.percent
        }
    
    def get_cpu_info(self) -> Dict[str, float]:
        """Get current CPU information."""
        # Get CPU usage over a short interval for accuracy
        cpu_percent = psutil.cpu_percent(interval=1)
        cpu_count = psutil.cpu_count()
        load_avg = psutil.getloadavg() if hasattr(psutil, 'getloadavg') else (0, 0, 0)
        
        return {
            'percent_used': cpu_percent,
            'logical_cores': cpu_count,
            'load_1min': load_avg[0],
            'load_5min': load_avg[1],
            'load_15min': load_avg[2]
        }
    
    def get_disk_info(self, path: str = '.') -> Dict[str, float]:
        """Get disk usage information for the given path."""
        usage = shutil.disk_usage(path)
        total_gb = usage.total / (1024 ** 3)
        free_gb = usage.free / (1024 ** 3)
        used_gb = (usage.total - usage.free) / (1024 ** 3)
        percent_used = ((usage.total - usage.free) / usage.total) * 100
        
        return {
            'total_gb': total_gb,
            'free_gb': free_gb,
            'used_gb': used_gb,
            'percent_used': percent_used
        }
    
    def ensure_memory_headroom(self, min_mb: Optional[int] = None) -> Tuple[bool, str]:
        """
        Ensure sufficient memory headroom is available.
        
        Args:
            min_mb: Minimum required memory headroom in MB (defaults to configured value)
            
        Returns:
            Tuple of (success, message)
        """
        required_mb = min_mb if min_mb is not None else self.memory_headroom_mb
        memory_info = self.get_memory_info()
        
        available_mb = memory_info['available_mb']
        
        if available_mb < required_mb:
            message = (f"❌ Insufficient memory headroom: {available_mb:.1f}MB available, "
                      f"need {required_mb}MB")
            return False, message
        
        message = (f"✅ Sufficient memory headroom: {available_mb:.1f}MB available "
                  f"(required: {required_mb}MB)")
        return True, message
    
    def check_resource_pressure(self) -> Tuple[bool, str]:
        """
        Check if the system is under resource pressure.
        
        Returns:
            Tuple of (has_pressure, message)
        """
        issues = []
        
        # Check memory
        memory_info = self.get_memory_info()
        if memory_info['percent_used'] > 85:
            issues.append(f"High memory usage: {memory_info['percent_used']:.1f}%")
        
        # Check CPU
        cpu_info = self.get_cpu_info()
        if cpu_info['percent_used'] > self.cpu_threshold:
            issues.append(f"High CPU usage: {cpu_info['percent_used']:.1f}%")
        
        # Check disk
        disk_info = self.get_disk_info()
        if disk_info['percent_used'] > self.disk_threshold:
            issues.append(f"High disk usage: {disk_info['percent_used']:.1f}%")
        
        if issues:
            return True, "Resource pressure detected: " + ", ".join(issues)
        else:
            return False, "No resource pressure detected"
    
    def log_resource_usage(self, message: str = "", file_path: Optional[str] = None):
        """
        Log current resource usage.
        
        Args:
            message: Optional message to include
            file_path: Optional file to write to (defaults to stdout)
        """
        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        memory_info = self.get_memory_info()
        cpu_info = self.get_cpu_info()
        disk_info = self.get_disk_info()
        
        log_line = (
            f"[{timestamp}] 📊 {message} | "
            f"Memory: {memory_info['used_mb']:.1f}/{memory_info['total_mb']:.1f}MB "
            f"({memory_info['percent_used']:.1f}%) | "
            f"CPU: {cpu_info['percent_used']:.1f}% | "
            f"Disk: {disk_info['used_gb']:.1f}/{disk_info['total_gb']:.1f}GB "
            f"({disk_info['percent_used']:.1f}%)"
        )
        
        if file_path:
            with open(file_path, 'a') as f:
                f.write(log_line + '\n')
        else:
            print(log_line)
    
    def wait_for_resource_availability(self, timeout_seconds: int = 60) -> bool:
        """
        Wait for sufficient resources to become available.
        
        Args:
            timeout_seconds: Maximum time to wait
            
        Returns:
            True if resources became available, False if timeout
        """
        start_time = time.time()
        
        while time.time() - start_time < timeout_seconds:
            success, message = self.ensure_memory_headroom()
            pressure, pressure_msg = self.check_resource_pressure()
            
            if success and not pressure:
                print(f"✅ Resources available: {message}")
                return True
            
            print(f"⏳ Waiting for resources... {message}")
            if pressure:
                print(f"   {pressure_msg}")
            
            time.sleep(5)
        
        print(f"❌ Timeout waiting for sufficient resources ({timeout_seconds}s)")
        return False


def ensure_memory_headroom(min_mb: int = 512) -> bool:
    """
    Utility function to ensure memory headroom (compatible with issue example).
    
    Args:
        min_mb: Minimum required memory headroom in MB
        
    Returns:
        True if sufficient headroom available
    """
    monitor = ResourceMonitor(memory_headroom_mb=min_mb)
    success, message = monitor.ensure_memory_headroom()
    print(message)
    return success


def log_system_resources(message: str = "System resources"):
    """Log current system resource usage."""
    monitor = ResourceMonitor()
    monitor.log_resource_usage(message)


def check_system_health() -> bool:
    """
    Comprehensive system health check before starting stress tests.
    
    Returns:
        True if system is healthy for stress testing
    """
    print("🔍 Checking system health before stress testing...")
    
    monitor = ResourceMonitor()
    
    # Check memory headroom
    memory_ok, memory_msg = monitor.ensure_memory_headroom()
    print(f"   {memory_msg}")
    
    # Check resource pressure
    pressure, pressure_msg = monitor.check_resource_pressure()
    print(f"   {pressure_msg}")
    
    # Log current state
    monitor.log_resource_usage("Pre-stress system state")
    
    if not memory_ok:
        print("❌ Insufficient memory headroom for stress testing")
        return False
    
    if pressure:
        print("⚠️ System under resource pressure - stress testing not recommended")
        return False
    
    print("✅ System healthy for stress testing")
    return True


def monitor_stress_test(test_name: str, duration_seconds: int):
    """
    Monitor resource usage during stress test execution.
    
    Args:
        test_name: Name of the stress test
        duration_seconds: Expected test duration
    """
    print(f"📊 Starting resource monitoring for {test_name} ({duration_seconds}s)")
    
    monitor = ResourceMonitor()
    start_time = time.time()
    log_interval = min(30, duration_seconds // 10)  # Log at least 10 times during test
    
    monitor.log_resource_usage(f"Stress test '{test_name}' started")
    
    try:
        while time.time() - start_time < duration_seconds:
            time.sleep(log_interval)
            
            elapsed = int(time.time() - start_time)
            monitor.log_resource_usage(f"Stress test '{test_name}' - {elapsed}s elapsed")
            
            # Check for resource exhaustion
            pressure, pressure_msg = monitor.check_resource_pressure()
            if pressure:
                print(f"⚠️ {pressure_msg}")
                
                # Check if we should abort
                memory_info = monitor.get_memory_info()
                if memory_info['available_mb'] < 256:  # Critical threshold
                    print(f"🚨 CRITICAL: Memory critically low ({memory_info['available_mb']:.1f}MB)")
                    print("Aborting stress test to prevent system kill")
                    return False
    
    except KeyboardInterrupt:
        print(f"\n⏹️ Stress test '{test_name}' interrupted by user")
        
    finally:
        monitor.log_resource_usage(f"Stress test '{test_name}' completed")
    
    return True


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: resource_utils.py <command> [args...]")
        print("Commands:")
        print("  check_health - Check system health")
        print("  ensure_memory <mb> - Check memory headroom")
        print("  log_resources [message] - Log current resource usage")
        print("  monitor_test <name> <duration> - Monitor stress test")
        sys.exit(1)
    
    command = sys.argv[1]
    
    if command == "check_health":
        success = check_system_health()
        sys.exit(0 if success else 1)
        
    elif command == "ensure_memory":
        min_mb = int(sys.argv[2]) if len(sys.argv) > 2 else 512
        success = ensure_memory_headroom(min_mb)
        sys.exit(0 if success else 1)
        
    elif command == "log_resources":
        message = " ".join(sys.argv[2:]) if len(sys.argv) > 2 else "System resources"
        log_system_resources(message)
        
    elif command == "monitor_test":
        if len(sys.argv) < 4:
            print("Usage: resource_utils.py monitor_test <name> <duration>")
            sys.exit(1)
        test_name = sys.argv[2]
        duration = int(sys.argv[3])
        success = monitor_stress_test(test_name, duration)
        sys.exit(0 if success else 1)
        
    else:
        print(f"Unknown command: {command}")
        sys.exit(1)