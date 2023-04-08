#include "common/selector.h"
#include "generator.h"
#include "vm.h"
#include "variables.h"
#include "dstructures/tommy.h"
#include <string.h>

void (*amtail_vmfunc[256])(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline);

int amtail_variable_compare(const void* arg, const void* obj)
{
	char *s1 = (char*)arg;
	char *s2 = ((amtail_variable*)obj)->key;
	return strcmp(s1, s2);
}

amtail_byteop* amtail_vmstack_pop(amtail_thread *amt_thread)
{
    if (amt_thread->stack_ptr < 1)
        return NULL;

    printf("LOL %d: %p opcode %s {%"PRIu64" %lf}\n", amt_thread->stack_ptr-1, amt_thread->stack[amt_thread->stack_ptr-1], opname_from_code(amt_thread->stack[amt_thread->stack_ptr-1]->opcode), amt_thread->stack[amt_thread->stack_ptr-1]->li, amt_thread->stack[amt_thread->stack_ptr-1]->ld);
    return amt_thread->stack[--amt_thread->stack_ptr];
}

void amtail_vmstack_push(amtail_thread *amt_thread, amtail_byteop *byte_ops)
{
    if (amt_thread->stack_ptr + 1 >= AMTAIL_VM_STACK_SIZE)
    {
        fprintf(stderr, "amtail fatal: stack is oversized\n");
        return;
    }

    printf("LUL %d: %p opcode %s {{%"PRIu64" %lf}\n", amt_thread->stack_ptr, byte_ops, opname_from_code(byte_ops->opcode), byte_ops->li, byte_ops->ld);
    amt_thread->stack[amt_thread->stack_ptr++] = byte_ops;
}

void amtail_vmfunc_assign(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline)
{
    printf("ASSIGN! %p\n", byte_ops);
    amtail_vmstack_push(amt_thread, byte_ops);
}

void amtail_vmfunc_var_use(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline)
{
    printf("VAR!\n");
    amtail_vmstack_push(amt_thread, byte_ops);
}

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
void amtail_vmfunc_add(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline)
{
    printf("ADD !!!\n");

    amtail_byteop *left = amtail_vmstack_pop(amt_thread);
    amtail_byteop *right = amtail_vmstack_pop(amt_thread);
    amtail_byteop *new = calloc(1, sizeof(*new));
    amtail_vmstack_push(amt_thread, new);
    new->allocated = 1;

	if (left->vartype == ALLIGATOR_VARTYPE_COUNTER && right->vartype == ALLIGATOR_VARTYPE_COUNTER)
    {
        new->li = left->li + right->li;
        new->vartype = ALLIGATOR_VARTYPE_COUNTER;
        printf("counter counter adding: %"PRId64" and %"PRId64"\n", left->li, right->li);
    }
	else if (left->vartype == ALLIGATOR_VARTYPE_GAUGE && right->vartype == ALLIGATOR_VARTYPE_COUNTER)
    {
        new->ld = left->ld + right->li;
        new->vartype = ALLIGATOR_VARTYPE_GAUGE;
        printf("gauge counter adding: %lf and %"PRId64"\n", left->ld, right->li);
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_COUNTER && right->vartype == ALLIGATOR_VARTYPE_GAUGE)
    {
        new->ld = left->li + right->ld;
        new->vartype = ALLIGATOR_VARTYPE_GAUGE;
        printf("counter gauge adding: %"PRId64" and %lf\n", left->li, right->ld);
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_GAUGE && right->vartype == ALLIGATOR_VARTYPE_GAUGE)
    {
        new->ld = left->ld + right->ld;
        new->vartype = ALLIGATOR_VARTYPE_GAUGE;
        printf("counter gauge adding: %lf and %lf\n", left->ld, right->ld);
	}
}

