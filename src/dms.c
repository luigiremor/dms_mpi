#include "dms.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

dms_context_t *dms_ctx = NULL;

int dms_init(dms_config_t *config) {
    if (!config || config->n <= 0 || config->k <= 0 || config->t <= 0) {
        return DMS_ERROR_INVALID_PROCESS;
    }

    if (config->process_id < 0 || config->process_id >= config->n) {
        return DMS_ERROR_INVALID_PROCESS;
    }

    dms_ctx = malloc(sizeof(dms_context_t));
    if (!dms_ctx) {
        return DMS_ERROR_MEMORY;
    }

    memset(dms_ctx, 0, sizeof(dms_context_t));
    memcpy(&dms_ctx->config, config, sizeof(dms_config_t));

    // Calculate how many blocks this process owns
    int blocks_per_process = config->k / config->n;
    int extra_blocks = config->k % config->n;
    int local_blocks = blocks_per_process;
    if (config->process_id < extra_blocks) {
        local_blocks++;
    }

    size_t local_storage_size = local_blocks * config->t;
    dms_ctx->blocks = malloc(local_storage_size);
    if (!dms_ctx->blocks) {
        free(dms_ctx);
        return DMS_ERROR_MEMORY;
    }
    memset(dms_ctx->blocks, 0, local_storage_size);

    dms_ctx->block_owners = malloc(config->k * sizeof(int));
    if (!dms_ctx->block_owners) {
        free(dms_ctx->blocks);
        free(dms_ctx);
        return DMS_ERROR_MEMORY;
    }

    // Calculate block ownership using round-robin distribution
    for (int i = 0; i < config->k; i++) {
        dms_ctx->block_owners[i] = i % config->n;
    }

    for (int i = 0; i < CACHE_SIZE; i++) {
        dms_ctx->cache[i].block_id = -1;
        dms_ctx->cache[i].data = malloc(config->t);
        if (!dms_ctx->cache[i].data) {
            for (int j = 0; j < i; j++) {
                free(dms_ctx->cache[j].data);
            }
            free(dms_ctx->block_owners);
            free(dms_ctx->blocks);
            free(dms_ctx);
            return DMS_ERROR_MEMORY;
        }
        dms_ctx->cache[i].valid = 0;
        dms_ctx->cache[i].dirty = 0;
        pthread_mutex_init(&dms_ctx->cache[i].mutex, NULL);
    }

    pthread_mutex_init(&dms_ctx->cache_mutex, NULL);
    pthread_mutex_init(&dms_ctx->mpi_mutex, NULL);

    MPI_Comm_rank(MPI_COMM_WORLD, &dms_ctx->mpi_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &dms_ctx->mpi_size);

    // Verify MPI configuration matches DMS configuration
    if (dms_ctx->mpi_size != config->n) {
        for (int i = 0; i < CACHE_SIZE; i++) {
            free(dms_ctx->cache[i].data);
            pthread_mutex_destroy(&dms_ctx->cache[i].mutex);
        }
        pthread_mutex_destroy(&dms_ctx->cache_mutex);
        pthread_mutex_destroy(&dms_ctx->mpi_mutex);
        free(dms_ctx->block_owners);
        free(dms_ctx->blocks);
        free(dms_ctx);
        return DMS_ERROR_COMMUNICATION;
    }

    // Use MPI rank as process ID to ensure consistency
    config->process_id = dms_ctx->mpi_rank;
    dms_ctx->config.process_id = dms_ctx->mpi_rank;

    return DMS_SUCCESS;
}

int get_block_owner(int block_id) {
    if (!dms_ctx || block_id < 0 || block_id >= dms_ctx->config.k) {
        return -1;
    }
    return dms_ctx->block_owners[block_id];
}

int get_block_from_position(int position) {
    if (!dms_ctx || position < 0) {
        return -1;
    }
    return position / dms_ctx->config.t;
}

int get_offset_in_block(int position) {
    if (!dms_ctx || position < 0) {
        return -1;
    }
    return position % dms_ctx->config.t;
}

cache_entry_t *find_cache_entry(int block_id) {
    if (!dms_ctx) {
        return NULL;
    }

    for (int i = 0; i < CACHE_SIZE; i++) {
        if (dms_ctx->cache[i].block_id == block_id && dms_ctx->cache[i].valid) {
            return &dms_ctx->cache[i];
        }
    }
    return NULL;
}

int write_back_dirty_entry(cache_entry_t *entry) {
    if (!entry || !entry->dirty || !entry->valid) {
        return DMS_SUCCESS;
    }

    int owner = get_block_owner(entry->block_id);
    if (owner < 0 || owner >= dms_ctx->config.n) {
        return DMS_ERROR_INVALID_PROCESS;
    }

    dms_message_t write_request;
    memset(&write_request, 0, sizeof(write_request));
    write_request.type = MSG_WRITE_REQUEST;
    write_request.block_id = entry->block_id;
    write_request.position = 0;  // Write entire block
    write_request.size = dms_ctx->config.t;
    memcpy(write_request.data, entry->data, dms_ctx->config.t);

    printf("DEBUG: Process %d writing back dirty block %d to owner %d\n",
           dms_ctx->mpi_rank, entry->block_id, owner);

    int result = send_message(owner, &write_request);
    if (result != DMS_SUCCESS) {
        return result;
    }

    // Wait for write confirmation
    dms_message_t response;
    int attempts = 0;
    const int max_attempts = 1000;

    while (attempts < max_attempts) {
        result = receive_message(&response);
        if (result == DMS_SUCCESS) {
            if (response.type == MSG_WRITE_RESPONSE &&
                response.block_id == entry->block_id) {
                printf("DEBUG: Process %d got write-back confirmation for block %d\n",
                       dms_ctx->mpi_rank, entry->block_id);
                return DMS_SUCCESS;
            } else {
                handle_message(&response);
            }
        } else {
            usleep(1000);
            attempts++;
        }
    }

    return DMS_ERROR_COMMUNICATION;
}

