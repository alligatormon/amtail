#include "common/selector.h"
#include "generator.h"
#include "vm.h"
#include "variables.h"
#include "dstructures/tommy.h"
#include <string.h>

void (*amtail_vmfunc[256])(amtail_byteop *byte_ops, alligator_ht *variables, string *logline);

int amtail_variable_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((amtail_variable*)obj)->key;
	return strcmp(s1, s2);
}

//void amtail_vmfunc_variable(amtail_byteop *byte_ops, alligator_ht *variables, string *logline)
//{
//	if (byte_ops->vartype == ALLIGATOR_VARTYPE_COUNTER)
//	{
//		printf("create counter %s\n", byte_ops->export_name->s);
//		byte_ops->li = 0;
//	}
//	else if (byte_ops->vartype == ALLIGATOR_VARTYPE_GAUGE)
//	{
//		printf("create gauge %s\n", byte_ops->export_name->s);
//		byte_ops->ld = 0;
//	}
//	else if (byte_ops->vartype == ALLIGATOR_VARTYPE_TEXT)
//	{
//		printf("create text %s\n", byte_ops->export_name->s);
//		byte_ops->ls = string_new();
//	}
//	else if (byte_ops->vartype == ALLIGATOR_VARTYPE_CONST)
//	{
//		printf("create const %s: %s\n", byte_ops->export_name->s, byte_ops->ls->s);
//	}
//	else if (byte_ops->vartype == ALLIGATOR_VARTYPE_HISTOGRAM)
//	{
//		printf("create histogram %s\n", byte_ops->export_name->s);
//	}
//}

uint64_t amtail_vmfunc_branch(amtail_byteop *byte_ops, alligator_ht *variables, string *logline, uint64_t offset, uint64_t line_size)
{
    uint8_t match = amtail_regex_exec(byte_ops->re_match, logline->s+offset, line_size);
	//fprintf(stderr, "branch pcre '%s' (jmp %"PRIu64", res: %"PRIu8" with logline '%p'\n", byte_ops->export_name->s+1, byte_ops->right_opcounter, match, logline->s);
    if (match)
	    return 0;
    else
        return byte_ops->right_opcounter;
	//amtail_execute(byte_ops, variables, logline);
}

// TODO
void amtail_vmfunc_plus(amtail_byteop *byte_ops, alligator_ht *variables, string *logline)
{
    printf("byte_ops->export_name->s is %s, vartype is %d/%d/%d\n", byte_ops->export_name ? byte_ops->export_name->s : NULL, byte_ops->vartype, ALLIGATOR_VARTYPE_COUNTER, ALLIGATOR_VARTYPE_GAUGE);
	//amtail_variable *var = alligator_ht_search(variables, amtail_variable_compare, byte_ops->export_name->s, amtail_hash(byte_ops->export_name->s, byte_ops->export_name->l));
	//if (var)
	//{
		if (byte_ops->vartype == ALLIGATOR_VARTYPE_COUNTER)
            printf("\tvar->i is %lld and %lld\n", byte_ops->li, byte_ops->ri);
			//++var->i;
		else if (byte_ops->vartype == ALLIGATOR_VARTYPE_GAUGE)
            printf("var->d is %lf and %lf", byte_ops->ld, byte_ops->rd);
			//++var->d;
	//}
}
// TODO end

void amtail_vmfunc_inc(amtail_byteop *byte_ops, alligator_ht *variables, string *logline)
{
   // printf("export name is %s\n", byte_ops->export_name->s);
	amtail_variable *var = alligator_ht_search(variables, amtail_variable_compare, byte_ops->export_name->s, amtail_hash(byte_ops->export_name->s, byte_ops->export_name->l));
	if (var)
	{
		if (var->type == ALLIGATOR_VARTYPE_COUNTER)
			++var->i;
		else if (var->type == ALLIGATOR_VARTYPE_GAUGE)
			++var->d;
	}
}

void amtail_vmfunc_dec(amtail_byteop *byte_ops, alligator_ht *variables, string *logline)
{
	amtail_variable *var = alligator_ht_search(variables, amtail_variable_compare, byte_ops->export_name->s, amtail_hash(byte_ops->export_name->s, byte_ops->export_name->l));
	if (var)
	{
		if (var->type == ALLIGATOR_VARTYPE_COUNTER)
			--var->i;
		else if (var->type == ALLIGATOR_VARTYPE_GAUGE)
			--var->d;
	}
}

void amtail_vmfunc_variable(amtail_byteop *byte_ops, alligator_ht *variables, string *logline)
{
	//printf("create variable %s with type %hhu\n", byte_ops->export_name->s, byte_ops->vartype);
	uint32_t name_hash = amtail_hash(byte_ops->export_name->s, byte_ops->export_name->l);
	amtail_variable *var = alligator_ht_search(variables, amtail_variable_compare, byte_ops->export_name->s, name_hash);
	if (!var)
	{
		var = calloc(1, sizeof(*var));
		var->hidden = byte_ops->hidden;
		var->type = byte_ops->vartype;
		var->key = strdup(byte_ops->export_name->s);
		var->export_name = string_new();
		string_string_cat(var->export_name, byte_ops->export_name);
		alligator_ht_insert(variables, &(var->node), var, name_hash);
	}
	else
		fprintf(stderr, "Error: variable called as '%s' already declared\n", byte_ops->export_name->s);
}

