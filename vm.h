#pragma once
#include "generator.h"
#include "variables.h"
#define AMTAIL_VM_STACK_SIZE 1024
#define AMTAIL_CAPTURE_MAX 64
#define AMTAIL_SPLIT_MAX 64
#define AMTAIL_SPLIT_BIND_MAX 64
#define AMTAIL_SPLIT_ARRAYS_MAX 16

typedef struct amtail_capture_slice {
	const char *ptr;
	uint32_t len;
} amtail_capture_slice;

typedef struct amtail_named_capture_slot {
	const char *name;
	uint8_t name_len;
	amtail_capture_slice slice;
} amtail_named_capture_slot;

typedef struct amtail_named_split_array {
	char name[AMTAIL_SPLIT_BIND_MAX];
	uint8_t name_len;
	char *storage;
	amtail_capture_slice parts[AMTAIL_SPLIT_MAX];
	uint8_t count;
} amtail_named_split_array;

typedef struct amtail_touch_callbacks {
	void *userdata;
	void (*on_var_touched)(void *userdata, amtail_variable *var);
} amtail_touch_callbacks;

typedef struct amtail_thread {
    amtail_byteop* stack[AMTAIL_VM_STACK_SIZE];
    uint16_t stack_ptr;
    amtail_byteop temp_pool[AMTAIL_VM_STACK_SIZE];
    uint16_t temp_pool_ptr;
    char *line_ptr;
    uint64_t line_size;
    /* Timestamp register set by settime()/strptime() and read by timestamp().
     * Undefined (timestamp_set == 0) on thread init, per mtail spec. */
    uint8_t timestamp_set;
    double timestamp_value;
    /* Filename of the log the current line came from; used by getfilename().
     * NULL if the caller does not provide one. Not owned by the thread. */
    const char *filename;
    /* Active for the current amtail_run/amtail_run_file; cleared before return. */
    amtail_touch_callbacks touch;
    /* Line-scoped regex captures (pointers into line_ptr; reset each log line). */
    uint8_t capture_count;
    amtail_capture_slice captures[AMTAIL_CAPTURE_MAX];
    uint8_t named_capture_count;
    amtail_named_capture_slot named_captures[AMTAIL_CAPTURE_MAX];
    /* Line-scoped split() / loop state (reset each log line). */
    char *split_storage;
    amtail_capture_slice split_parts[AMTAIL_SPLIT_MAX];
    uint8_t split_count;
    uint8_t split_index;
    char split_bind[AMTAIL_SPLIT_BIND_MAX];
    uint8_t split_bind_len;
    uint8_t split_active;
    /* Named split results: $responses = split(",", $field) */
    amtail_named_split_array split_arrays[AMTAIL_SPLIT_ARRAYS_MAX];
    uint8_t split_array_count;
    /* During range() / split-as loops: iteration source array (not owned). */
    amtail_named_split_array *split_loop_array;
} amtail_thread;

amtail_thread *amtail_thread_init(void);
void amtail_thread_free(amtail_thread *amt_thread);

void amtail_bytecode_dump(amtail_bytecode* byte_code);
int amtail_run(amtail_bytecode* byte_code, alligator_ht *variables, string* logline, amtail_log_level amtail_ll, const amtail_touch_callbacks *touch, struct amtail_thread *reuse_thread);
int amtail_run_file(amtail_bytecode* byte_code, alligator_ht *variables, string* logline, const char *filename, amtail_log_level amtail_ll, const amtail_touch_callbacks *touch, struct amtail_thread *reuse_thread);
void amtail_vm_init();
