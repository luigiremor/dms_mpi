#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "dms.h"

static int running = 1;
static pthread_t message_thread;

void signal_handler(int sig) {
    running = 0;
}

void test_basic_operations(void) {
    printf("\n=== Testing Basic Operations ===\n");

    // Test writing and reading a simple string
    const char *test_string = "ALO MUNDO";
    byte buffer[256];
    int result;

    printf("Writing '%s' to position 0...\n", test_string);
    result = escreve(0, (byte *)test_string, strlen(test_string));
    if (result != DMS_SUCCESS) {
        printf("Error writing: %d\n", result);
        return;
    }

    printf("Reading from position 0...\n");
    memset(buffer, 0, sizeof(buffer));
    result = le(0, buffer, strlen(test_string));
    if (result != DMS_SUCCESS) {
        printf("Error reading: %d\n", result);
        return;
    }

    printf("Read: '%s'\n", buffer);
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

    printf("Writing %d bytes starting at position %d (crosses block boundary)...\n",
           len, cross_position);
    result = escreve(cross_position, (byte *)long_string, len);
    if (result != DMS_SUCCESS) {
        printf("Error writing cross-block: %d\n", result);
        return;
    }

    printf("Reading back the cross-block data...\n");
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
        printf("No remote blocks available for cache testing\n");
        return;
    }

    int remote_position = remote_block * dms_ctx->config.t;

    printf("First read from remote block %d (should cause cache miss)...\n", remote_block);
    result = le(remote_position, buffer1, 32);
    if (result != DMS_SUCCESS) {
        printf("Error in first read: %d\n", result);
        return;
    }

    printf("Second read from same remote block (should hit cache)...\n");
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

    printf("Distributed Shared Memory System\n");
    printf("================================\n");

    // Setup signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Parse configuration
    if (argc == 2 && access(argv[1], F_OK) == 0) {
        // Configuration file provided
        result = load_config_from_file(argv[1], &config);
        if (result != DMS_SUCCESS) {
            fprintf(stderr, "Error loading configuration from file\n");
            return 1;
        }
    } else {
        // Parse command line arguments
        result = parse_command_line_config(argc, argv, &config);
        if (result != DMS_SUCCESS) {
            return 1;
        }
    }

    print_config(&config);

    // Initialize DMS
    printf("\nInitializing DMS...\n");
    result = dms_init(&config);
    if (result != DMS_SUCCESS) {
        fprintf(stderr, "Error initializing DMS: %d\n", result);
        return 1;
    }

    printf("DMS initialized successfully!\n");

    // Start message handler thread
    if (pthread_create(&message_thread, NULL, message_handler_thread, NULL) != 0) {
        fprintf(stderr, "Error creating message handler thread\n");
        dms_cleanup();
        return 1;
    }

    // Give some time for all processes to start
    sleep(2);

    // Run tests based on process ID
    if (config.process_id == 0) {
        // Master process runs all tests
        test_basic_operations();
        test_cross_block_operations();
        test_cache_behavior();

        // Run interactive mode
        run_interactive_mode();
    } else {
        // Other processes just handle requests
        printf("Process %d ready, handling requests...\n", config.process_id);
        printf("Press Ctrl+C to exit\n");

        while (running) {
            sleep(1);
        }
    }

    // Cleanup
    printf("\nShutting down...\n");
    running = 0;

    // Wait for message thread to finish
    pthread_join(message_thread, NULL);

    result = dms_cleanup();
    if (result != DMS_SUCCESS) {
        fprintf(stderr, "Error during cleanup: %d\n", result);
        return 1;
    }

    printf("DMS shutdown complete\n");
    return 0;
}