#include "common/selector.h"
#include <string.h>
#include "sstring.h"
#include "parser.h"
#define AMTAIL_INDENT_MAX_SIZE 4096

amtail_ast* amtail_ast_init()
{
	amtail_ast *ast = calloc(1, sizeof(*ast));
	//printf("create obj %p\n", ast);
	return ast;
}

amtail_ast** amtail_ast_multi_init(uint64_t count)
{
	amtail_ast **ast = calloc(1, sizeof(void*) * count);
	for (uint64_t i = 0; i < count; i++)
	{
		//printf("init %p with number %d\n", ast, i);
		ast[i] = amtail_ast_init();
	}
	return ast;
}

void amtail_ast_free(amtail_ast *ast)
{
	if (ast->name)
		string_free(ast->name);

	if (ast->export_name)
		string_free(ast->export_name);

	if (*ast->by)
	{
		for (uint64_t i = 0; i < ast->by_count; ++i)
		{
			string_free(ast->by[i]);
		}

		//free(ast->by);
	}

	free(ast);
}

void amtail_ast_print(amtail_ast *ast, uint64_t indent)
{
	if (!ast)
		return;

	char indent_str[AMTAIL_INDENT_MAX_SIZE];
	size_t copy_size = indent + 1 > AMTAIL_INDENT_MAX_SIZE ? AMTAIL_INDENT_MAX_SIZE - 1 : indent;
	memset(indent_str, '\t', copy_size);
	indent_str[++copy_size] = 0;

	printf("\n%s>>>> BLOCK %p <<<<\n", indent_str, ast);
	if (ast->opcode == AMTAIL_AST_OPCODE_VARIABLE)
	{
		if (ast->vartype == ALLIGATOR_VARTYPE_COUNTER)
			printf("%s>> (hidden:%d) variable counter: %"PRIu64"\n", indent_str, ast->hidden, ast->ivalue);
		else if (ast->vartype == ALLIGATOR_VARTYPE_GAUGE)
			printf("%s>> (hidden:%d) variable gauge: %lf\n", indent_str, ast->hidden, ast->dvalue);
		else if (ast->vartype == ALLIGATOR_VARTYPE_TEXT)
			printf("%s>> (hidden:%d) variable text: %s\n", indent_str, ast->hidden, ast->svalue->s);
		else if (ast->vartype == ALLIGATOR_VARTYPE_CONST)
			printf("%s>> (hidden:%d) variable const: %s\n", indent_str, ast->hidden, ast->svalue->s);
		else if (ast->vartype == ALLIGATOR_VARTYPE_HISTOGRAM)
			printf("%s>> (hidden:%d) variable histogram\n", indent_str, ast->hidden);
	}
	else if (ast->opcode == AMTAIL_AST_OPCODE_BRANCH)
		printf("%s>> branch\n", indent_str);
	else if (ast->opcode == AMTAIL_AST_OPCODE_INC)
		printf("%s>> increment\n", indent_str);
	else if (ast->opcode == AMTAIL_AST_OPCODE_DEC)
		printf("%s>> decrement\n", indent_str);
	else if (ast->opcode == AMTAIL_AST_OPCODE_FUNC_DECLARATION)
		printf("%s>> decorator\n", indent_str);
	else if (ast->opcode == AMTAIL_AST_OPCODE_FUNC_CALL)
		printf("%s>> call\n", indent_str);

	if (ast->name)
		printf("%sname %s\n", indent_str, ast->name->s);

	if (ast->export_name)
		printf("%sexport name %s\n", indent_str, ast->export_name->s);

	if (*ast->by && ast->by_count)
	{
		for (uint64_t i = 0; i < ast->by_count; ++i)
			printf("%sby[%"PRIu64"] %s\n", indent_str, i, ast->by[i]->s);
	}

	if (ast->stem)
	{
		if (ast->stem[AMTAIL_AST_LEFT])
			amtail_ast_print(ast->stem[AMTAIL_AST_LEFT], indent);
		if (ast->stem[AMTAIL_AST_RIGHT])
			amtail_ast_print(ast->stem[AMTAIL_AST_RIGHT], ++indent);
	}
}

