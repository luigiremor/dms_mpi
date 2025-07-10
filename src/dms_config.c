#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dms.h"

int load_config_from_file(const char *filename, dms_config_t *config) {
    if (!filename || !config) {
        return DMS_ERROR_INVALID_PROCESS;
    }

    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Unable to open config file %s\n", filename);
        return DMS_ERROR_INVALID_PROCESS;
    }

    char line[256];
    config->n = 0;
    config->k = 0;
    config->t = 0;
    config->process_id = -1;

    while (fgets(line, sizeof(line), file)) {
        // Remove newline
        line[strcspn(line, "\n")] = 0;

        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }

        char key[64], value[64];
        if (sscanf(line, "%s %s", key, value) == 2) {
            if (strcmp(key, "processes") == 0 || strcmp(key, "n") == 0) {
                config->n = atoi(value);
            } else if (strcmp(key, "blocks") == 0 || strcmp(key, "k") == 0) {
                config->k = atoi(value);
            } else if (strcmp(key, "block_size") == 0 || strcmp(key, "t") == 0) {
                config->t = atoi(value);
            } else if (strcmp(key, "process_id") == 0 || strcmp(key, "pid") == 0) {
                config->process_id = atoi(value);
            }
        }
    }

    fclose(file);

    // Validate configuration
    if (config->n <= 0 || config->k <= 0 || config->t <= 0 ||
        config->process_id < 0 || config->process_id >= config->n) {
        return DMS_ERROR_INVALID_PROCESS;
    }

    return DMS_SUCCESS;
}

int parse_command_line_config(int argc, char *argv[], dms_config_t *config) {
    if (!config) {
        return DMS_ERROR_INVALID_PROCESS;
    }

    // Set defaults
    config->n = 4;           // 4 processes
    config->k = 1000;        // 1000 blocks
    config->t = 4096;        // 4KB blocks
    config->process_id = 0;  // default to process 0

    int opt;
    while ((opt = getopt(argc, argv, "n:k:t:p:h")) != -1) {
        switch (opt) {
            case 'n':
                config->n = atoi(optarg);
                break;
            case 'k':
                config->k = atoi(optarg);
                break;
            case 't':
                config->t = atoi(optarg);
                break;
            case 'p':
                config->process_id = atoi(optarg);
                break;
            case 'h':
                print_usage(argv[0]);
                exit(0);
            default:
                print_usage(argv[0]);
                return DMS_ERROR_INVALID_PROCESS;
        }
    }

    // Validate configuration
    if (config->n <= 0 || config->k <= 0 || config->t <= 0 ||
        config->process_id < 0 || config->process_id >= config->n) {
        fprintf(stderr, "Error: Invalid configuration parameters\n");
        return DMS_ERROR_INVALID_PROCESS;
    }

    return DMS_SUCCESS;
}

void print_usage(const char *program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  -n <num>     Number of processes (default: 4)\n");
    printf("  -k <num>     Number of blocks (default: 1000)\n");
    printf("  -t <num>     Block size in bytes (default: 4096)\n");
    printf("  -p <num>     Process ID (0 to n-1)\n");
    printf("  -h           Show this help message\n");
    printf("\nExample:\n");
    printf("  %s -n 4 -k 1000 -t 4096 -p 0\n", program_name);
    printf("\nOr use configuration file:\n");
    printf("  %s config.txt\n", program_name);
}

void print_config(const dms_config_t *config) {
    if (!config) return;

    printf("DMS Configuration:\n");
    printf("  Processes (n): %d\n", config->n);
    printf("  Blocks (k): %d\n", config->k);
    printf("  Block size (t): %d bytes\n", config->t);
    printf("  Process ID: %d\n", config->process_id);
    printf("  Total memory: %d bytes (%.2f MB)\n",
           config->k * config->t,
           (config->k * config->t) / (1024.0 * 1024.0));
    printf("  Local blocks per process: ~%d\n", config->k / config->n);
}
