#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "dms.h"

static int running = 1;

void signal_handler(int sig) {
    running = 0;
}

void test_basic_operations(void) {
    printf("\n=== Testing Basic Operations ===\n");

    // Test writing and reading a simple string
    const char *test_string = "ALO MUNDO";
    byte buffer[256];
    int result;

    printf("TEST: Writing '%s' to position 0...\n", test_string);
    result = escreve(0, (byte *)test_string, strlen(test_string));
    if (result != DMS_SUCCESS) {
        printf("Error writing: %d\n", result);
        return;
    }

    printf("TEST: Reading from position 0...\n");
    memset(buffer, 0, sizeof(buffer));
    result = le(0, buffer, strlen(test_string));
    if (result != DMS_SUCCESS) {
        printf("Error reading: %d\n", result);
        return;
    }

    printf("TEST: Read '%s'\n", buffer);
    if (strcmp((char *)buffer, test_string) == 0) {
        printf("✓ Basic read/write test PASSED\n");
    } else {
        printf("✗ Basic read/write test FAILED\n");
    }
}

void test_cross_block_operations(void) {
    printf("\n=== Testing Cross-Block Operations ===\n");

    // Write data that spans multiple blocks
    const char *long_string =
        "This is a very long string that should span multiple blocks "
        "to test the cross-block read and write functionality of the "
        "distributed shared memory system. It should demonstrate that "
        "data can be correctly written and read across block boundaries.";

    int len = strlen(long_string);
    byte buffer[512];
    int result;

    // Calculate a position that will cross block boundaries
    int block_size = dms_ctx->config.t;
    int cross_position = block_size - 20;  // Start 20 bytes before block boundary

    printf("TEST: Writing %d bytes starting at position %d (crosses block boundary)...\n",
           len, cross_position);
    result = escreve(cross_position, (byte *)long_string, len);
    if (result != DMS_SUCCESS) {
        printf("Error writing cross-block: %d\n", result);
        return;
    }

    printf("TEST: Reading back the cross-block data...\n");
    memset(buffer, 0, sizeof(buffer));
    result = le(cross_position, buffer, len);
    if (result != DMS_SUCCESS) {
        printf("Error reading cross-block: %d\n", result);
        return;
    }

    buffer[len] = '\0';
    if (strcmp((char *)buffer, long_string) == 0) {
        printf("✓ Cross-block read/write test PASSED\n");
    } else {
        printf("✗ Cross-block read/write test FAILED\n");
        printf("Expected: %s\n", long_string);
        printf("Got: %s\n", buffer);
    }
}

void test_cache_behavior(void) {
    printf("\n=== Testing Cache Behavior ===\n");

    // Test successive reads from the same remote block
    byte buffer1[64], buffer2[64];
    int result;

    // Find a block that belongs to another process
    int remote_block = -1;
    for (int i = 0; i < dms_ctx->config.k; i++) {
        if (get_block_owner(i) != dms_ctx->config.process_id) {
            remote_block = i;
            break;
        }
    }

    if (remote_block == -1) {
        printf("TEST: No remote blocks available for cache testing\n");
        return;
    }

    int remote_position = remote_block * dms_ctx->config.t;

    printf("TEST: First read from remote block %d (should cause cache miss)...\n", remote_block);
    result = le(remote_position, buffer1, 32);
    if (result != DMS_SUCCESS) {
        printf("Error in first read: %d\n", result);
        return;
    }

    printf("TEST: Second read from same remote block (should hit cache)...\n");
    result = le(remote_position, buffer2, 32);
    if (result != DMS_SUCCESS) {
        printf("Error in second read: %d\n", result);
        return;
    }

    if (memcmp(buffer1, buffer2, 32) == 0) {
        printf("✓ Cache consistency test PASSED\n");
    } else {
        printf("✗ Cache consistency test FAILED\n");
    }
}

void test_cache_invalidation_scenario(void) {
    printf("\n=== Testing Cache Invalidation Scenario ===\n");

    byte buffer1[64], buffer2[64];
    int result;

    // Find a block that belongs to another process and is NOT already cached
    int remote_block = -1;
    for (int i = 2; i < dms_ctx->config.k; i++) {  // Start from block 2 to avoid previously used blocks
        if (get_block_owner(i) != dms_ctx->config.process_id) {
            // Check if this block is NOT in cache
            cache_entry_t *existing_entry = find_cache_entry(i);
            if (!existing_entry || !existing_entry->valid) {
                remote_block = i;
                break;
            }
        }
    }

    if (remote_block == -1) {
        printf("TEST: No suitable remote blocks available for invalidation testing\n");
        return;
    }

    int remote_position = remote_block * dms_ctx->config.t;
    int owner_process = get_block_owner(remote_block);

    // Step 1: Process A reads remote block (should be genuine cache miss)
    printf("TEST: Process A reading from remote block %d (owner=%d) - cache miss...\n",
           remote_block, owner_process);

    result = le(remote_position, buffer1, 32);
    if (result != DMS_SUCCESS) {
        printf("Error in first read: %d\n", result);
        return;
    }

    // Verify cache entry exists after first read
    cache_entry_t *cache_entry = find_cache_entry(remote_block);
    if (cache_entry && cache_entry->valid) {
        printf("TEST: Block %d now cached in process A\n", remote_block);
    } else {
        printf("Error: Cache entry not found or invalid after read\n");
        return;
    }

    // Step 2: Process A writes to remote block (simulates Process B writing)
    printf("TEST: Process A writing to remote block %d (triggers invalidation)...\n", remote_block);

    const char *test_data = "INVALIDATION_TEST_DATA";
    result = escreve(remote_position, (byte *)test_data, strlen(test_data));
    if (result != DMS_SUCCESS) {
        printf("Error in write operation: %d\n", result);
        return;
    }

    // Step 3: Process A reads again (should get updated data)
    printf("TEST: Process A reading again from block %d (should see updated data)...\n", remote_block);

    result = le(remote_position, buffer2, 32);
    if (result != DMS_SUCCESS) {
        printf("Error in second read: %d\n", result);
        return;
    }

    // Check if data changed and matches what we wrote
    if (memcmp(buffer1, buffer2, strlen(test_data)) != 0 &&
        memcmp(buffer2, test_data, strlen(test_data)) == 0) {
        printf("✓ Cache invalidation test PASSED\n");
    } else {
        printf("✗ Cache invalidation test FAILED\n");
    }
}