void amtail_ast_stack_push(amtail_ast *stack, amtail_ast *push)
{
	if (!stack)
		return;

	if (!push)
		return;

	if (!stack->tail)
	{
		//printf("amtail_ast_stack_push PUSH %p: (%p)\n", push, stack->tail);
		stack->tail = push;
	}
	else
	{
		amtail_ast *tail = stack->tail;
		//tail->stem = amtail_ast_multi_init(1);
		//tail->stem[0] = push;
		stack->tail = push;
		//printf("amtail_ast_stack_push PUSH %p: (%p)\n", push, stack->tail);
		stack->tail->prev = tail;
	}
}

amtail_ast* amtail_ast_stack_pop(amtail_ast *stack)
{
	if (!stack)
		return NULL;

	if (!stack->tail)
		return NULL;

	amtail_ast *ret_ast = stack->tail;
	//printf("amtail_ast_stack_pop POP %p\n", ret_ast);
	amtail_ast *new_tail = ret_ast->prev;
	if (new_tail)
		new_tail->tail = NULL;
	stack->tail = new_tail;

	ret_ast->prev = NULL;

	return ret_ast;
}

char *hiden_visible(uint8_t hidden)
{
	return hidden ? "hidden" : "visible";
}

char *vartype_get(uint8_t vartype)
{
	return vartype ? "counter" : "gauge";
}

int dec_identy(uint64_t *identy)
{
	if (!*identy)
		return 0; // PANIC!

	--(*identy);
	return 1;
}

void amtail_parser_print_stack(string_tokens* tokens, uint64_t i)
{
	uint64_t k = 100;

	uint64_t j = k > i ? 0 : i - k + 1;

	while (++j && --k && j < tokens->l)
	{
		if (tokens->str[j]->s[0] == '\n')
			continue;

		if (j == i)
		{
			printf("PROBLEM: '%s'\n", tokens->str[j]->s);
			k = 15;
			continue;
		}

		printf("TK: '%s'\n", tokens->str[j]->s);
	}
}

