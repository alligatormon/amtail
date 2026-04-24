#pragma once
#include "generator.h"
#include "variables.h"
#define AMTAIL_VM_STACK_SIZE 1024

typedef struct amtail_thread {
    amtail_byteop* stack[AMTAIL_VM_STACK_SIZE];
    uint16_t stack_ptr;
    char *line_ptr;
    uint64_t line_size;
    /* Timestamp register set by settime()/strptime() and read by timestamp().
     * Undefined (timestamp_set == 0) on thread init, per mtail spec. */
    uint8_t timestamp_set;
    double timestamp_value;
    /* Filename of the log the current line came from; used by getfilename().
     * NULL if the caller does not provide one. Not owned by the thread. */
    const char *filename;
} amtail_thread;

void amtail_bytecode_dump(amtail_bytecode* byte_code);
int amtail_run(amtail_bytecode* byte_code, alligator_ht *variables, string* logline, amtail_log_level amtail_ll);
int amtail_run_file(amtail_bytecode* byte_code, alligator_ht *variables, string* logline, const char *filename, amtail_log_level amtail_ll);
void amtail_vm_init();
