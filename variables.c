#include "common/selector.h"
#include "variables.h"
#include "dstructures/tommy.h"
#include <string.h>
#include <stdint.h>

void amtail_variables_dump_foreach(void *funcarg, void* arg)
{
	amtail_variable *var = arg;
	if (var->is_template)
		return;

	fprintf(stderr, "dump %s/%s\n", var->export_name->s, var->key);

	string *dst = funcarg;
	string_string_cat(dst, var->export_name);
	string_cat(dst, " ", 1);

	if (var->by) {
		//char *ptrby = var->key;
		for (uint8_t i = 0; i < var->by_count; ++i)
		{
			if (i)
				string_cat(dst, ", ", 2);

            char *ptrby = var->key + var->by_positions[i];
            uint8_t key_len = var->by_positions[i+1] - var->by_positions[i] - 2;
            printf("by_position is %hhu/len %hhu (next %hhu)\n", var->by_positions[i], key_len, var->by_positions[i+1]);
			//ptrby = strstr(ptrby, "[");
			//if (!ptrby)
			//	break;

			//uint8_t key_len = strcspn(++ptrby, "]");

			string_string_cat(dst, var->by[i]);
			string_cat(dst, "=", 1);
			string_cat(dst, ptrby, key_len);
		}

		string_cat(dst, " ", 1);
	}

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

amtail_variable* amtail_variable_make(uint8_t hidden, uint8_t vartype, char *key, string *export_name, string **by, uint8_t by_count, uint8_t* by_positions)
{
	amtail_variable *var = calloc(1, sizeof(*var));
	var->hidden = hidden;
	var->type = vartype;
	var->key = key;
	var->export_name = export_name;
	var->by = by;
	var->by_count = by_count;
    var->by_positions = by_positions;

    return var;
}

inline uint32_t amtail_hash(char *str, uint64_t syms)
{
	return (uint32_t)(str[0] + str[syms]);
}
