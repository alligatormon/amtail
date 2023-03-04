#pragma once
#include "generator.h"

void amtail_bytecode_dump(amtail_bytecode* byte_code);
int amtail_run(amtail_bytecode* byte_code, string* logline);
void amtail_vm_init();
