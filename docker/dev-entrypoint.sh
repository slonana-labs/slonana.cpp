#!/bin/bash
set -euo pipefail

# Development Environment Entrypoint Script
# Provides additional development tools and utilities

SLONANA_HOME=${SLONANA_HOME:-/opt/slonana}

# Logging function
log() {
    echo "[$(date -Iseconds)] $*" >&2
}

# Initialize development environment
init_dev_env() {
    log "Setting up development environment..."
    
    # Create development directories
    mkdir -p "$SLONANA_HOME/dev" \
             "$SLONANA_HOME/test-data" \
             "$SLONANA_HOME/benchmarks"
    
    # Set up aliases for common development tasks
    cat > "$HOME/.bashrc" << 'EOF'
# Slonana development aliases
alias sl-build='cd /workspace && mkdir -p build && cd build && cmake .. && make -j$(nproc)'
alias sl-test='cd /workspace/build && ./slonana_tests'
alias sl-test-all='cd /workspace/build && ./slonana_comprehensive_tests'
alias sl-bench='cd /workspace/build && ./slonana_benchmarks'
alias sl-format='find /workspace/src /workspace/include /workspace/tests -name "*.cpp" -o -name "*.h" | xargs clang-format -i'
alias sl-check='cd /workspace && cppcheck --enable=warning src/ include/'
alias sl-clean='cd /workspace && rm -rf build'
alias sl-debug='gdb /workspace/build/slonana_validator'
alias sl-valgrind='valgrind --tool=memcheck --leak-check=full /workspace/build/slonana_tests'

# Development environment info
echo "=== Slonana.cpp Development Environment ==="
echo "Workspace: /workspace"
echo "Build dir: /workspace/build"
echo "Home: $SLONANA_HOME"
echo ""
echo "Useful commands:"
echo "  sl-build      - Build the project"
echo "  sl-test       - Run basic tests"
echo "  sl-test-all   - Run comprehensive tests"
echo "  sl-bench      - Run benchmarks"
echo "  sl-format     - Format code"
echo "  sl-check      - Static analysis"
echo "  sl-debug      - Debug with GDB"
echo "  sl-valgrind   - Memory check with Valgrind"
echo ""
EOF
    
    # Create development scripts
    create_dev_scripts
}

# Create useful development scripts
create_dev_scripts() {
    log "Creating development scripts..."
    
    # Quick test script
    cat > "$SLONANA_HOME/dev/quick-test.sh" << 'EOF'
#!/bin/bash
cd /workspace
echo "Building project..."
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)

echo "Running tests..."
./slonana_tests --quick
echo "Quick test complete!"
EOF
    
    # Performance profiling script
    cat > "$SLONANA_HOME/dev/profile.sh" << 'EOF'
#!/bin/bash
cd /workspace/build
echo "Running performance profile..."
perf record -g ./slonana_benchmarks
perf report
EOF
    
    # Memory leak detection script
    cat > "$SLONANA_HOME/dev/memcheck.sh" << 'EOF'
#!/bin/bash
cd /workspace/build
echo "Running memory leak detection..."
valgrind --tool=memcheck \
         --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --verbose \
         --log-file=memcheck.log \
         ./slonana_tests
echo "Memory check complete. See memcheck.log for details."
EOF
    
    # Code coverage script
    cat > "$SLONANA_HOME/dev/coverage.sh" << 'EOF'
#!/bin/bash
cd /workspace
echo "Building with coverage..."
mkdir -p build-coverage && cd build-coverage
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="--coverage"
make -j$(nproc)

echo "Running tests..."
./slonana_tests
./slonana_comprehensive_tests