void run_interactive_mode(void) {
    printf("\n=== Interactive Mode ===\n");
    printf("Commands: read <pos> <size>, write <pos> <data>, quit\n");

    char command[256];
    byte buffer[1024];

    while (running) {
        printf("dms> ");
        fflush(stdout);

        if (!fgets(command, sizeof(command), stdin)) {
            break;
        }

        // Remove newline
        command[strcspn(command, "\n")] = 0;

        if (strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0) {
            break;
        }

        char cmd[64], data[256];
        int pos, size;

        if (sscanf(command, "read %d %d", &pos, &size) == 2) {
            if (size > sizeof(buffer)) {
                printf("Error: Size too large (max %zu)\n", sizeof(buffer));
                continue;
            }

            int result = le(pos, buffer, size);
            if (result == DMS_SUCCESS) {
                printf("Read %d bytes from position %d:\n", size, pos);
                printf("Data: ");
                for (int i = 0; i < size; i++) {
                    if (buffer[i] >= 32 && buffer[i] <= 126) {
                        printf("%c", buffer[i]);
                    } else {
                        printf("\\x%02x", buffer[i]);
                    }
                }
                printf("\n");
            } else {
                printf("Error reading: %d\n", result);
            }
        } else if (sscanf(command, "write %d %s", &pos, data) == 2) {
            int result = escreve(pos, (byte *)data, strlen(data));
            if (result == DMS_SUCCESS) {
                printf("Wrote %zu bytes to position %d\n", strlen(data), pos);
            } else {
                printf("Error writing: %d\n", result);
            }
        } else {
            printf("Unknown command. Use: read <pos> <size>, write <pos> <data>, quit\n");
        }
    }
}

int main(int argc, char *argv[]) {
    dms_config_t config;
    int result;

    // Initialize MPI with thread support
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI não suporta múltiplas threads.\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // Get MPI info early for debugging
    int mpi_rank, mpi_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);

    printf("Distributed Shared Memory System - Process %d/%d\n", mpi_rank, mpi_size - 1);

    // Setup signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Parse configuration
    if (argc == 2 && access(argv[1], F_OK) == 0) {
        // Configuration file provided
        result = load_config_from_file(argv[1], &config);
        if (result != DMS_SUCCESS) {
            fprintf(stderr, "Process %d: Error loading configuration from file\n", mpi_rank);
            MPI_Finalize();
            return 1;
        }
    } else {
        // Parse command line arguments
        result = parse_command_line_config(argc, argv, &config);
        if (result != DMS_SUCCESS) {
            MPI_Finalize();
            return 1;
        }
    }

    // Check MPI size matches configuration
    if (mpi_size != config.n) {
        fprintf(stderr, "Process %d: MPI size (%d) doesn't match config n (%d)\n",
                mpi_rank, mpi_size, config.n);
        MPI_Finalize();
        return 1;
    }

    if (mpi_rank == 0) {
        print_config(&config);
    }

    // Initialize DMS
    result = dms_init(&config);
    if (result != DMS_SUCCESS) {
        fprintf(stderr, "Process %d: Error initializing DMS: %d\n", mpi_rank, result);
        MPI_Finalize();
        return 1;
    }

    // Synchronize all processes before starting tests
    pthread_mutex_lock(&dms_ctx->mpi_mutex);
    MPI_Barrier(MPI_COMM_WORLD);
    pthread_mutex_unlock(&dms_ctx->mpi_mutex);

    // Run tests based on process ID
    if (config.process_id == 0) {
        // Master process runs all tests
        printf("\nRunning DMS tests...\n");

        printf("\n--- TEST 1: BASIC OPERATIONS ---\n");
        dms_flush_local_cache();  // Fresh start
        test_basic_operations();

        printf("\n--- TEST 2: CROSS-BLOCK OPERATIONS ---\n");
        dms_flush_local_cache();  // Isolate from previous test
        test_cross_block_operations();

        printf("\n--- TEST 3: CACHE BEHAVIOR ---\n");
        dms_flush_local_cache();  // Isolate from previous test
        test_cache_behavior();

        printf("\n--- TEST 4: CACHE INVALIDATION ---\n");
        dms_flush_local_cache();  // Isolate from previous test
        test_cache_invalidation_scenario();

        // Run interactive mode
        run_interactive_mode();
    } else {
        // Other processes just handle requests in a loop
        printf("Process %d ready, handling requests...\n", config.process_id);

        while (running) {
            handle_incoming_messages();
            usleep(10000);  // 10ms delay
        }
    }

    // Cleanup
    printf("Process %d: Shutting down...\n", mpi_rank);
    running = 0;

    result = dms_cleanup();
    if (result != DMS_SUCCESS) {
        fprintf(stderr, "Process %d: Error during cleanup: %d\n", mpi_rank, result);
        MPI_Finalize();
        return 1;
    }

    // Finalize MPI
    MPI_Finalize();

    return 0;
}