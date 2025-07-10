#ifndef DMS_H
#define DMS_H

#include <fcntl.h>
#include <mpi.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/types.h>

#define MAX_PROCESSES 16
#define MAX_BLOCK_SIZE 4096
#define MAX_BLOCKS 1000000
#define CACHE_SIZE 128
#define MESSAGE_SIZE 256

typedef uint8_t byte;

typedef enum {
    DMS_SUCCESS = 0,
    DMS_ERROR_INVALID_POSITION = -1,
    DMS_ERROR_INVALID_SIZE = -2,
    DMS_ERROR_BLOCK_NOT_FOUND = -3,
    DMS_ERROR_COMMUNICATION = -4,
    DMS_ERROR_MEMORY = -5,
    DMS_ERROR_INVALID_PROCESS = -6
} dms_error_t;

typedef enum {
    MSG_READ_REQUEST,
    MSG_READ_RESPONSE,
    MSG_WRITE_REQUEST,
    MSG_WRITE_RESPONSE,
    MSG_INVALIDATE,
    MSG_INVALIDATE_ACK
} message_type_t;

typedef struct {
    int n;           // number of processes
    int k;           // number of blocks
    int t;           // block size in bytes
    int process_id;  // current process ID
} dms_config_t;

typedef struct {
    int block_id;
    byte *data;
    int valid;
    int dirty;
    pthread_mutex_t mutex;
} cache_entry_t;

typedef struct {
    message_type_t type;
    int source_pid;
    int target_pid;
    int block_id;
    int position;
    int size;
    byte data[MAX_BLOCK_SIZE];
} dms_message_t;

typedef struct {
    dms_config_t config;
    byte *blocks;
    int *block_owners;
    cache_entry_t cache[CACHE_SIZE];
    pthread_mutex_t cache_mutex;
    pthread_mutex_t mpi_mutex;  // New mutex for MPI operations
    int mpi_rank;
    int mpi_size;
} dms_context_t;

extern dms_context_t *dms_ctx;

// API Functions
int dms_init(dms_config_t *config);
int le(int posicao, byte *buffer, int tamanho);
int escreve(int posicao, byte *buffer, int tamanho);
int dms_cleanup(void);
void dms_flush_local_cache(void);

// Internal Functions
int get_block_owner(int block_id);
int get_block_from_position(int position);
int get_offset_in_block(int position);
cache_entry_t *find_cache_entry(int block_id);
cache_entry_t *allocate_cache_entry(int block_id);
int request_block_from_owner(int block_id, int owner_pid);
int send_message(int target_pid, dms_message_t *msg);
int receive_message(dms_message_t *msg);
int invalidate_cache_entry(int block_id);
int handle_incoming_messages(void);
byte *get_local_block_data(int block_id);
int handle_message(dms_message_t *msg);
int invalidate_cache_and_wait_acks(int block_id, int requester_pid);

// Configuration Functions
int load_config_from_file(const char *filename, dms_config_t *config);
int parse_command_line_config(int argc, char *argv[], dms_config_t *config);
void print_usage(const char *program_name);
void print_config(const dms_config_t *config);

#endif  // DMS_H