#pragma once
#include "parser.h"
#include "common/pcre_parser.h"

typedef struct amtail_byteop {
	uint8_t opcode;
	string *export_name;
	uint8_t vartype;
	uint8_t hidden;
	union {
		double ld;
		int64_t li;
		string *ls;
	};
	union {
		double rd;
		int64_t ri;
		string *rs;
	};

    //uint64_t jmp;
    regex_match *re_match;
	uint64_t right_opcounter;
} amtail_byteop;

typedef struct amtail_bytecode {
	amtail_byteop *ops;
	uint64_t m;
	uint64_t l;
	alligator_ht *variables;
} amtail_bytecode;

amtail_bytecode* amtail_code_generator(amtail_ast *ast, amtail_log_level amtail_ll);
void amtail_code_free(amtail_bytecode *byte_code);
