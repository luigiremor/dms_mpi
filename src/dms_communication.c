#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dms.h"

int send_message(int target_pid, dms_message_t *msg) {
    if (!dms_ctx || !msg || target_pid < 0 || target_pid >= dms_ctx->config.n) {
        return DMS_ERROR_INVALID_PROCESS;
    }

    char target_queue_name[32];
    snprintf(target_queue_name, sizeof(target_queue_name), "/dms_queue_%d", target_pid);

    mqd_t target_queue = mq_open(target_queue_name, O_WRONLY);
    if (target_queue == (mqd_t)-1) {
        return DMS_ERROR_COMMUNICATION;
    }

    msg->source_pid = dms_ctx->config.process_id;
    msg->target_pid = target_pid;

    int result = mq_send(target_queue, (const char *)msg, sizeof(dms_message_t), 0);
    mq_close(target_queue);

    if (result == -1) {
        return DMS_ERROR_COMMUNICATION;
    }

    return DMS_SUCCESS;
}

int receive_message(dms_message_t *msg) {
    if (!dms_ctx || !msg) {
        return DMS_ERROR_COMMUNICATION;
    }

    ssize_t bytes_received = mq_receive(dms_ctx->message_queue, (char *)msg, sizeof(dms_message_t), NULL);

    if (bytes_received == -1) {
        if (errno == EAGAIN) {
            return DMS_ERROR_COMMUNICATION;  // No message available
        }
        return DMS_ERROR_COMMUNICATION;
    }

    if (bytes_received != sizeof(dms_message_t)) {
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

    int result = send_message(owner_pid, &request);
    if (result != DMS_SUCCESS) {
        return result;
    }

    // Wait for response
    dms_message_t response;
    while (1) {
        result = receive_message(&response);
        if (result == DMS_SUCCESS) {
            if (response.type == MSG_READ_RESPONSE && response.block_id == block_id) {
                // Found our response
                cache_entry_t *cache_entry = allocate_cache_entry(block_id);
                if (!cache_entry) {
                    return DMS_ERROR_MEMORY;
                }

                pthread_mutex_lock(&cache_entry->mutex);
                memcpy(cache_entry->data, response.data, dms_ctx->config.t);
                pthread_mutex_unlock(&cache_entry->mutex);

                break;
            }
            // If it's not our response, handle it separately
            handle_message(&response);
        } else {
            // Continue waiting or handle timeout
            usleep(1000);  // 1ms delay
        }
    }

    return DMS_SUCCESS;
}

int handle_message(dms_message_t *msg) {
    if (!dms_ctx || !msg) {
        return DMS_ERROR_COMMUNICATION;
    }

    switch (msg->type) {
        case MSG_READ_REQUEST: {
            // Someone wants to read a block we own
            byte *local_data = get_local_block_data(msg->block_id);
            if (!local_data) {
                return DMS_ERROR_BLOCK_NOT_FOUND;
            }

            dms_message_t response;
            memset(&response, 0, sizeof(response));
            response.type = MSG_READ_RESPONSE;
            response.block_id = msg->block_id;
            memcpy(response.data, local_data, dms_ctx->config.t);

            return send_message(msg->source_pid, &response);
        }

        case MSG_WRITE_REQUEST: {
            // Someone wants to write to a block we own
            byte *local_data = get_local_block_data(msg->block_id);
            if (!local_data) {
                return DMS_ERROR_BLOCK_NOT_FOUND;
            }

            // Update local data
            int offset = msg->position;
            int size = msg->size;
            if (offset >= 0 && offset + size <= dms_ctx->config.t) {
                memcpy(local_data + offset, msg->data, size);
            }

            // Send acknowledgment
            dms_message_t response;
            memset(&response, 0, sizeof(response));
            response.type = MSG_WRITE_RESPONSE;
            response.block_id = msg->block_id;

            int result = send_message(msg->source_pid, &response);

            // Invalidate cache entries in other processes
            invalidate_cache_in_other_processes(msg->block_id);

            return result;
        }

        case MSG_INVALIDATE: {
            // Invalidate cache entry for this block
            cache_entry_t *entry = find_cache_entry(msg->block_id);
            if (entry) {
                pthread_mutex_lock(&entry->mutex);
                entry->valid = 0;
                entry->dirty = 0;
                pthread_mutex_unlock(&entry->mutex);
            }

            // Send acknowledgment
            dms_message_t response;
            memset(&response, 0, sizeof(response));
            response.type = MSG_INVALIDATE_ACK;
            response.block_id = msg->block_id;

            return send_message(msg->source_pid, &response);
        }

        case MSG_READ_RESPONSE:
        case MSG_WRITE_RESPONSE:
        case MSG_INVALIDATE_ACK:
            // These should be handled by the calling function
            break;
    }

    return DMS_SUCCESS;
}

int invalidate_cache_in_other_processes(int block_id) {
    if (!dms_ctx) {
        return DMS_ERROR_COMMUNICATION;
    }

    dms_message_t invalidate_msg;
    memset(&invalidate_msg, 0, sizeof(invalidate_msg));
    invalidate_msg.type = MSG_INVALIDATE;
    invalidate_msg.block_id = block_id;

    for (int i = 0; i < dms_ctx->config.n; i++) {
        if (i != dms_ctx->config.process_id) {
            send_message(i, &invalidate_msg);
        }
    }

    return DMS_SUCCESS;
}

int handle_incoming_messages(void) {
    if (!dms_ctx) {
        return DMS_ERROR_COMMUNICATION;
    }

    dms_message_t msg;

    // Set non-blocking mode for the queue
    struct mq_attr old_attr, new_attr;
    mq_getattr(dms_ctx->message_queue, &old_attr);
    new_attr = old_attr;
    new_attr.mq_flags = O_NONBLOCK;
    mq_setattr(dms_ctx->message_queue, &new_attr, NULL);

    while (receive_message(&msg) == DMS_SUCCESS) {
        handle_message(&msg);
    }

    // Restore blocking mode
    mq_setattr(dms_ctx->message_queue, &old_attr, NULL);

    return DMS_SUCCESS;
}

void *message_handler_thread(void *arg) {
    while (dms_ctx) {
        handle_incoming_messages();
        usleep(10000);  // 10ms delay
    }
    return NULL;
}