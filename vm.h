#pragma once
#include "generator.h"
#include "variables.h"
#define AMTAIL_VM_STACK_SIZE 1024

typedef struct amtail_thread {
    amtail_byteop* stack[AMTAIL_VM_STACK_SIZE];
    uint16_t stack_ptr;
    char *line_ptr;
    uint64_t line_size;
} amtail_thread;

void amtail_bytecode_dump(amtail_bytecode* byte_code);
int amtail_run(amtail_bytecode* byte_code, alligator_ht *variables, string* logline, amtail_log_level amtail_ll);
void amtail_vm_init();
