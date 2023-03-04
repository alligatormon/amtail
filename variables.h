#pragma once
#include "parser.h"

typedef struct amtail_variable {
	string *export_name;
	char *key;
	uint8_t type;
	uint8_t hidden;
	union {
		double d;
		int64_t i;
		string *s;
	};

	tommy_node node;
} amtail_variable;


void amtail_variables_dump(alligator_ht *variables);
uint32_t amtail_hash(char *str, uint64_t syms);
