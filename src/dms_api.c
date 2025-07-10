#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dms.h"

int le(int posicao, byte *buffer, int tamanho) {
    if (!dms_ctx || !buffer || posicao < 0 || tamanho <= 0) {
        return DMS_ERROR_INVALID_POSITION;
    }

    int total_memory_size = dms_ctx->config.k * dms_ctx->config.t;
    if (posicao + tamanho > total_memory_size) {
        return DMS_ERROR_INVALID_SIZE;
    }

    int bytes_read = 0;

    while (bytes_read < tamanho) {
        int current_position = posicao + bytes_read;
        int block_id = get_block_from_position(current_position);
        int offset_in_block = get_offset_in_block(current_position);
        int owner = get_block_owner(block_id);

        if (block_id < 0 || block_id >= dms_ctx->config.k) {
            return DMS_ERROR_INVALID_POSITION;
        }

        // Calculate how much we can read from this block
        int remaining_in_block = dms_ctx->config.t - offset_in_block;
        int remaining_to_read = tamanho - bytes_read;
        int bytes_to_read = (remaining_in_block < remaining_to_read) ? remaining_in_block : remaining_to_read;

        byte *data_source = NULL;

        if (owner == dms_ctx->config.process_id) {
            // Local block - read directly
            data_source = get_local_block_data(block_id);
            if (!data_source) {
                return DMS_ERROR_BLOCK_NOT_FOUND;
            }
        } else {
            // Remote block - check cache first
            printf("DEBUG: Process %d reading from remote block %d (owner=%d)\n",
                    dms_ctx->mpi_rank, block_id, owner);

            cache_entry_t *cache_entry = find_cache_entry(block_id);

            if (cache_entry && cache_entry->valid) {
                // Cache hit - read from cache
                printf("DEBUG: Cache hit for block %d\n", block_id);
                pthread_mutex_lock(&cache_entry->mutex);
                data_source = cache_entry->data;
            } else {
                // Cache miss - request block from owner
                printf("DEBUG: Cache miss for block %d, requesting from owner\n", block_id);
                int result = request_block_from_owner(block_id, owner);
                if (result != DMS_SUCCESS) {
                    printf("DEBUG: Failed to get remote block %d\n", block_id);
                    return result;
                }

                // Now find the cache entry (should be available)
                cache_entry = find_cache_entry(block_id);
                if (!cache_entry) {
                    printf("DEBUG: Cache entry not found after request\n");
                    return DMS_ERROR_MEMORY;
                }

                pthread_mutex_lock(&cache_entry->mutex);
                data_source = cache_entry->data;
            }
        }

        // Copy data to user buffer
        memcpy(buffer + bytes_read, data_source + offset_in_block, bytes_to_read);

        // Unlock cache entry if it was locked
        if (owner != dms_ctx->config.process_id) {
            cache_entry_t *cache_entry = find_cache_entry(block_id);
            if (cache_entry) {
                pthread_mutex_unlock(&cache_entry->mutex);
            }
        }

        bytes_read += bytes_to_read;
    }

    return DMS_SUCCESS;
}

int escreve(int posicao, byte *buffer, int tamanho) {
    if (!dms_ctx || !buffer || posicao < 0 || tamanho <= 0) {
        return DMS_ERROR_INVALID_POSITION;
    }

    int total_memory_size = dms_ctx->config.k * dms_ctx->config.t;
    if (posicao + tamanho > total_memory_size) {
        return DMS_ERROR_INVALID_SIZE;
    }

    int bytes_written = 0;

    while (bytes_written < tamanho) {
        int current_position = posicao + bytes_written;
        int block_id = get_block_from_position(current_position);
        int offset_in_block = get_offset_in_block(current_position);
        int owner = get_block_owner(block_id);

        if (block_id < 0 || block_id >= dms_ctx->config.k) {
            return DMS_ERROR_INVALID_POSITION;
        }

        // Calculate how much we can write to this block
        int remaining_in_block = dms_ctx->config.t - offset_in_block;
        int remaining_to_write = tamanho - bytes_written;
        int bytes_to_write = (remaining_in_block < remaining_to_write) ? remaining_in_block : remaining_to_write;

        if (owner == dms_ctx->config.process_id) {
            // Local block - write directly
            printf("DEBUG: Process %d writing to local block\n", dms_ctx->mpi_rank);
            byte *local_data = get_local_block_data(block_id);
            if (!local_data) {
                return DMS_ERROR_BLOCK_NOT_FOUND;
            }

            memcpy(local_data + offset_in_block, buffer + bytes_written, bytes_to_write);

            // Invalidate cache entries in other processes
            invalidate_cache_in_other_processes(block_id);

        } else {
            // Remote block - send write request to owner
            printf("DEBUG: Process %d writing to remote block %d (owner=%d)\n",
                    dms_ctx->mpi_rank, block_id, owner);

            dms_message_t write_request;
            memset(&write_request, 0, sizeof(write_request));
            write_request.type = MSG_WRITE_REQUEST;
            write_request.block_id = block_id;
            write_request.position = offset_in_block;
            write_request.size = bytes_to_write;
            memcpy(write_request.data, buffer + bytes_written, bytes_to_write);

            int result = send_message(owner, &write_request);
            if (result != DMS_SUCCESS) {
                printf("DEBUG: Failed to send write request\n");
                return result;
            }

            printf("DEBUG: Process %d sent write request, waiting for response...\n", dms_ctx->mpi_rank);

            // Wait for acknowledgment with timeout
            dms_message_t response;
            int attempts = 0;
            const int max_attempts = 1000;  // 1 second timeout
            int found_response = 0;

            while (attempts < max_attempts && !found_response) {
                result = receive_message(&response);
                if (result == DMS_SUCCESS) {
                    if (response.type == MSG_WRITE_RESPONSE && response.block_id == block_id) {
                        printf("DEBUG: Process %d got write response\n", dms_ctx->mpi_rank);
                        found_response = 1;
                    } else {
                        // Handle other messages that aren't our response
                        printf("DEBUG: Process %d handling other message type %d\n", dms_ctx->mpi_rank, response.type);
                        handle_message(&response);
                    }
                } else {
                    // No message available, process other messages
                    attempts++;
                    usleep(1000);  // 1ms delay
                }
            }

            if (!found_response) {
                printf("DEBUG: Process %d timed out waiting for write response\n");
                return DMS_ERROR_COMMUNICATION;
            }

            // Invalidate our own cache entry for this block
            cache_entry_t *cache_entry = find_cache_entry(block_id);
            if (cache_entry) {
                pthread_mutex_lock(&cache_entry->mutex);
                cache_entry->valid = 0;
                cache_entry->dirty = 0;
                pthread_mutex_unlock(&cache_entry->mutex);
            }
        }

        bytes_written += bytes_to_write;
    }

    return DMS_SUCCESS;
}

int invalidate_cache_entry(int block_id) {
    if (!dms_ctx || block_id < 0 || block_id >= dms_ctx->config.k) {
        return DMS_ERROR_INVALID_POSITION;
    }

    cache_entry_t *entry = find_cache_entry(block_id);
    if (entry) {
        pthread_mutex_lock(&entry->mutex);
        entry->valid = 0;
        entry->dirty = 0;
        pthread_mutex_unlock(&entry->mutex);
    }

    return DMS_SUCCESS;
}