amtail_ast* amtail_parser(string_tokens *tokens, char *name, amtail_log_level amtail_ll)
{
	//amtail_ast *funcs = amtail_ast_init();
	amtail_ast *ast = amtail_ast_init();
	amtail_ast *stack = amtail_ast_init();
	amtail_ast *cur = ast;
	uint64_t identy = 0;
	uint8_t begin = 1;
	uint8_t otherwise = 0;
	uint8_t e_else = 0;
	uint8_t func_declaration = 0;
	uint8_t func_call = 0;

	uint8_t expression = 0;
	string *expr = string_new();
	uint8_t hidden = 0;
	uint8_t vartype = 0;

	if (amtail_ll.parser)
		puts("\t<====> PARSER STAGE <====>");

	for (uint64_t i = 0; i < tokens->l; i++)
	{
		//for (uint64_t ii = 0; ii < identy; ii++, printf("\t"));
		//printf("i=%"PRIi64", identy %"PRIu64"\n", i, identy);
		//for (uint64_t ii = 0; ii < identy; ii++, printf("\t"));
		//printf("%lu: '%s'\n", i, tokens->str[i]->s);
		if (!strcmp(tokens->str[i]->s, "counter"))
		{
			++i;
			vartype = ALLIGATOR_VARTYPE_COUNTER;
			if (amtail_ll.parser > 0)
				printf("[%s] %s name [%"PRIu64"]: '%s'\n", hiden_visible(hidden), vartype_get(vartype), i, tokens->str[i]->s);

			cur->name = string_init_alloc(tokens->str[i]->s, tokens->str[i]->l);
			cur->opcode = AMTAIL_AST_OPCODE_VARIABLE;
			cur->vartype = ALLIGATOR_VARTYPE_COUNTER;
			cur->hidden = hidden;

			hidden = 0;
		}
		else if (!strcmp(tokens->str[i]->s, "gauge"))
		{
			++i;
			vartype = ALLIGATOR_VARTYPE_GAUGE;
			if (amtail_ll.parser > 0)
				printf("[%s] %s name [%"PRIu64"]: '%s'\n", hiden_visible(hidden), vartype_get(vartype), i, tokens->str[i]->s);

			cur->name = string_init_alloc(tokens->str[i]->s, tokens->str[i]->l);
			cur->opcode = AMTAIL_AST_OPCODE_VARIABLE;
			cur->vartype = ALLIGATOR_VARTYPE_GAUGE;
			cur->hidden = hidden;

			hidden = 0;
		}
		else if (!strcmp(tokens->str[i]->s, "hidden"))
		{
			if (amtail_ll.parser > 0)
				printf("name '%s', [%"PRIu64"] add parameter 'hidden'\n", cur->name->s, i);

			hidden = 1;
		}
		else if (!strcmp(tokens->str[i]->s, "as"))
		{
			++i;
			if (amtail_ll.parser > 0)
				printf("%s export name [%"PRIu64"]: '%s'\n", vartype_get(vartype), i, tokens->str[i]->s);

			cur->export_name = string_init_alloc(tokens->str[i]->s, tokens->str[i]->l);
		}
		else if (!strcmp(tokens->str[i]->s, "by"))
		{
			uint64_t j = 0;
			for (++i; strcmp(tokens->str[i]->s, "\n"); ++i, ++j)
			{
				if (amtail_ll.parser > 0)
					printf("by [%"PRIu64"]: '%s'\n", i, tokens->str[i]->s);
				cur->by[j] = string_init_alloc(tokens->str[i]->s, tokens->str[i]->l);
			}
			cur->by_count = j;

			--i;
		}
		else if (!strcmp(tokens->str[i]->s, "const"))
		{
			++i;
			vartype = ALLIGATOR_VARTYPE_TEXT;

			if (amtail_ll.parser > 0)
				printf("const data [%"PRIu64"]: '%s'\n", i, tokens->str[i]->s);

			cur->name = string_init_alloc(tokens->str[i]->s, tokens->str[i]->l);
			cur->opcode = AMTAIL_AST_OPCODE_VARIABLE;
			cur->vartype = ALLIGATOR_VARTYPE_CONST;
			cur->hidden = hidden;

			++i;
			cur->svalue = string_new();
			string_string_copy(cur->svalue, tokens->str[i]);

			hidden = 0;
		}
		else if (!strcmp(tokens->str[i]->s, "text"))
		{
			++i;
			vartype = ALLIGATOR_VARTYPE_TEXT;

			if (amtail_ll.parser > 0)
				printf("text name [%"PRIu64"]: '%s'\n", i, tokens->str[i]->s);

			cur->name = string_init_alloc(tokens->str[i]->s, tokens->str[i]->l);
			cur->opcode = AMTAIL_AST_OPCODE_VARIABLE;
			cur->vartype = ALLIGATOR_VARTYPE_TEXT;
			cur->hidden = hidden;
			cur->svalue = string_new();

			hidden = 0;
		}
		else if (!strcmp(tokens->str[i]->s, "def"))
		{
			++i;

			if (amtail_ll.parser > 0)
				printf("function declaration [%"PRIu64"]: '%s'\n", i, tokens->str[i]->s);

			func_declaration = 1;
			cur->name = string_init_alloc(tokens->str[i]->s, tokens->str[i]->l);
			//string_string_cat(expr, tokens->str[i]);
			cur->opcode = AMTAIL_AST_OPCODE_FUNC_DECLARATION;
			expression = 0;
		}
		else if (tokens->str[i]->s[0] != '/' && strstr(tokens->str[i]->s, "="))
		{
			cur->opcode = AMTAIL_AST_OPCODE_ASSIGN;
			++i;

			if ((strcmp(tokens->str[i - 1]->s, "+=")) || (strcmp(tokens->str[i]->s, "+")))
			{
				cur->opcode = AMTAIL_AST_OPCODE_PLUS;
				cur->stem = amtail_ast_multi_init(2);
				cur->stem[AMTAIL_AST_LEFT]->name = tokens->str[i - 1];
				cur->stem[AMTAIL_AST_RIGHT]->name = tokens->str[i];
			}

			else if ((strcmp(tokens->str[i - 1]->s, "-=")) || (strcmp(tokens->str[i]->s, "-")))
			{
				cur->opcode = AMTAIL_AST_OPCODE_MINUS;
				cur->stem = amtail_ast_multi_init(2);
				cur->stem[AMTAIL_AST_LEFT]->name = tokens->str[i - 1];
				cur->stem[AMTAIL_AST_RIGHT]->name = tokens->str[i];
			}

			else if ((strcmp(tokens->str[i - 1]->s, "/=")) || (strcmp(tokens->str[i]->s, "/")))
			{
				cur->opcode = AMTAIL_AST_OPCODE_DIV;
				cur->stem = amtail_ast_multi_init(2);
				cur->stem[AMTAIL_AST_LEFT]->name = tokens->str[i - 1];
				cur->stem[AMTAIL_AST_RIGHT]->name = tokens->str[i];
			}

			else if ((strcmp(tokens->str[i - 1]->s, "*=")) || (strcmp(tokens->str[i]->s, "*")))
			{
				cur->opcode = AMTAIL_AST_OPCODE_MUL;
				cur->stem = amtail_ast_multi_init(2);
				cur->stem[AMTAIL_AST_LEFT]->name = tokens->str[i - 1];
				cur->stem[AMTAIL_AST_RIGHT]->name = tokens->str[i];
			}

			if (amtail_ll.parser > 0)
				printf("calculation [%"PRIu64"]: '%s' type: %d\n", i, tokens->str[i]->s, cur->opcode);
		}
		else if (tokens->str[i]->s[0] != '/' && strstr(tokens->str[i]->s, "++"))
		{
			cur->opcode = AMTAIL_AST_OPCODE_INC;
			cur->name = string_init_alloc(tokens->str[i]->s, tokens->str[i]->l - 3);

			if (amtail_ll.parser > 0)
				printf("increment [%"PRIu64"]: '%s'\n", i, tokens->str[i]->s);
		}
		else if (tokens->str[i]->s[0] != '/' && strstr(tokens->str[i]->s, "--"))
		{
			cur->opcode = AMTAIL_AST_OPCODE_DEC;

			if (amtail_ll.parser > 0)
				printf("decrement [%"PRIu64"]: '%s'\n", i, tokens->str[i]->s);
		}
		else if (!strcmp(tokens->str[i]->s, "\n")) // NEWLINE
		{
			if (cur->name)
			{
				cur->stem = amtail_ast_multi_init(1);
				cur = cur->stem[AMTAIL_AST_LEFT];
			}

			if (amtail_ll.parser > 1)
				printf("NEWLINE [%"PRIu64"]: '%s'\n", i, (cur->name ? cur->name->s : "NULL"));

			hidden = 0;
			vartype = 0;
		}
		else if (!strcmp(tokens->str[i]->s, "next"))
		{
			if (amtail_ll.parser > 0)
				printf("NEXT [%"PRIu64"]\n", i);
		}
		else if (!strcmp(tokens->str[i]->s, "stop"))
		{
			if (amtail_ll.parser > 0)
				printf("STOP [%"PRIu64"]\n", i);
		}
		else if (!strcmp(tokens->str[i]->s, "else"))
		{
			if (amtail_ll.parser > 0)
				printf("ELSE [%"PRIu64"]\n", i);

			e_else = 1;
		}
		else if (!strcmp(tokens->str[i]->s, "otherwise"))
		{
			if (amtail_ll.parser > 0)
				printf("OTHERWISE [%"PRIu64"]\n", i);

			otherwise = 1;
		}
		else if (!strcmp(tokens->str[i]->s, "del"))
		{
			if (amtail_ll.parser > 0)
				printf("DEL [%"PRIu64"]\n", i);
		}
		else if (!strcmp(tokens->str[i]->s, "{"))
		{
			if (expression || otherwise || e_else)
			{
				if (amtail_ll.parser > 0)
				{
					puts("\t\t!!! {");
					printf("full expr is '%s'\n", expr->s);
				}

				cur->name = string_init_alloc(expr->s, expr->l);
				cur->opcode = AMTAIL_AST_OPCODE_BRANCH;

				amtail_ast_stack_push(stack, cur);
				cur->stem = amtail_ast_multi_init(2);

				if (amtail_ll.parser > 0)
					printf("PUSH cur %p/%p PP %p/%p %"PRIu64"\n", cur, cur->stem, cur->stem[AMTAIL_AST_LEFT], cur->stem[AMTAIL_AST_RIGHT], identy);

				cur = cur->stem[AMTAIL_AST_LEFT];
				string_null(expr);
				++identy;

				expression = 0;
				otherwise = 0;
				e_else = 0;
			}
			else if (func_declaration)
			{
				//cur->name = string_init_alloc(expr->s, expr->l);
				cur->opcode = AMTAIL_AST_OPCODE_FUNC_DECLARATION;

				if (amtail_ll.parser > 0)
					printf("func declaration is '%s'\n", cur->name->s);

				amtail_ast_stack_push(stack, cur);
				cur->stem = amtail_ast_multi_init(2);
				cur = cur->stem[AMTAIL_AST_LEFT];
				//string_null(expr);
				++identy;

				func_declaration = 0;
			}
			else if (func_call)
			{
				//cur->name = string_init_alloc(expr->s, expr->l);
				cur->opcode = AMTAIL_AST_OPCODE_FUNC_DECLARATION;

				if (amtail_ll.parser > 0)
					printf("func call is '%s'\n", cur->name->s);

				amtail_ast_stack_push(stack, cur);
				cur->stem = amtail_ast_multi_init(2);
				cur = cur->stem[AMTAIL_AST_LEFT];
				//string_null(expr);
				++identy;

				func_call = 0;
			}
			else
			{
				if (amtail_ll.parser > 0)
					puts("govno!");
			}
		}
		else if (!strcmp(tokens->str[i]->s, "}"))
		{
			if (amtail_ll.parser > 0)
				puts("\t\t!!! }");

			if (!dec_identy(&identy))
			{
				if (amtail_ll.parser > 0)
					printf("panic '%s'!!!\n", name);

				amtail_parser_print_stack(tokens, i);
				return NULL;
			}

			cur = amtail_ast_stack_pop(stack);
			if (!cur)
			{
				if (amtail_ll.parser > 0)
					printf("panic2 '%s'!!!\n", name);

				amtail_parser_print_stack(tokens, i);
				return NULL;
			}

			if (amtail_ll.parser > 0)
				printf("%s POP cur %p/%p PP %p/%p %"PRIu64"\n", name, cur, cur->stem, cur->stem[AMTAIL_AST_LEFT], cur->stem[AMTAIL_AST_RIGHT], identy);

			cur = cur->stem[AMTAIL_AST_RIGHT];
		}
		else if (*tokens->str[i]->s == '@')
		{
			if (amtail_ll.parser > 0)
				printf("decorator_call [%"PRIu64"]: '%s'\n", i, tokens->str[i]->s);

			func_call = 1;
			//string_string_cat(expr, tokens->str[i]);
			cur->name = string_init_alloc(tokens->str[i]->s, tokens->str[i]->l);
			cur->opcode = AMTAIL_AST_OPCODE_FUNC_CALL;
			expression = 0;
		}
		else if (begin)
		{
			if (amtail_ll.parser > 0)
				printf("expression [%"PRIu64"]: '%s'\n", i, tokens->str[i]->s);

			expression = 1;
			string_string_cat(expr, tokens->str[i]);
		}
		else if (expression)
		{
			string_cat(expr, " ", 1);
			string_string_cat(expr, tokens->str[i]);

			if (amtail_ll.parser > 0)
				printf("internal expression [%"PRIu64"]: '%s' / (%p)\n", i, tokens->str[i]->s, expr->s );
		}
		else
			if (amtail_ll.parser > 0)
				printf("tok [%"PRIu64"]: '%s'\n", i, tokens->str[i]->s);

		if (!strcmp(tokens->str[i]->s, "\n"))
			begin = 1;
		else
			begin = 0;
	}

	string_free(expr);

	if (amtail_ll.parser)
		puts("\t<-----> END PARSER <----->");

	return ast;
}

