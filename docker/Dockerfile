# Dockerfile for Distributed Shared Memory System
FROM ubuntu:22.04

# Set non-interactive mode for apt
ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    gcc \
    make \
    gdb \
    valgrind \
    htop \
    vim \
    libopenmpi-dev \
    openmpi-bin \
    && rm -rf /var/lib/apt/lists/*

# Create app directory
WORKDIR /app

# Copy source files
COPY ./src/ ./src/
COPY ./Makefile ./README.md ./

# Build the application
RUN make clean && make

# Create a non-root user for better security
RUN useradd -m -s /bin/bash dmsuser && \
    chown -R dmsuser:dmsuser /app

# Switch to non-root user
USER dmsuser

# Expose no ports (local IPC only)

# Default command shows help
CMD ["./dms", "-h"] 