void amtail_vmfunc_mul(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline)
{

    amtail_byteop *left = amtail_vmstack_pop(amt_thread);
    amtail_byteop *right = amtail_vmstack_pop(amt_thread);
    amtail_byteop *new = calloc(1, sizeof(*new));
    printf("MUL: left %p, right %p, new %p\n", left, right, new);
    amtail_vmstack_push(amt_thread, new);
    new->allocated = 1;

	if (left->vartype == ALLIGATOR_VARTYPE_COUNTER && right->vartype == ALLIGATOR_VARTYPE_COUNTER)
    {
        new->li = left->li * right->li;
        new->vartype = ALLIGATOR_VARTYPE_COUNTER;
        printf("counter counter mul: %"PRId64" and %"PRId64" = %"PRIu64"\n", left->li, right->li, new->li);
    }
	else if (left->vartype == ALLIGATOR_VARTYPE_GAUGE && right->vartype == ALLIGATOR_VARTYPE_COUNTER)
    {
        new->ld = left->ld * right->li;
        new->vartype = ALLIGATOR_VARTYPE_GAUGE;
        printf("gauge counter mul: %lf and %"PRId64" = %lf\n", left->ld, right->li, new->ld);
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_COUNTER && right->vartype == ALLIGATOR_VARTYPE_GAUGE)
    {
        new->ld = left->li * right->ld;
        new->vartype = ALLIGATOR_VARTYPE_GAUGE;
        printf("counter gauge mul: %"PRId64" and %lf =  %lf\n", left->li, right->ld, new->ld);
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_GAUGE && right->vartype == ALLIGATOR_VARTYPE_GAUGE)
    {
        new->ld = left->ld * right->ld;
        new->vartype = ALLIGATOR_VARTYPE_GAUGE;
        printf("counter gauge nul: %lf and %lf = %lf\n", left->ld, right->ld, new->ld);
	}
}

void amtail_vmfunc_pow(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline)
{

    amtail_byteop *left = amtail_vmstack_pop(amt_thread);
    amtail_byteop *right = amtail_vmstack_pop(amt_thread);
    amtail_byteop *new = calloc(1, sizeof(*new));
    printf("POW: left %p, right %p, new %p\n", left, right, new);
    amtail_vmstack_push(amt_thread, new);
    new->allocated = 1;

	if (left->vartype == ALLIGATOR_VARTYPE_COUNTER && right->vartype == ALLIGATOR_VARTYPE_COUNTER)
    {
        new->li = pow(left->li, right->li);
        new->vartype = ALLIGATOR_VARTYPE_COUNTER;
        printf("counter counter mul: %"PRId64" and %"PRId64" = %"PRIu64"\n", left->li, right->li, new->li);
    }
	else if (left->vartype == ALLIGATOR_VARTYPE_GAUGE && right->vartype == ALLIGATOR_VARTYPE_COUNTER)
    {
        new->ld = pow(left->ld, right->li);
        new->vartype = ALLIGATOR_VARTYPE_GAUGE;
        printf("gauge counter mul: %lf and %"PRId64" = %lf\n", left->ld, right->li, new->ld);
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_COUNTER && right->vartype == ALLIGATOR_VARTYPE_GAUGE)
    {
        new->ld = pow(left->li, right->ld);
        new->vartype = ALLIGATOR_VARTYPE_GAUGE;
        printf("counter gauge mul: %"PRId64" and %lf =  %lf\n", left->li, right->ld, new->ld);
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_GAUGE && right->vartype == ALLIGATOR_VARTYPE_GAUGE)
    {
        new->ld = pow(left->ld, right->ld);
        new->vartype = ALLIGATOR_VARTYPE_GAUGE;
        printf("counter gauge nul: %lf and %lf = %lf\n", left->ld, right->ld, new->ld);
	}
}

void amtail_vmfunc_div(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline)
{

    amtail_byteop *left = amtail_vmstack_pop(amt_thread);
    amtail_byteop *right = amtail_vmstack_pop(amt_thread);
    amtail_byteop *new = calloc(1, sizeof(*new));
    printf("DIV: left %p, right %p, new %p, opcode %u\n", left, right, new, right->opcode);
    amtail_vmstack_push(amt_thread, new);
    new->allocated = 1;

	if (left->vartype == ALLIGATOR_VARTYPE_COUNTER && right->vartype == ALLIGATOR_VARTYPE_COUNTER)
    {
        new->li = left->li / right->li;
        new->vartype = ALLIGATOR_VARTYPE_COUNTER;
        printf("counter counter mul: %"PRId64" and %"PRId64" = %"PRIu64"\n", left->li, right->li, new->li);
    }
	else if (left->vartype == ALLIGATOR_VARTYPE_GAUGE && right->vartype == ALLIGATOR_VARTYPE_COUNTER)
    {
        new->ld = left->ld / right->li;
        new->vartype = ALLIGATOR_VARTYPE_GAUGE;
        printf("gauge counter mul: %lf and %"PRId64" = %lf\n", left->ld, right->li, new->ld);
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_COUNTER && right->vartype == ALLIGATOR_VARTYPE_GAUGE)
    {
        new->ld = left->li / right->ld;
        new->vartype = ALLIGATOR_VARTYPE_GAUGE;
        printf("counter gauge mul: %"PRId64" and %lf =  %lf\n", left->li, right->ld, new->ld);
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_GAUGE && right->vartype == ALLIGATOR_VARTYPE_GAUGE)
    {
        new->ld = left->ld / right->ld;
        new->vartype = ALLIGATOR_VARTYPE_GAUGE;
        printf("counter gauge nul: %lf and %lf = %lf\n", left->ld, right->ld, new->ld);
	}
}

