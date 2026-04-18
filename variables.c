#include "common/selector.h"
#include "variables.h"
#include "dstructures/tommy.h"
#include <errno.h>
#include <string.h>
#include <stdint.h>

void amtail_variables_dump_foreach(void *funcarg, void* arg)
{
	amtail_variable *var = arg;
    printf("variable %d: %s\n", var->is_template, var->export_name->s);
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
		else if (var->type == ALLIGATOR_VARTYPE_CONST) {
			if (var->facttype == ALLIGATOR_FACTTYPE_TEXT)
				string_string_cat(dst, var->s);
			else if (var->facttype == ALLIGATOR_FACTTYPE_DOUBLE)
				string_double(dst, var->d);
			else if (var->facttype == ALLIGATOR_FACTTYPE_DOUBLE)
				string_int(dst, var->i);
		}
		else if (var->type == ALLIGATOR_VARTYPE_HISTOGRAM)
			string_double(dst, var->d);
		else if (var->type == ALLIGATOR_VARTYPE_TEXT)
			string_string_cat(dst, var->s);
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


int amtail_variable_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((amtail_variable*)obj)->key;
	return strcmp(s1, s2);
}

int variable_parse_set_value(amtail_variable *var, string *s)
{
    char *end;
    errno = 0;

    int64_t iv = strtoll(s->s, &end, 10);
    if (errno == 0 && *end == '\0') {
        var->facttype = ALLIGATOR_FACTTYPE_INT;
        var->i = iv;
        return 1;
    }

    errno = 0;
    double dv = strtod(s->s, &end);
    if (errno == 0 && *end == '\0') {
        var->facttype = ALLIGATOR_FACTTYPE_DOUBLE;
        var->d = dv;
        return 1;
    }

    var->facttype = ALLIGATOR_FACTTYPE_TEXT;
    var->s = string_string_init_dup(s);
    return 1;
}

alligator_ht* amtail_variables_init()
{
	alligator_ht *variables = alligator_ht_init(NULL);
	return variables;
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
	if (!str || !syms)
		return 0;
	return (uint32_t)(((uint8_t)str[0] * 33u) + (uint8_t)str[syms - 1]);
}

void amtail_variable_free(void *funcarg, void* arg)
{
	amtail_variable *var = arg;
	if (!var)
		return;
	if (var->export_name)
		string_free(var->export_name);
	if (var->key)
		free(var->key);
	if ((var->type == ALLIGATOR_VARTYPE_TEXT || var->type == ALLIGATOR_VARTYPE_CONST) && var->s)
		string_free(var->s);
	if (var->by_positions)
		free(var->by_positions);
	free(var);
}

void amtail_variables_free(alligator_ht *variables)
{
	alligator_ht_foreach_arg(variables, amtail_variable_free, NULL);
	alligator_ht_done(variables);
	free(variables);
}