# Distributed Shared Memory System Makefile

CC = mpicc
CFLAGS = -Wall -Wextra -std=c99 -pthread
LDFLAGS = -pthread
TARGET = dms
TEST_TARGET = dms_test

# Source files
SRC_DIR = src
SOURCES = $(SRC_DIR)/dms.c $(SRC_DIR)/dms_communication.c $(SRC_DIR)/dms_api.c $(SRC_DIR)/dms_config.c $(SRC_DIR)/main.c
OBJECTS = $(SOURCES:.c=.o)

# Test source files  
TEST_SOURCES = $(SRC_DIR)/dms.c $(SRC_DIR)/dms_communication.c $(SRC_DIR)/dms_api.c $(SRC_DIR)/dms_config.c
TEST_OBJECTS = $(TEST_SOURCES:.c=.o)

# Header files
HEADERS = $(SRC_DIR)/dms.h

.PHONY: all clean test install debug release

# Default target
all: $(TARGET)

# Main executable
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

# Test executable
test: $(TEST_TARGET)
	
$(TEST_TARGET): $(TEST_OBJECTS)
	$(CC) $(TEST_OBJECTS) -o $(TEST_TARGET) $(LDFLAGS)

# Object files
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Debug build
debug: CFLAGS += -g -DDEBUG -O0
debug: $(TARGET)

# Release build  
release: CFLAGS += -O2 -DNDEBUG
release: $(TARGET)

# Install (copy to /usr/local/bin)
install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/
	sudo chmod +x /usr/local/bin/$(TARGET)

# Clean build artifacts
clean:
	rm -f $(OBJECTS) $(TEST_OBJECTS) $(TARGET) $(TEST_TARGET)
	rm -f *.core core
	rm -f /dev/mqueue/dms_queue_*

# Run basic functionality test
run_test: $(TARGET)
	@echo "Starting DMS test with 4 processes..."
	@echo "Note: Run each command in separate terminals:"
	@echo "./$(TARGET) -n 4 -k 100 -t 1024 -p 0"
	@echo "./$(TARGET) -n 4 -k 100 -t 1024 -p 1"  
	@echo "./$(TARGET) -n 4 -k 100 -t 1024 -p 2"
	@echo "./$(TARGET) -n 4 -k 100 -t 1024 -p 3"

# Create sample configuration file
config:
	@echo "# DMS Configuration File" > dms.conf
	@echo "# Number of processes" >> dms.conf
	@echo "n 4" >> dms.conf
	@echo "# Number of blocks" >> dms.conf
	@echo "k 1000" >> dms.conf  
	@echo "# Block size in bytes" >> dms.conf
	@echo "t 4096" >> dms.conf
	@echo "# Process ID (must be set for each process)" >> dms.conf
	@echo "process_id 0" >> dms.conf
	@echo "Sample configuration file created: dms.conf"

# Docker targets
docker-build:
	docker build -t dms-system -f docker/Dockerfile .

docker-run: docker-build
	@echo "Starting DMS system with Docker Compose..."
	docker-compose -f docker/docker-compose.yml up --build

docker-clean:
	docker-compose -f docker/docker-compose.yml down --volumes --remove-orphans
	docker rmi dms-system 2>/dev/null || true

docker-shell: docker-build
	docker run -it --privileged --rm dms-system /bin/bash

# Help target
help:
	@echo "Available targets:"
	@echo "  all          - Build main executable (default)"
	@echo "  test         - Build test executable" 
	@echo "  debug        - Build with debug symbols"
	@echo "  release      - Build optimized version"
	@echo "  install      - Install to /usr/local/bin (requires sudo)"
	@echo "  clean        - Remove all build artifacts"
	@echo "  config       - Create sample configuration file"
	@echo "  run_test     - Show commands to run test"
	@echo "  docker-build - Build Docker image"
	@echo "  docker-run   - Run DMS system with Docker Compose"
	@echo "  docker-clean - Clean Docker containers and images"
	@echo "  docker-shell - Open shell in Docker container"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Native usage examples:"
	@echo "  make"
	@echo "  make debug"
	@echo "  make clean && make release"
	@echo "  ./$(TARGET) -n 4 -k 1000 -t 4096 -p 0"
	@echo ""
	@echo "Docker usage examples:"
	@echo "  make docker-run"
	@echo "  make docker-shell" 