void amtail_vmfunc_runcalc(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline)
{
    amtail_byteop *left = amtail_vmstack_pop(amt_thread);
    amtail_byteop *right = amtail_vmstack_pop(amt_thread);
    amtail_byteop *new = calloc(1, sizeof(*new));
    printf("RUN: left %p, right %p, new %p\n", left, right, new);
    amtail_vmstack_push(amt_thread, new);
    new->allocated = 1;

    amtail_variable *var = alligator_ht_search(variables, amtail_variable_compare, right->export_name->s, amtail_hash(right->export_name->s, right->export_name->l));
    if (var)
    {
        if (var->type == ALLIGATOR_VARTYPE_COUNTER && left->vartype == ALLIGATOR_VARTYPE_COUNTER)
        {
            var->i = left->li;
        }
        else if (var->type == ALLIGATOR_VARTYPE_GAUGE && left->vartype == ALLIGATOR_VARTYPE_COUNTER)
        {
            var->d = left->li;
        }
        else if (var->type == ALLIGATOR_VARTYPE_COUNTER && left->vartype == ALLIGATOR_VARTYPE_GAUGE)
        {
            var->i = left->ld;
        }
        else if (var->type == ALLIGATOR_VARTYPE_GAUGE && left->vartype == ALLIGATOR_VARTYPE_GAUGE)
        {
            var->d = left->ld;
        }
    }


	//if (left->vartype == ALLIGATOR_VARTYPE_COUNTER && right->vartype == ALLIGATOR_VARTYPE_COUNTER)
    //{
    //    new->li = left->li / right->li;
    //    new->vartype = ALLIGATOR_VARTYPE_COUNTER;
    //    printf("counter counter mul: %"PRId64" and %"PRId64" = %"PRIu64"\n", left->li, right->li, new->li);
    //}
	//else if (left->vartype == ALLIGATOR_VARTYPE_GAUGE && right->vartype == ALLIGATOR_VARTYPE_COUNTER)
    //{
    //    new->ld = left->ld / right->li;
    //    new->vartype = ALLIGATOR_VARTYPE_GAUGE;
    //    printf("gauge counter mul: %lf and %"PRId64" = %lf\n", left->ld, right->li, new->ld);
	//}
	//else if (left->vartype == ALLIGATOR_VARTYPE_COUNTER && right->vartype == ALLIGATOR_VARTYPE_GAUGE)
    //{
    //    new->ld = left->li / right->ld;
    //    new->vartype = ALLIGATOR_VARTYPE_GAUGE;
    //    printf("counter gauge mul: %"PRId64" and %lf =  %lf\n", left->li, right->ld, new->ld);
	//}
	//else if (left->vartype == ALLIGATOR_VARTYPE_GAUGE && right->vartype == ALLIGATOR_VARTYPE_GAUGE)
    //{
    //    new->ld = left->ld / right->ld;
    //    new->vartype = ALLIGATOR_VARTYPE_GAUGE;
    //    printf("counter gauge nul: %lf and %lf = %lf\n", left->ld, right->ld, new->ld);
	//}
}
// TODO end

void amtail_vmfunc_inc(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline)
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

void amtail_vmfunc_dec(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline)
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

void amtail_vmfunc_variable(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline)
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
		fprintf(stderr, "error: variable called as '%s' already declared\n", byte_ops->export_name->s);
}

void amtail_vmfunc_noop(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline)
{
}

