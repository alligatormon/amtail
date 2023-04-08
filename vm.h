#pragma once
#include "generator.h"
#define AMTAIL_VM_STACK_SIZE 1024

typedef struct amtail_thread {
    amtail_byteop* stack[AMTAIL_VM_STACK_SIZE];
    uint16_t stack_ptr;
} amtail_thread;

void amtail_bytecode_dump(amtail_bytecode* byte_code);
int amtail_run(amtail_bytecode* byte_code, string* logline);
void amtail_vm_init();