cache_entry_t *allocate_cache_entry(int block_id) {
    if (!dms_ctx) {
        return NULL;
    }

    pthread_mutex_lock(&dms_ctx->cache_mutex);

    // First, try to find an invalid entry
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (!dms_ctx->cache[i].valid) {
            dms_ctx->cache[i].block_id = block_id;
            dms_ctx->cache[i].valid = 1;
            dms_ctx->cache[i].dirty = 0;
            pthread_mutex_unlock(&dms_ctx->cache_mutex);
            return &dms_ctx->cache[i];
        }
    }

    // If no invalid entry, use round-robin replacement with write-back
    static int next_victim = 0;
    cache_entry_t *victim = &dms_ctx->cache[next_victim];
    next_victim = (next_victim + 1) % CACHE_SIZE;

    pthread_mutex_lock(&victim->mutex);

    if (victim->valid && victim->dirty) {
        printf("DEBUG: Process %d evicting dirty block %d, performing write-back\n",
               dms_ctx->mpi_rank, victim->block_id);

        int write_back_result = write_back_dirty_entry(victim);
        if (write_back_result != DMS_SUCCESS) {
            printf("ERROR: Process %d failed to write-back dirty block %d (error: %d)\n",
                   dms_ctx->mpi_rank, victim->block_id, write_back_result);
            pthread_mutex_unlock(&victim->mutex);
            pthread_mutex_unlock(&dms_ctx->cache_mutex);
            return NULL;
        }
    }

    // Now safe to replace the victim entry
    victim->block_id = block_id;
    victim->valid = 1;
    victim->dirty = 0;

    pthread_mutex_unlock(&victim->mutex);
    pthread_mutex_unlock(&dms_ctx->cache_mutex);

    return victim;
}

byte *get_local_block_data(int block_id) {
    if (!dms_ctx || block_id < 0 || block_id >= dms_ctx->config.k) {
        return NULL;
    }

    int owner = get_block_owner(block_id);
    if (owner != dms_ctx->config.process_id) {
        return NULL;
    }

    // Calculate local offset for this block
    int local_block_index = 0;
    for (int i = 0; i < block_id; i++) {
        if (get_block_owner(i) == dms_ctx->config.process_id) {
            local_block_index++;
        }
    }

    return dms_ctx->blocks + (local_block_index * dms_ctx->config.t);
}

void dms_flush_local_cache(void) {
    if (!dms_ctx) return;

    pthread_mutex_lock(&dms_ctx->cache_mutex);

    printf("DEBUG: Flushing local cache (128 entries) with write-back...\n");

    // First pass: write-back all dirty entries
    for (int i = 0; i < CACHE_SIZE; i++) {
        pthread_mutex_lock(&dms_ctx->cache[i].mutex);
        if (dms_ctx->cache[i].valid && dms_ctx->cache[i].dirty) {
            printf("DEBUG: Writing back dirty block %d during cache flush\n",
                   dms_ctx->cache[i].block_id);
            int result = write_back_dirty_entry(&dms_ctx->cache[i]);
            if (result != DMS_SUCCESS) {
                printf("ERROR: Failed to write-back dirty block %d during flush (error: %d)\n",
                       dms_ctx->cache[i].block_id, result);
            }
        }
        pthread_mutex_unlock(&dms_ctx->cache[i].mutex);
    }

    // Second pass: invalidate all entries
    for (int i = 0; i < CACHE_SIZE; i++) {
        pthread_mutex_lock(&dms_ctx->cache[i].mutex);
        dms_ctx->cache[i].valid = 0;
        dms_ctx->cache[i].dirty = 0;
        dms_ctx->cache[i].block_id = -1;
        pthread_mutex_unlock(&dms_ctx->cache[i].mutex);
    }

    pthread_mutex_unlock(&dms_ctx->cache_mutex);
    printf("DEBUG: Cache flush complete\n");
}

int dms_cleanup(void) {
    if (!dms_ctx) {
        return DMS_SUCCESS;
    }

    for (int i = 0; i < CACHE_SIZE; i++) {
        if (dms_ctx->cache[i].data) {
            free(dms_ctx->cache[i].data);
        }
        pthread_mutex_destroy(&dms_ctx->cache[i].mutex);
    }

    pthread_mutex_destroy(&dms_ctx->cache_mutex);
    pthread_mutex_destroy(&dms_ctx->mpi_mutex);

    if (dms_ctx->blocks) {
        free(dms_ctx->blocks);
    }

    if (dms_ctx->block_owners) {
        free(dms_ctx->block_owners);
    }

    free(dms_ctx);
    dms_ctx = NULL;

    return DMS_SUCCESS;
}