void amtail_vm_init()
{
	amtail_vmfunc[AMTAIL_AST_OPCODE_NOOP] = amtail_vmfunc_noop;
	amtail_vmfunc[AMTAIL_AST_OPCODE_VARIABLE] = amtail_vmfunc_variable;
	//amtail_vmfunc[AMTAIL_AST_OPCODE_BRANCH] = amtail_vmfunc_branch;
	amtail_vmfunc[AMTAIL_AST_OPCODE_INC] = amtail_vmfunc_inc;
	amtail_vmfunc[AMTAIL_AST_OPCODE_DEC] = amtail_vmfunc_dec;
	amtail_vmfunc[AMTAIL_AST_OPCODE_ADD] = amtail_vmfunc_add;
	amtail_vmfunc[AMTAIL_AST_OPCODE_MUL] = amtail_vmfunc_mul;
	amtail_vmfunc[AMTAIL_AST_OPCODE_POW] = amtail_vmfunc_pow;
	amtail_vmfunc[AMTAIL_AST_OPCODE_DIV] = amtail_vmfunc_div;
	amtail_vmfunc[AMTAIL_AST_OPCODE_ASSIGN] = amtail_vmfunc_assign;
	amtail_vmfunc[AMTAIL_AST_OPCODE_VAR] = amtail_vmfunc_var_use;
	amtail_vmfunc[AMTAIL_AST_OPCODE_RUN] = amtail_vmfunc_runcalc;
}

amtail_thread* amtail_thread_init()
{
    amtail_thread *amt_thread = calloc(1, sizeof(*amt_thread));
    return amt_thread;
}

void amtail_thread_free(amtail_thread *amt_thread)
{
    free(amt_thread);
}

int amtail_pre_execute(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline)
{
	if (byte_ops->opcode == AMTAIL_AST_OPCODE_BRANCH) // branch
		return 2;
	else if 
		(byte_ops->opcode == AMTAIL_AST_OPCODE_VARIABLE)
	
	{
		//printf("byte_ops %p\n", byte_ops);
		amtail_vmfunc[byte_ops->opcode](amt_thread, byte_ops, variables, logline);
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

int amtail_execute(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline)
{
	if (
		(byte_ops->opcode == AMTAIL_AST_OPCODE_BRANCH) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_VARIABLE)
	)
		return 2;
	else if (
		(byte_ops->opcode == AMTAIL_AST_OPCODE_INC) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_DEC) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_ADD) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_MUL) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_POW) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_DIV) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_ASSIGN) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_VAR) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_RUN) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_NOOP)
	)
	{
	    printf("\nbyte_ops %p code %d\n", byte_ops, byte_ops->opcode);
		amtail_vmfunc[byte_ops->opcode](amt_thread, byte_ops, variables, logline);
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
        printf("[%"PRIu64"] opcode %d (%s)\n", i, byte_code->ops[i].opcode, opname_from_code(byte_code->ops[i].opcode));
        printf("\t[%"PRIu64"] jmp %"PRIu64"\n", i, byte_code->ops[i].right_opcounter);
        printf("\t[%"PRIu64"] re_match %p\n", i, byte_code->ops[i].re_match);
        printf("\t[%"PRIu64"] hidden %d\n", i, byte_code->ops[i].hidden);
        if (byte_code->ops[i].export_name)
            printf("\t[%"PRIu64"] name '%s'\n", i, byte_code->ops[i].export_name->s);
        if (byte_code->ops[i].opcode == AMTAIL_AST_OPCODE_VAR && byte_code->ops[i].vartype == ALLIGATOR_VARTYPE_COUNTER)
        {
            printf("\t[%"PRIu64"] var counter left %"PRId64"\n", i, byte_code->ops[i].li);
            printf("\t[%"PRIu64"] var counter right %"PRId64"\n", i, byte_code->ops[i].ri);
        }
        else if (byte_code->ops[i].opcode == AMTAIL_AST_OPCODE_VAR && byte_code->ops[i].vartype == ALLIGATOR_VARTYPE_GAUGE)
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

    amtail_thread *amt_thread = amtail_thread_init();

	for (uint64_t i = 0; i < size; ++i)
	{
		amtail_pre_execute(amt_thread, &byte_ops[i], variables, logline);
	}

	for (uint64_t cursym_log = 0, counter = 0; cursym_log < logline->l; ++cursym_log, ++counter)
	{
        printf("logline counter %llu\n", counter);
		line_size = strcspn(logline->s + cursym_log, "\n");
        while (amtail_vmstack_pop(amt_thread));
		for (uint64_t i = 0; i < size; ++i)
		{
			rc = amtail_execute(amt_thread, &byte_ops[i], variables, logline);
			if (rc == 2) // branch
			{
				uint64_t new = amtail_branch_select(&byte_ops[i], variables, logline, cursym_log, line_size);
                if (new)
                    i = new;
			}
			else if (!rc)
			{
				//printf("error execute on logline: '%s', %d\n", logline->s, rc);
				return rc;
			}
		}

		cursym_log += line_size;
		cursym_log += strspn(logline->s + cursym_log, "\n");
	}

    amtail_thread_free(amt_thread);
	return 1;
}
