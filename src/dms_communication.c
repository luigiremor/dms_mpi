#include <errno.h>
#include <mpi.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dms.h"

static inline size_t effective_size(const dms_message_t *msg) {
    return offsetof(dms_message_t, data) + msg->size;
}

int send_message(int target_pid, dms_message_t *msg) {
    if (!dms_ctx || !msg || target_pid < 0 || target_pid >= dms_ctx->config.n) {
        return DMS_ERROR_INVALID_PROCESS;
    }

    msg->source_pid = dms_ctx->mpi_rank;
    msg->target_pid = target_pid;

    size_t nbytes = effective_size(msg);

    pthread_mutex_lock(&dms_ctx->mpi_mutex);
    int result = MPI_Send(msg, nbytes, MPI_BYTE, target_pid, 0, MPI_COMM_WORLD);
    pthread_mutex_unlock(&dms_ctx->mpi_mutex);

    if (result != MPI_SUCCESS) {
        return DMS_ERROR_COMMUNICATION;
    }

    return DMS_SUCCESS;
}

int receive_message(dms_message_t *msg) {
    if (!dms_ctx || !msg) {
        return DMS_ERROR_COMMUNICATION;
    }

    MPI_Status status;
    int flag;

    pthread_mutex_lock(&dms_ctx->mpi_mutex);

    int result = MPI_Iprobe(MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &flag, &status);
    if (result != MPI_SUCCESS) {
        pthread_mutex_unlock(&dms_ctx->mpi_mutex);
        return DMS_ERROR_COMMUNICATION;
    }

    if (!flag) {
        pthread_mutex_unlock(&dms_ctx->mpi_mutex);
        return DMS_ERROR_COMMUNICATION;
    }

    int nbytes;
    MPI_Get_count(&status, MPI_BYTE, &nbytes);

    result = MPI_Recv(msg, nbytes, MPI_BYTE, status.MPI_SOURCE, 0, MPI_COMM_WORLD, &status);

    pthread_mutex_unlock(&dms_ctx->mpi_mutex);

    if (result != MPI_SUCCESS) {
        return DMS_ERROR_COMMUNICATION;
    }

    return DMS_SUCCESS;
}

int request_block_from_owner(int block_id, int owner_pid) {
    if (!dms_ctx || block_id < 0 || block_id >= dms_ctx->config.k) {
        return DMS_ERROR_INVALID_POSITION;
    }

    dms_message_t request;
    memset(&request, 0, sizeof(request));
    request.type = MSG_READ_REQUEST;
    request.block_id = block_id;
    request.size = 0;

    int result = send_message(owner_pid, &request);
    if (result != DMS_SUCCESS) {
        return result;
    }

    // Wait for response with timeout, actively handling other messages
    dms_message_t response;
    int attempts = 0;
    const int max_attempts = 1000;  // 1 second timeout

    while (attempts < max_attempts) {
        result = receive_message(&response);
        if (result == DMS_SUCCESS) {
            if (response.type == MSG_READ_RESPONSE && response.block_id == block_id) {
                cache_entry_t *cache_entry = allocate_cache_entry(block_id);
                if (!cache_entry) {
                    return DMS_ERROR_MEMORY;
                }

                pthread_mutex_lock(&cache_entry->mutex);
                memcpy(cache_entry->data, response.data, dms_ctx->config.t);
                cache_entry->valid = 1;
                pthread_mutex_unlock(&cache_entry->mutex);

                return DMS_SUCCESS;
            }
            // Handle other messages that aren't our response
            if (response.type != MSG_READ_RESPONSE &&
                response.type != MSG_WRITE_RESPONSE &&
                response.type != MSG_INVALIDATE_ACK) {
                handle_message(&response);
            }
        } else {
            usleep(1000);  // 1ms delay
            attempts++;
        }
    }

    return DMS_ERROR_COMMUNICATION;
}

