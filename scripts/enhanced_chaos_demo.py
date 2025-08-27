#!/usr/bin/env python3
"""
Enhanced chaos engineering script with resource monitoring to prevent SIGTERM from resource exhaustion.

This script demonstrates how to integrate resource monitoring into chaos testing
to prevent resource exhaustion-induced SIGTERM signals.
"""

import subprocess
import time
import sys
import signal
import os
import psutil
from pathlib import Path

# Import our resource utilities
sys.path.append(str(Path(__file__).parent))
from resource_utils import ResourceMonitor, ensure_memory_headroom, check_system_health, monitor_stress_test

class EnhancedChaosEngine:
    """Enhanced chaos engine with resource monitoring to prevent SIGTERM."""
    
    def __init__(self, scenario_type: str, severity: str, duration: int):
        self.scenario_type = scenario_type
        self.severity = severity
        self.duration = duration
        self.validator_process = None
        self.resource_monitor = ResourceMonitor(memory_headroom_mb=512)
        
    def log(self, message: str):
        """Log a message with timestamp."""
        timestamp = time.strftime("%H:%M:%S")
        print(f"[{timestamp}] 🌪️  {message}")
        
    def pre_flight_checks(self) -> bool:
        """Perform pre-flight resource checks before starting chaos testing."""
        self.log("Starting pre-flight resource checks...")
        
        # Check system health
        if not check_system_health():
            self.log("❌ Pre-flight check failed: System not healthy for stress testing")
            return False
            
        # Ensure sufficient memory headroom
        if not ensure_memory_headroom(512):
            self.log("❌ Pre-flight check failed: Insufficient memory headroom")
            return False
            
        # Check that validator can start
        if not self._check_validator_availability():
            self.log("❌ Pre-flight check failed: Validator not available")
            return False
            
        self.log("✅ Pre-flight checks passed")
        return True
    
    def _check_validator_availability(self) -> bool:
        """Check if the validator binary is available."""
        validator_path = Path("build/slonana_validator")
        if not validator_path.exists():
            self.log(f"Validator binary not found at {validator_path}")
            return False
        return True
    
    def start_validator_with_monitoring(self) -> bool:
        """Start the validator with resource monitoring."""
        self.log("Starting validator with resource monitoring...")
        
        # Log resources before starting
        self.resource_monitor.log_resource_usage("Before validator startup")
        
        try:
            # Start validator in background
            cmd = [
                "./build/slonana_validator",
                "--ledger-path", "/tmp/chaos_test_ledger",
                "--log-level", "info"
            ]
            
            self.validator_process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True
            )
            
            # Wait for startup and check resource usage
            self.log("Waiting for validator startup...")
            time.sleep(5)
            
            if self.validator_process.poll() is not None:
                self.log("❌ Validator failed to start")
                return False
                
            # Check resource usage after startup
            self.resource_monitor.log_resource_usage("After validator startup")
            
            # Ensure we still have headroom
            if not self.resource_monitor.ensure_memory_headroom():
                self.log("⚠️ WARNING: Low memory headroom after validator startup")
                return False
                
            self.log("✅ Validator started successfully with sufficient resources")
            return True
            
        except Exception as e:
            self.log(f"❌ Failed to start validator: {e}")
            return False
    
    def run_chaos_scenario(self) -> bool:
        """Run the chaos scenario with continuous resource monitoring."""
        self.log(f"Starting chaos scenario: {self.scenario_type} (severity: {self.severity})")
        
        # Start resource monitoring during chaos
        start_time = time.time()
        last_check = start_time
        
        try:
            # Simulate different chaos scenarios (simplified for demo)
            if self.scenario_type == "memory_pressure":
                return self._run_memory_pressure_scenario()
            elif self.scenario_type == "cpu_stress":
                return self._run_cpu_stress_scenario()
            elif self.scenario_type == "network_chaos":
                return self._run_network_chaos_scenario()
            else:
                self.log(f"Unknown chaos scenario: {self.scenario_type}")
                return False
                
        except Exception as e:
            self.log(f"❌ Chaos scenario failed: {e}")
            return False
    
    def _run_memory_pressure_scenario(self) -> bool:
        """Simulate memory pressure with safety checks."""
        self.log("Starting memory pressure scenario...")
        
        # Check initial state
        memory_info = self.resource_monitor.get_memory_info()
        initial_memory_usage = (memory_info['percent_used'])
        
        self.log(f"Initial memory usage: {initial_memory_usage:.1f}%")
        
        # Gradually increase memory pressure while monitoring
        memory_consumers = []
        
        try:
            for i in range(self.duration // 10):  # Check every 10 seconds
                # Check if we should abort due to resource pressure
                pressure, pressure_msg = self.resource_monitor.check_resource_pressure()
                if pressure:
                    self.log(f"⚠️ Resource pressure detected: {pressure_msg}")
                    break
                
                # Check validator health
                if self.validator_process and self.validator_process.poll() is not None:
                    self.log("❌ Validator died during chaos scenario")
                    return False
                
                # Simulate memory allocation (in a real scenario, this would be stress-ng or similar)
                self.log(f"Chaos step {i+1}: Simulating memory pressure...")
                
                # Log current resource usage
                self.resource_monitor.log_resource_usage(f"Memory chaos step {i+1}")
                
                # Safety check - ensure we don't exhaust memory
                if not ensure_memory_headroom(256):
                    self.log("🚨 ABORT: Approaching memory exhaustion, stopping chaos scenario")
                    break
                
                time.sleep(10)
            
            self.log("✅ Memory pressure scenario completed safely")
            return True
            
        finally:
            # Cleanup any memory consumers
            for consumer in memory_consumers:
                try:
                    consumer.terminate()
                except:
                    pass
    
    def _run_cpu_stress_scenario(self) -> bool:
        """Simulate CPU stress with monitoring."""
        self.log("Starting CPU stress scenario...")
        
        stress_process = None
        try:
            # Start CPU stress (if stress-ng is available)
            if subprocess.run(["which", "stress-ng"], capture_output=True).returncode == 0:
                stress_process = subprocess.Popen([
                    "stress-ng", "--cpu", "2", "--timeout", f"{self.duration}s", "--quiet"
                ])
                
            # Monitor during stress
            monitor_stress_test(f"CPU stress {self.severity}", self.duration)
            
            if stress_process:
                stress_process.wait()
                
            self.log("✅ CPU stress scenario completed")
            return True
            
        except Exception as e:
            self.log(f"❌ CPU stress scenario failed: {e}")
            if stress_process:
                stress_process.terminate()
            return False
    
    def _run_network_chaos_scenario(self) -> bool:
        """Simulate network chaos."""
        self.log("Starting network chaos scenario...")
        
        # For demo purposes, just monitor resources during a delay
        for i in range(self.duration // 5):
            self.resource_monitor.log_resource_usage(f"Network chaos step {i+1}")
            
            # Check validator health
            if self.validator_process and self.validator_process.poll() is not None:
                self.log("❌ Validator died during network chaos")
                return False
                
            time.sleep(5)
        
        self.log("✅ Network chaos scenario completed")
        return True
    
    def stop_validator(self):
        """Stop the validator gracefully."""
        if self.validator_process:
            self.log("Stopping validator...")
            try:
                # Send SIGTERM first
                self.validator_process.terminate()
                
                # Wait for graceful shutdown
                try:
                    self.validator_process.wait(timeout=10)
                    self.log("✅ Validator stopped gracefully")
                except subprocess.TimeoutExpired:
                    # Force kill if needed
                    self.log("⚠️ Forcing validator shutdown")
                    self.validator_process.kill()
                    self.validator_process.wait()
                    
            except Exception as e:
                self.log(f"Error stopping validator: {e}")
            
            # Log final resource usage
            self.resource_monitor.log_resource_usage("After validator shutdown")
    
    def run_enhanced_chaos_test(self) -> bool:
        """Run the complete enhanced chaos test with resource monitoring."""
        try:
            # Pre-flight checks
            if not self.pre_flight_checks():
                return False
            
            # Start validator with monitoring
            if not self.start_validator_with_monitoring():
                return False
            
            # Run chaos scenario
            success = self.run_chaos_scenario()
            
            return success
            
        finally:
            # Always cleanup
            self.stop_validator()


def main():
    if len(sys.argv) != 4:
        print("Usage: enhanced_chaos_demo.py <scenario_type> <severity> <duration>")
        print("Examples:")
        print("  enhanced_chaos_demo.py memory_pressure medium 60")
        print("  enhanced_chaos_demo.py cpu_stress high 30")
        print("  enhanced_chaos_demo.py network_chaos low 45")
        sys.exit(1)
    
    scenario_type = sys.argv[1]
    severity = sys.argv[2]
    duration = int(sys.argv[3])
    
    print(f"🌪️ Enhanced Chaos Engineering Demo")
    print(f"Scenario: {scenario_type}")
    print(f"Severity: {severity}")
    print(f"Duration: {duration}s")
    print("=" * 50)
    
    engine = EnhancedChaosEngine(scenario_type, severity, duration)
    
    def signal_handler(signum, frame):
        print(f"\n⏹️ Received signal {signum}, cleaning up...")
        engine.stop_validator()
        sys.exit(0)
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    success = engine.run_enhanced_chaos_test()
    
    if success:
        print("\n✅ Enhanced chaos test completed successfully!")
        print("🎯 Validator survived chaos scenario without resource exhaustion")
        sys.exit(0)
    else:
        print("\n❌ Enhanced chaos test failed!")
        print("📊 Check resource logs for analysis")
        sys.exit(1)


if __name__ == "__main__":
    main()