void amtail_vmfunc_noop(amtail_byteop *byte_ops, alligator_ht *variables, string *logline)
{
}

void amtail_vm_init()
{
	amtail_vmfunc[AMTAIL_AST_OPCODE_NOOP] = amtail_vmfunc_noop;
	amtail_vmfunc[AMTAIL_AST_OPCODE_VARIABLE] = amtail_vmfunc_variable;
	//amtail_vmfunc[AMTAIL_AST_OPCODE_BRANCH] = amtail_vmfunc_branch;
	amtail_vmfunc[AMTAIL_AST_OPCODE_INC] = amtail_vmfunc_inc;
	amtail_vmfunc[AMTAIL_AST_OPCODE_DEC] = amtail_vmfunc_dec;
	//amtail_vmfunc[AMTAIL_AST_OPCODE_VARIABLE] = amtail_vmfunc_variable;

	amtail_vmfunc[AMTAIL_AST_OPCODE_PLUS] = amtail_vmfunc_plus;
}

int amtail_pre_execute(amtail_byteop *byte_ops, alligator_ht *variables, string *logline)
{
	if (byte_ops->opcode == AMTAIL_AST_OPCODE_BRANCH) // branch
		return 2;
	else if 
		(byte_ops->opcode == AMTAIL_AST_OPCODE_VARIABLE)
	
	{
		//printf("byte_ops %p\n", byte_ops);
		amtail_vmfunc[byte_ops->opcode](byte_ops, variables, logline);
	}
	else
	{
		return 0;
	}
	return 1;
}

uint64_t amtail_branch_select(amtail_byteop *byte_ops, alligator_ht *variables, string *logline, uint64_t offset, uint64_t line_size)
{
	//if 
	//	(byte_ops->opcode == AMTAIL_AST_OPCODE_BRANCH)
	
		return amtail_vmfunc_branch(byte_ops, variables, logline, offset, line_size);

    //return
}

int amtail_execute(amtail_byteop *byte_ops, alligator_ht *variables, string *logline)
{
	if (
		(byte_ops->opcode == AMTAIL_AST_OPCODE_BRANCH) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_VARIABLE)
	)
		return 2;
	else if (
		(byte_ops->opcode == AMTAIL_AST_OPCODE_INC) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_DEC) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_PLUS) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_NOOP)
	)
	{
		//printf("byte_ops %p\n", byte_ops);
		amtail_vmfunc[byte_ops->opcode](byte_ops, variables, logline);
	}
	else
	{
		printf("NOT defined OP: %d\n", byte_ops->opcode);
		return 0;
	}
	return 1;
}

void amtail_bytecode_dump(amtail_bytecode* byte_code)
{
	uint64_t size = byte_code->l;

	for (uint64_t i = 0; i < size; ++i)
	{
        printf("[%"PRIu64"] opcode %d\n", i, byte_code->ops[i].opcode);
        printf("\t[%"PRIu64"] jmp %"PRIu64"\n", i, byte_code->ops[i].right_opcounter);
        printf("\t[%"PRIu64"] re_match %p\n", i, byte_code->ops[i].re_match);
        printf("\t[%"PRIu64"] hidden %d\n", i, byte_code->ops[i].hidden);
        if (byte_code->ops[i].vartype == ALLIGATOR_VARTYPE_COUNTER)
        {
            printf("\t[%"PRIu64"] var counter left %"PRId64"\n", i, byte_code->ops[i].li);
            printf("\t[%"PRIu64"] var counter right %"PRId64"\n", i, byte_code->ops[i].ri);
        }
        else if (byte_code->ops[i].vartype == ALLIGATOR_VARTYPE_GAUGE)
        {
            printf("\t[%"PRIu64"] var gauge left %lf\n", i, byte_code->ops[i].ld);
            printf("\t[%"PRIu64"] var gauge right %lf\n", i, byte_code->ops[i].rd);
        }
	}
}

int amtail_run(amtail_bytecode* byte_code, string* logline)
{
	uint64_t size = byte_code->l;
	amtail_byteop *byte_ops = byte_code->ops;
	alligator_ht *variables = byte_code->variables;
	int rc;
	uint64_t line_size = 0;

	for (uint64_t i = 0; i < size; ++i)
	{
		amtail_pre_execute(&byte_ops[i], variables, logline);
	}

	for (uint64_t cursym_log = 0; cursym_log < logline->l; ++cursym_log)
	{
		line_size = strcspn(logline->s + cursym_log, "\n");
		for (uint64_t i = 0; i < size; ++i)
		{
			rc = amtail_execute(&byte_ops[i], variables, logline);
			if (rc == 2) // branch
			{
				uint64_t new = amtail_branch_select(&byte_ops[i], variables, logline, cursym_log, line_size);
                if (new)
                    i = new;
			}
			else if (!rc)
			{
				printf("error execute on logline: '%s', %d\n", logline->s, rc);
				return rc;
			}
		}

		cursym_log += line_size;
		cursym_log += strspn(logline->s + cursym_log, "\n");
	}
	return 1;
}