int handle_message(dms_message_t *msg) {
    if (!dms_ctx || !msg) {
        return DMS_ERROR_COMMUNICATION;
    }

    if (msg->type == MSG_READ_RESPONSE ||
        msg->type == MSG_WRITE_RESPONSE ||
        msg->type == MSG_INVALIDATE_ACK) {
        return DMS_SUCCESS;
    }

    printf("DEBUG: Process %d handling message type %d from process %d for block %d\n",
           dms_ctx->mpi_rank, msg->type, msg->source_pid, msg->block_id);

    switch (msg->type) {
        case MSG_READ_REQUEST: {
            printf("DEBUG: Process %d processing read request\n", dms_ctx->mpi_rank);
            byte *local_data = get_local_block_data(msg->block_id);
            if (!local_data) {
                printf("DEBUG: Process %d did not found block %d locally\n", dms_ctx->mpi_rank, msg->block_id);
                return DMS_ERROR_BLOCK_NOT_FOUND;
            }

            dms_message_t response;
            memset(&response, 0, sizeof(response));
            response.type = MSG_READ_RESPONSE;
            response.block_id = msg->block_id;
            response.size = dms_ctx->config.t;
            memcpy(response.data, local_data, dms_ctx->config.t);

            printf("DEBUG: Process %d sending read response\n", dms_ctx->mpi_rank);
            return send_message(msg->source_pid, &response);
        }

        case MSG_WRITE_REQUEST: {
            printf("DEBUG: Process %d processing write request\n", dms_ctx->mpi_rank);
            byte *local_data = get_local_block_data(msg->block_id);
            if (!local_data) {
                printf("DEBUG: Process %d did not found block %d locally\n", dms_ctx->mpi_rank, msg->block_id);
                return DMS_ERROR_BLOCK_NOT_FOUND;
            }

            int offset = msg->position;
            int size = msg->size;
            if (offset >= 0 && offset + size <= dms_ctx->config.t) {
                memcpy(local_data + offset, msg->data, size);
                printf("DEBUG: Process %d updated block %d\n", dms_ctx->mpi_rank, msg->block_id);
            }

            printf("DEBUG: Process %d invalidating caches and waiting for ACKs\n", dms_ctx->mpi_rank);
            int invalidate_result = invalidate_cache_and_wait_acks(msg->block_id, msg->source_pid);
            if (invalidate_result != DMS_SUCCESS) {
                printf("DEBUG: Process %d failed to invalidate caches\n", dms_ctx->mpi_rank);
                return invalidate_result;
            }

            dms_message_t response;
            memset(&response, 0, sizeof(response));
            response.type = MSG_WRITE_RESPONSE;
            response.block_id = msg->block_id;
            response.size = 0;

            printf("DEBUG: Process %d sending write response after invalidation complete\n", dms_ctx->mpi_rank);
            return send_message(msg->source_pid, &response);
        }

        case MSG_INVALIDATE: {
            printf("DEBUG: Process %d processing invalidate request\n", dms_ctx->mpi_rank);
            cache_entry_t *entry = find_cache_entry(msg->block_id);
            if (entry) {
                pthread_mutex_lock(&entry->mutex);
                entry->valid = 0;
                entry->dirty = 0;
                pthread_mutex_unlock(&entry->mutex);
                printf("DEBUG: Process %d invalidated cache for block %d\n", dms_ctx->mpi_rank, msg->block_id);
            }

            dms_message_t response;
            memset(&response, 0, sizeof(response));
            response.type = MSG_INVALIDATE_ACK;
            response.block_id = msg->block_id;
            response.size = 0;

            printf("DEBUG: Process %d sending invalidate ack\n", dms_ctx->mpi_rank);
            return send_message(msg->source_pid, &response);
        }

        default:
            printf("DEBUG: Process %d doesn't recognize message type %d\n", dms_ctx->mpi_rank, msg->type);
            break;
    }

    return DMS_SUCCESS;
}

int invalidate_cache_and_wait_acks(int block_id, int requester_pid) {
    if (!dms_ctx) {
        return DMS_ERROR_COMMUNICATION;
    }

    dms_message_t invalidate_msg;
    memset(&invalidate_msg, 0, sizeof(invalidate_msg));
    invalidate_msg.type = MSG_INVALIDATE;
    invalidate_msg.block_id = block_id;
    invalidate_msg.size = 0;

    int expected_acks = 0;
    for (int i = 0; i < dms_ctx->config.n; i++) {
        if (i != dms_ctx->mpi_rank && i != requester_pid) {
            if (send_message(i, &invalidate_msg) == DMS_SUCCESS) {
                expected_acks++;
            }
        }
    }

    if (expected_acks == 0) {
        return DMS_SUCCESS;
    }

    int received_acks = 0;
    int attempts = 0;
    const int max_attempts = 1000;
    dms_message_t response;

    while (received_acks < expected_acks && attempts < max_attempts) {
        if (receive_message(&response) == DMS_SUCCESS) {
            if (response.type == MSG_INVALIDATE_ACK && response.block_id == block_id) {
                received_acks++;
            } else {
                handle_message(&response);
            }
        } else {
            usleep(1000);
            attempts++;
        }
    }

    if (received_acks < expected_acks) {
        return DMS_ERROR_COMMUNICATION;
    }

    return DMS_SUCCESS;
}

int handle_incoming_messages(void) {
    if (!dms_ctx) {
        return DMS_ERROR_COMMUNICATION;
    }

    dms_message_t msg;

    while (receive_message(&msg) == DMS_SUCCESS) {
        handle_message(&msg);
    }

    return DMS_SUCCESS;
}
