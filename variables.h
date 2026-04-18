#pragma once
#include "parser.h"

typedef struct amtail_variable {
	string *export_name;
	char *key;
	uint8_t type;
	uint8_t facttype;
	uint8_t hidden;
	union {
		double d;
		int64_t i;
		string *s;
	};

	uint8_t by_count;
	string **by;
	uint8_t *by_positions;
	uint8_t bucket_count;
	string **bucket;
	double *bucket_bounds;
	uint64_t *bucket_hits;
	double histogram_sum;
	uint64_t histogram_count;
	uint8_t is_template;

	tommy_node node;
} amtail_variable;


void amtail_variables_dump(alligator_ht *variables);
uint32_t amtail_hash(char *str, uint64_t syms);
amtail_variable* amtail_variable_make(uint8_t hidden, uint8_t vartype, char *key, string *export_name, string **by, uint8_t by_count, uint8_t* by_positions);
int variable_parse_set_value(amtail_variable *var, string *s);
int amtail_variable_compare(const void* arg, const void* obj);
alligator_ht* amtail_variables_init();