echo "Generating coverage report..."
gcov src/*.cpp
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage-html
echo "Coverage report generated in coverage-html/"
EOF
    
    # Make scripts executable
    chmod +x "$SLONANA_HOME/dev"/*.sh
}

# Set up monitoring for development
setup_monitoring() {
    log "Setting up development monitoring..."
    
    # Create a simple metrics endpoint
    cat > "$SLONANA_HOME/dev/metrics-server.py" << 'EOF'
#!/usr/bin/env python3
"""Simple metrics server for development monitoring."""

import json
import time
import threading
from http.server import HTTPServer, BaseHTTPRequestHandler
import subprocess
import psutil

class MetricsHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/metrics':
            metrics = self.collect_metrics()
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps(metrics, indent=2).encode())
        else:
            self.send_response(404)
            self.end_headers()
    
    def collect_metrics(self):
        """Collect system and validator metrics."""
        return {
            'timestamp': time.time(),
            'system': {
                'cpu_percent': psutil.cpu_percent(interval=1),
                'memory_percent': psutil.virtual_memory().percent,
                'disk_usage': psutil.disk_usage('/').percent,
                'load_average': psutil.getloadavg() if hasattr(psutil, 'getloadavg') else None
            },
            'validator': {
                'process_exists': self.check_process('slonana_validator'),
                'rpc_responsive': self.check_rpc()
            }
        }
    
    def check_process(self, name):
        """Check if a process is running."""
        for proc in psutil.process_iter(['name']):
            if proc.info['name'] == name:
                return True
        return False
    
    def check_rpc(self):
        """Check if RPC is responsive."""
        try:
            import urllib.request
            urllib.request.urlopen('http://localhost:8899/health', timeout=5)
            return True
        except:
            return False

if __name__ == '__main__':
    server = HTTPServer(('0.0.0.0', 9000), MetricsHandler)
    print("Metrics server running on http://localhost:9000/metrics")
    server.serve_forever()
EOF
    
    chmod +x "$SLONANA_HOME/dev/metrics-server.py"
}

# Start development services
start_dev_services() {
    log "Starting development services..."
    
    # Start metrics server in background
    if command -v python3 >/dev/null; then
        python3 "$SLONANA_HOME/dev/metrics-server.py" &
        log "Metrics server started on http://localhost:9000/metrics"
    fi
}

# Handle commands
case "${1:-bash}" in
    bash|shell)
        init_dev_env
        setup_monitoring
        start_dev_services
        log "Development environment ready!"
        exec /bin/bash
        ;;
    
    build)
        log "Building project..."
        cd /workspace
        mkdir -p build && cd build
        cmake .. -DCMAKE_BUILD_TYPE="${2:-Debug}"
        make -j$(nproc)
        log "Build complete!"
        ;;
    
    test)
        shift  # Remove 'test' from arguments
        cd /workspace/build
        if [[ $# -eq 0 ]]; then
            exec ./slonana_tests
        else
            exec ./slonana_tests "$@"
        fi
        ;;
    
    test-all)
        cd /workspace/build
        ./slonana_tests && ./slonana_comprehensive_tests
        ;;
    
    bench)
        cd /workspace/build
        exec ./slonana_benchmarks
        ;;
    
    debug)
        cd /workspace/build
        exec gdb ./slonana_validator
        ;;
    
    valgrind)
        cd /workspace/build
        exec valgrind --tool=memcheck --leak-check=full ./slonana_tests
        ;;
    
    format)
        log "Formatting code..."
        find /workspace/src /workspace/include /workspace/tests \
             -name "*.cpp" -o -name "*.h" | xargs clang-format -i
        log "Code formatting complete!"
        ;;
    
    check)
        log "Running static analysis..."
        cd /workspace
        cppcheck --enable=warning --error-exitcode=1 src/ include/
        ;;
    
    clean)
        log "Cleaning build artifacts..."
        rm -rf /workspace/build /workspace/build-*
        log "Clean complete!"
        ;;
    
    metrics)
        exec python3 "$SLONANA_HOME/dev/metrics-server.py"
        ;;
    
    help|--help|-h)
        cat << 'EOF'
Slonana.cpp Development Environment

Usage: dev-entrypoint.sh [COMMAND] [ARGS...]

Commands:
  bash|shell    Start interactive bash shell (default)
  build [TYPE]  Build project (Debug|Release|RelWithDebInfo)
  test [ARGS]   Run basic tests with optional arguments
  test-all      Run all tests (basic + comprehensive)
  bench         Run benchmarks
  debug         Start GDB debugger
  valgrind      Run tests with Valgrind memory checking
  format        Format source code with clang-format
  check         Run static analysis with cppcheck
  clean         Clean build artifacts
  metrics       Start metrics server on port 9000
  help          Show this help message

Development Aliases (available in bash):
  sl-build      - Build the project
  sl-test       - Run basic tests
  sl-test-all   - Run comprehensive tests
  sl-bench      - Run benchmarks
  sl-format     - Format code
  sl-check      - Static analysis
  sl-debug      - Debug with GDB
  sl-valgrind   - Memory check

Examples:
  # Start development shell
  docker run -it slonana/validator:dev

  # Build and test
  docker run slonana/validator:dev build
  docker run slonana/validator:dev test

  # Format code and run checks
  docker run -v $(pwd):/workspace slonana/validator:dev format
  docker run -v $(pwd):/workspace slonana/validator:dev check
EOF
        ;;
    
    *)
        # Pass through to regular entrypoint
        exec /opt/slonana/entrypoint.sh "$@"
        ;;
esac