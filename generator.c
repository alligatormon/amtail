#include "common/selector.h"
#include "parser.h"
#include "generator.h"
#include <string.h>
#include "amtail_pcre.h"

amtail_bytecode* amtail_code_init(uint64_t size)
{
	amtail_bytecode *byte_code = calloc(1, sizeof(*byte_code));
	byte_code->ops = calloc(1, sizeof(amtail_byteop) * size);
	byte_code->variables = alligator_ht_init(NULL);

	byte_code->m = size;
	byte_code->l = 0;

	return byte_code;
}

void amtail_byteops_free(amtail_byteop *arg)
{
	free(arg);
}

void amtail_code_push(amtail_bytecode *byte_code, amtail_ast *ast, amtail_log_level amtail_ll)
{
	if (byte_code->l >= byte_code->m)
	{
		byte_code->m *= 2;
		amtail_byteop *new = calloc(1, sizeof(amtail_byteop) * byte_code->m);
		memcpy(new, byte_code->ops, byte_code->l * sizeof(amtail_byteop));
		amtail_byteops_free(byte_code->ops);
		byte_code->ops = new;
	}

	amtail_byteop *fill = &byte_code->ops[byte_code->l];
	fill->opcode = ast->opcode;
	fill->vartype = ast->vartype;
	fill->hidden = ast->hidden;

	if (ast->vartype == ALLIGATOR_VARTYPE_CONST)
	{
		fill->ls = string_new();
		string_string_copy(fill->ls, ast->svalue);
	}

	if (ast->export_name)
	{
		fill->export_name = string_new();
		string_string_copy(fill->export_name, ast->export_name);
        if (amtail_ll.generator > 0)
		    printf("> byteop %p name %s\n", fill, fill->export_name->s);
	}

	if (ast->name && !ast->export_name)
	{
		fill->export_name = string_new();
		string_string_copy(fill->export_name, ast->name);
        if (amtail_ll.generator > 0)
		    printf("> byteop %p name %s\n", fill, fill->export_name->s);
	}

    if (fill->opcode == AMTAIL_AST_OPCODE_BRANCH)
    {
        --fill->export_name->l;
        fill->export_name->s[fill->export_name->l] = 0;
        fill->re_match = amtail_regex_compile(fill->export_name->s + 1);
        if (amtail_ll.generator > 0)
            printf("try to compile regexp: %p: '%s'\n", fill->re_match, fill->export_name->s + 1);
    }

	++byte_code->l;
}

void amtail_bytecode_walk(amtail_bytecode *byte_code, amtail_ast *ast, amtail_log_level amtail_ll)
{
	if (!ast)
		return;

    uint64_t index = byte_code->l;
	amtail_code_push(byte_code, ast, amtail_ll);

	if (ast->stem)
	{
		if (ast->stem[AMTAIL_AST_LEFT])
			amtail_bytecode_walk(byte_code, ast->stem[AMTAIL_AST_LEFT], amtail_ll);


		if (ast->stem[AMTAIL_AST_RIGHT])
        {
            //byte_code->ops[cur_ptr].jmp = byte_code->l + 1;
            byte_code->ops[index].right_opcounter = byte_code->l;
            //printf("DEBUG2[%"PRIu64"]: %"PRIu64"\n", index, byte_code->ops[index].right_opcounter);
			amtail_bytecode_walk(byte_code, ast->stem[AMTAIL_AST_RIGHT], amtail_ll);
        }

	}
}

amtail_bytecode* amtail_code_generator(amtail_ast *ast, amtail_log_level amtail_ll)
{
	if (!ast)
		return NULL;

	amtail_bytecode *byte_code = amtail_code_init(1);

	amtail_bytecode_walk(byte_code, ast, amtail_ll);

	return byte_code;
}

void amtail_code_free(amtail_bytecode *byte_code)
{
	free(byte_code);
}
