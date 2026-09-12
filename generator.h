#pragma once
#include "parser.h"
#include "common/pcre_parser.h"
#include "common/selector.h"

typedef struct amtail_byteop {
	uint8_t opcode;
	string *export_name;
	uint8_t vartype;
	uint8_t facttype;
	uint8_t hidden;
	/* Set at compile time: 1 if export_name may contain `[$` and needs runtime interpolation. */
	uint8_t metric_key_interpolate;
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

	uint8_t by_count;
	string **by;

	uint8_t bucket_count;
	string **bucket;

	/* RANGE: parallel split arrays + bind names (arity 1 == classic range). */
	string *loop_arrays[AMTAIL_ZIP_MAX];
	string *loop_binds[AMTAIL_ZIP_MAX];
	uint8_t loop_arity;

    //uint64_t jmp;
    regex_match *re_match;
	uint64_t right_opcounter;
	/* For RANGE: pc of trailing RANGE_STEP (skip target when loop does not activate). */
	uint64_t step_opcounter;
    uint8_t allocated; // only if not a part of sequence of command
} amtail_byteop;

typedef struct amtail_bytecode {
	amtail_byteop *ops;
	uint64_t m;
	uint64_t l;
	uint8_t prepared;
} amtail_bytecode;

amtail_bytecode* amtail_code_generator(amtail_ast *ast, amtail_log_level amtail_ll);
void amtail_code_free(amtail_bytecode *byte_code);
