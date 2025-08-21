# Multi-stage Dockerfile for Slonana.cpp Validator
# Optimized for production deployment with minimal image size

# Stage 1: Build environment
FROM ubuntu:22.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    curl \
    pkg-config \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# Set C++ compiler to use modern standards
ENV CC=gcc
ENV CXX=g++

# Create build user (security best practice)
RUN groupadd -r slonana && useradd -r -g slonana slonana

# Set working directory
WORKDIR /build

# Copy source code
COPY --chown=slonana:slonana . .

# Build the validator
RUN mkdir -p build && cd build && \
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_FLAGS="-O3 -march=native -DNDEBUG" && \
    make -j$(nproc) && \
    strip slonana_validator

# Stage 2: Runtime environment
FROM ubuntu:22.04 AS runtime

# Install runtime dependencies only
RUN apt-get update && apt-get install -y \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/* \
    && apt-get autoremove -y \
    && apt-get clean

# Create non-root user for security
RUN groupadd -r slonana && useradd -r -g slonana -s /bin/bash slonana

# Create necessary directories
RUN mkdir -p /opt/slonana/bin \
             /opt/slonana/data \
             /opt/slonana/config \
             /opt/slonana/logs \
    && chown -R slonana:slonana /opt/slonana

# Copy built binary from builder stage
COPY --from=builder --chown=slonana:slonana /build/build/slonana_validator /opt/slonana/bin/

# Copy configuration templates
COPY --chown=slonana:slonana docker/validator.toml /opt/slonana/config/
COPY --chown=slonana:slonana docker/entrypoint.sh /opt/slonana/
RUN chmod +x /opt/slonana/entrypoint.sh

# Switch to non-root user
USER slonana

# Set working directory
WORKDIR /opt/slonana

# Environment variables
ENV SLONANA_HOME=/opt/slonana
ENV SLONANA_LEDGER_PATH=/opt/slonana/data/ledger
ENV SLONANA_CONFIG_PATH=/opt/slonana/config/validator.toml
ENV SLONANA_LOG_LEVEL=info

# Expose ports
# 8899: RPC port
# 8900: WebSocket port  
# 8001: Gossip port
# 8003: Validator TPU port
EXPOSE 8899 8900 8001 8003

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=60s --retries=3 \
    CMD curl -f http://localhost:8899/health || exit 1

# Default command
ENTRYPOINT ["/opt/slonana/entrypoint.sh"]
CMD ["validator"]

# Labels for metadata
LABEL maintainer="Slonana Labs <contact@slonana.org>"
LABEL description="High-performance C++ Solana validator implementation"
LABEL version="1.0.0"
LABEL repository="https://github.com/slonana-labs/slonana.cpp"

# Stage 3: Development environment (optional)
FROM builder AS development

# Install additional development tools
RUN apt-get update && apt-get install -y \
    gdb \
    valgrind \
    clang-format \
    clang-tidy \
    cppcheck \
    vim \
    nano \
    && rm -rf /var/lib/apt/lists/*

# Build with debug symbols
RUN cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Debug && \
    make -j$(nproc)

# Copy test utilities
COPY --chown=slonana:slonana docker/dev-entrypoint.sh /opt/slonana/
RUN chmod +x /opt/slonana/dev-entrypoint.sh

USER slonana
WORKDIR /opt/slonana

ENTRYPOINT ["/opt/slonana/dev-entrypoint.sh"]
CMD ["bash"]