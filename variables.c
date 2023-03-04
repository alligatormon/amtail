#include "common/selector.h"
#include "variables.h"
#include "dstructures/tommy.h"
#include <string.h>
#include <stdint.h>

void amtail_variables_dump_foreach(void *funcarg, void* arg)
{
	amtail_variable *var = arg;
	fprintf(stderr, "dump %s/%s\n", var->export_name->s, var->key);

	string *dst = funcarg;
	string_string_cat(dst, var->export_name);
	string_cat(dst, " ", 1);

	if (var)
	{
		if (var->type == ALLIGATOR_VARTYPE_COUNTER)
			string_uint(dst, var->i);
		else if (var->type == ALLIGATOR_VARTYPE_GAUGE)
			string_double(dst, var->d);
	}
	string_cat(dst, "\n", 1);
}

void amtail_variables_dump(alligator_ht *variables)
{
	string *dst = string_new();
	alligator_ht_foreach_arg(variables, amtail_variables_dump_foreach, dst);
    printf("count of variables: %zu\n", alligator_ht_count(variables));

	fprintf(stderr, "dst is\n%s\n", dst->s);
}

inline uint32_t amtail_hash(char *str, uint64_t syms)
{
	return (uint32_t)(str[0] + str[syms]);
}
