#!/bin/bash

# Build script for DMS (Distributed Shared Memory System)

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default build type
BUILD_TYPE="release"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--debug)
            BUILD_TYPE="debug"
            shift
            ;;
        -r|--release)
            BUILD_TYPE="release"
            shift
            ;;
        -c|--clean)
            echo -e "${YELLOW}Cleaning build artifacts...${NC}"
            make clean
            exit 0
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo "Options:"
            echo "  -d, --debug    Build with debug symbols"
            echo "  -r, --release  Build optimized version (default)"
            echo "  -c, --clean    Clean build artifacts and exit"
            echo "  -h, --help     Show this help message"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            exit 1
            ;;
    esac
done

echo -e "${BLUE}Building DMS System (${BUILD_TYPE} mode)...${NC}"

# Clean first
echo -e "${YELLOW}Cleaning previous build...${NC}"
make clean

# Build
echo -e "${YELLOW}Compiling...${NC}"
if make $BUILD_TYPE; then
    echo -e "${GREEN}✓ Build successful!${NC}"
    echo -e "${BLUE}Executable: ./dms${NC}"
else
    echo -e "${RED}✗ Build failed!${NC}"
    exit 1
fi

# Show file info
if [ -f "./dms" ]; then
    echo -e "${BLUE}Binary info:${NC}"
    ls -lh ./dms
    file ./dms
fi 