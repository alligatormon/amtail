#include "common/selector.h"
#include <string.h>
//#include "sstring.h"
#include "parser.h"
#include "variables.h"
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
	if (!ast)
		return;

	if (ast->stem)
	{
		amtail_ast *left = ast->stem[AMTAIL_AST_LEFT];
		amtail_ast *right = ast->stem[AMTAIL_AST_RIGHT];
		if (left && left != ast)
			amtail_ast_free(left);
		if (right && right != ast)
			amtail_ast_free(right);
		free(ast->stem);
	}

	if (ast->name)
		string_free(ast->name);

	if (ast->export_name)
		string_free(ast->export_name);

	if (*ast->by)
	{
		for (uint64_t i = 0; i < ast->by_count; ++i)
		{
			if (ast->by[i])
				string_free(ast->by[i]);
		}
	}
	if (*ast->bucket)
	{
		for (uint64_t i = 0; i < ast->bucket_count; ++i)
		{
			if (ast->bucket[i])
				string_free(ast->bucket[i]);
		}
	}

	if (ast->opcode == AMTAIL_AST_OPCODE_VARIABLE &&
	    (ast->vartype == ALLIGATOR_VARTYPE_TEXT || ast->vartype == ALLIGATOR_VARTYPE_CONST) &&
	    ast->facttype == ALLIGATOR_FACTTYPE_TEXT &&
	    ast->svalue)
	{
		string_free(ast->svalue);
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

char **allvartypes;
void amtail_parser_init() {
	allvartypes = calloc(1, 5 * sizeof(void*));
	allvartypes[ALLIGATOR_VARTYPE_COUNTER] = "counter";
	allvartypes[ALLIGATOR_VARTYPE_GAUGE] = "gauge";
	allvartypes[ALLIGATOR_VARTYPE_TEXT] = "text";
	allvartypes[ALLIGATOR_VARTYPE_CONST] = "const";
	allvartypes[ALLIGATOR_VARTYPE_HISTOGRAM] = "histogram";
}

char *vartype_get(uint8_t vartype)
{
	return allvartypes[vartype];
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

// shunting yard start
calculation_cluster* calculation_new(uint64_t size) {
	calculation_cluster *ret = calloc(1, sizeof(*ret));
	ret->qmax = size;
	ret->queue = calloc(1, size * sizeof(*ret->queue));
	ret->qcur = 0;
	ret->stack = calloc(1, size);
	ret->smax = size;
	ret->scur = 0;

	//printf("CC return %p\n", ret);
	return ret;
}

void calculation_push_queue(calculation_cluster *calculation_expr, char *str, size_t len) {
	if (len == 1)
		printf("\tcalculation_push_queue is %p, ll is %llu, '%c'(1)\n", calculation_expr, (unsigned long long)calculation_expr->qcur, *str);
	else
		printf("\tcalculation_push_queue is %p, ll is %llu, '%s'(%zu)\n", calculation_expr, (unsigned long long)calculation_expr->qcur, str, len);
	calculation_expr->queue[calculation_expr->qcur].svalue = string_init_alloc(str, len);
	calculation_expr->queue[calculation_expr->qcur].vartype = ALLIGATOR_VARTYPE_TEXT;
	++calculation_expr->qcur;
}

void calculation_push_stack(calculation_cluster *calculation_expr, char str) {
	//printf("\tcalculation_push_stack is %p, ll is %llu, sym '%c'\n", calculation_expr, (unsigned long long)calculation_expr->scur, str);
	calculation_expr->stack[calculation_expr->scur++] = str;
}

char calculation_pop_stack(calculation_cluster *calculation_expr) {
	if (calculation_expr->scur < 1)
		return 0;
	//printf("\tcalculation_pop_stack is %p, ll is %llu, pop sym: '%c'\n", calculation_expr, (unsigned long long)calculation_expr->scur, calculation_expr->stack[calculation_expr->scur-1]);
	return calculation_expr->stack[--calculation_expr->scur];
}

char calculation_peek_stack(calculation_cluster *calculation_expr) {
	if (calculation_expr->scur < 1)
		return 0;
	//printf("\tcalculation_peek_stack is %p, ll is %llu, peek sym: '%c'\n", calculation_expr, (unsigned long long)calculation_expr->scur, calculation_expr->stack[calculation_expr->scur-1]);
	return calculation_expr->stack[calculation_expr->scur-1];
}

void calculation_del_queue(calculation_cluster *cc) {
	//printf("\tcc delete %p\n", cc);
	for (uint64_t i = 0; i < cc->qmax; ++i)
	{
		if (cc->queue[i].vartype == ALLIGATOR_VARTYPE_TEXT)
			string_free(cc->queue[i].svalue);
	}

	free(cc->queue);
	free(cc->stack);
	free(cc);
}

static void amtail_parser_const_free(void *arg)
{
	amtail_variable *var = arg;
	if (!var)
		return;

	if (var->facttype == ALLIGATOR_FACTTYPE_TEXT && var->s)
		string_free(var->s);
	free(var);
}
void calculation_flush(calculation_cluster **calculation_ptr, amtail_ast *stack, amtail_ast **ccur) {
	if (!*calculation_ptr)
		return;

	printf("calculation_flush!\n");
	amtail_ast *cur = *ccur;

	if (!cur)
	{
		cur = amtail_ast_init();
		if (!cur)
		{
			printf("error: failed to create AST node in calculation_flush\n");
			*calculation_ptr = NULL;
			return;
		}
		*ccur = cur;
	}

	calculation_cluster *calculation_expr = *calculation_ptr;

	char elem = 1;
	while (elem != 0)
	{
		elem = calculation_pop_stack(calculation_expr);
		if (!elem)
			break;

		calculation_push_queue(calculation_expr, &elem, 1);
	}

	printf("sval: ");
	for (uint64_t i = 0; i < calculation_expr->qcur; ++i)
	{
		if (calculation_expr->queue[i].vartype == ALLIGATOR_VARTYPE_TEXT)
		{
			char *expr = calculation_expr->queue[i].svalue->s;
			uint64_t size = calculation_expr->queue[i].svalue->l;
			printf(" '%s' ", expr);
			cur->name = string_init_alloc(expr, size);
			if (*expr == '^')
				cur->opcode = AMTAIL_AST_OPCODE_POW;
			else if (*expr == '*')
				cur->opcode = AMTAIL_AST_OPCODE_MUL;
			else if (*expr == '/')
				cur->opcode = AMTAIL_AST_OPCODE_DIV;
			else if (*expr == '+')
				cur->opcode = AMTAIL_AST_OPCODE_ADD;
			else if (*expr == '-')
				cur->opcode = AMTAIL_AST_OPCODE_SUB;
			else
			{
				cur->opcode = AMTAIL_AST_OPCODE_VAR;


				if (strstr(cur->name->s, ".")) // if float point number
				{
					cur->vartype = ALLIGATOR_VARTYPE_GAUGE;
					cur->dvalue = strtod(cur->name->s, NULL);
				}
				else if (sscanf(cur->name->s, "%"PRId64, &cur->ivalue)) // otherwise if integer
				{
					cur->vartype = ALLIGATOR_VARTYPE_COUNTER;
					cur->ivalue = strtoll(cur->name->s, NULL, 10);
				}
			}
		}

		amtail_ast_stack_push(stack, cur);
		cur->stem = amtail_ast_multi_init(2);
		cur = cur->stem[AMTAIL_AST_LEFT];
	}
	puts("");

	cur->opcode = AMTAIL_AST_OPCODE_RUN;
	amtail_ast_stack_push(stack, cur);
	cur->stem = amtail_ast_multi_init(2);
	cur = cur->stem[AMTAIL_AST_LEFT];
	*ccur = cur;

	calculation_del_queue(calculation_expr);

	*calculation_ptr = NULL;
}

int is_calculation_op(char op)
{
	if (
		   (op == '_')
		|| (op == '^')
		|| (op == '*')
		|| (op == '/')
		|| (op == '%')
		|| (op == '+')
		|| (op == '-')
		|| (op == '(')
		|| (op == ')')
	)
		return 1;
	return 0;
}
// shunting yard end

void state_flush(uint8_t *dst, uint8_t *st) {
	if (*st)
		*dst = *st - 1;
	*st = 0;
}

static string* amtail_collect_lhs_tokens(string_tokens *tokens, uint64_t op_index)
{
	if (!tokens || op_index == 0 || op_index > tokens->l)
		return NULL;

	uint64_t start = op_index;
	while (start > 0)
	{
		char *prev = tokens->str[start - 1]->s;
		if (!strcmp(prev, "\n") || !strcmp(prev, "{") || !strcmp(prev, "}"))
			break;
		--start;
	}

	string *lhs = string_new();
	for (uint64_t i = start; i < op_index; ++i)
	{
		char *t = tokens->str[i]->s;
		if (!strcmp(t, ","))
			continue;
		string_string_cat(lhs, tokens->str[i]);
	}

	if (!lhs->l)
	{
		string_free(lhs);
		return NULL;
	}

	return lhs;
}




amtail_ast* amtail_parser(string_tokens *tokens, char *name, amtail_log_level amtail_ll)
{
	amtail_ast *ast = amtail_ast_init();
	amtail_ast *stack = amtail_ast_init();
	amtail_ast *cur = ast;
	uint64_t identy = 0;
	alligator_ht *const_data = alligator_ht_init(NULL);
	calculation_cluster *calculation_expr = NULL;
	uint8_t calculation_op[255];
	memset(calculation_op, 0, 255);
	calculation_op['_'] = 10;
	calculation_op['^'] = 9;
	calculation_op['*'] = 8;
	calculation_op['/'] = 8;
	calculation_op['%'] = 8;
	calculation_op['+'] = 5;
	calculation_op['-'] = 5;
	calculation_op['('] = 0;
	calculation_op[')'] = 0;
	char calculation_lastop = 0;
	string *last_token = NULL;
	parser_state pstate;
	memset(&pstate, 0, sizeof(pstate));

	if (amtail_ll.parser)
		puts("\t<====> PARSER STAGE <====>");

	for (uint64_t i = 0; i < tokens->l; i++)
	{
		string *tok = tokens->str[i];
		char *t = tok->s;

		if (amtail_ll.parser > 1)
			printf("token[%"PRIu64"] '%s'\n", i, t);

		if (!strcmp(t, "\n"))
		{
			if (!cur)
			{
				cur = amtail_ast_init();
				if (!cur)
					return NULL;
			}

			if (calculation_expr)
			{
				calculation_flush(&calculation_expr, stack, &cur);
				pstate.expression = 0;
				calculation_lastop = 0;
			}

			state_flush(&cur->by_count, &pstate.variable_by);
			state_flush(&cur->bucket_count, &pstate.variable_bucket);
			pstate.branch = 0;
			last_token = NULL;

			if (cur->opcode != AMTAIL_AST_OPCODE_NOOP || cur->name || cur->export_name)
			{
				/*
				 * Use 2 children everywhere to keep LEFT/RIGHT access safe.
				 * Some control-flow paths expect RIGHT child after stack pop.
				 */
				cur->stem = amtail_ast_multi_init(2);
				if (cur->stem && cur->stem[AMTAIL_AST_LEFT])
					cur = cur->stem[AMTAIL_AST_LEFT];
				else
				{
					if (amtail_ll.parser)
						printf("warning: failed to allocate AST line node at %"PRIu64"\n", i);
					cur = NULL;
					break;
				}
			}
			continue;
		}

		if (!strcmp(t, "{"))
		{
			++identy;
			cur->opcode = AMTAIL_AST_OPCODE_BRANCH;
			pstate.branch = 0;

			amtail_ast_stack_push(stack, cur);
			cur->stem = amtail_ast_multi_init(2);
			cur = cur->stem[AMTAIL_AST_LEFT];
			continue;
		}

		if (!strcmp(t, "}"))
		{
			if (calculation_expr)
			{
				calculation_flush(&calculation_expr, stack, &cur);
				pstate.expression = 0;
				calculation_lastop = 0;
			}

			if (!dec_identy(&identy))
			{
				if (amtail_ll.parser)
					printf("panic '%s': unexpected '}'\n", name);
				amtail_parser_print_stack(tokens, i);
				return NULL;
			}

			cur = amtail_ast_stack_pop(stack);
			if (!cur)
			{
				if (amtail_ll.parser)
					printf("panic2 '%s': parser stack underflow\n", name);
				amtail_parser_print_stack(tokens, i);
				return NULL;
			}
			if (!cur->stem || !cur->stem[AMTAIL_AST_RIGHT])
			{
				if (!cur->stem)
					cur->stem = amtail_ast_multi_init(2);
				cur = cur->stem[AMTAIL_AST_RIGHT];
			}
			else
			{
				cur = cur->stem[AMTAIL_AST_RIGHT];
			}
			continue;
		}

		if (!strcmp(t, "hidden"))
		{
			cur->hidden = 1;
			continue;
		}
		if (!strcmp(t, "def"))
		{
			cur->opcode = AMTAIL_AST_OPCODE_FUNC_DECLARATION;
			if (i + 1 < tokens->l && strcmp(tokens->str[i + 1]->s, "\n"))
			{
				++i;
				cur->name = string_string_init_dup(tokens->str[i]);
			}
			continue;
		}
		if (t[0] == '@')
		{
			cur->opcode = AMTAIL_AST_OPCODE_FUNC_CALL;
			cur->name = string_string_init_dup(tok);
			last_token = tok;
			continue;
		}
		if (!strcmp(t, "counter"))
		{
			cur->opcode = AMTAIL_AST_OPCODE_VARIABLE;
			cur->vartype = ALLIGATOR_VARTYPE_COUNTER;
			continue;
		}
		if (!strcmp(t, "gauge"))
		{
			cur->opcode = AMTAIL_AST_OPCODE_VARIABLE;
			cur->vartype = ALLIGATOR_VARTYPE_GAUGE;
			continue;
		}
		if (!strcmp(t, "histogram"))
		{
			cur->opcode = AMTAIL_AST_OPCODE_VARIABLE;
			cur->vartype = ALLIGATOR_VARTYPE_HISTOGRAM;
			continue;
		}
		if (!strcmp(t, "const"))
		{
			cur->opcode = AMTAIL_AST_OPCODE_VARIABLE;
			cur->vartype = ALLIGATOR_VARTYPE_CONST;
			cur->hidden = 1;
			continue;
		}

		if (!strcmp(t, "by"))
		{
			++pstate.variable_by;
			state_flush(&cur->bucket_count, &pstate.variable_bucket);
			continue;
		}
		if ((!strcmp(t, "bucket") || !strcmp(t, "buckets")) && cur->vartype == ALLIGATOR_VARTYPE_HISTOGRAM)
		{
			++pstate.variable_bucket;
			state_flush(&cur->by_count, &pstate.variable_by);
			continue;
		}

		if (pstate.variable_by)
		{
			cur->by[pstate.variable_by++ - 1] = string_init_alloc(tok->s, tok->l);
			continue;
		}
		if (pstate.variable_bucket)
		{
			cur->bucket[pstate.variable_bucket++ - 1] = string_init_alloc(tok->s, tok->l);
			continue;
		}

		if (cur->opcode == AMTAIL_AST_OPCODE_VARIABLE && !cur->name)
		{
			cur->name = string_string_init_dup(tok);
			if (amtail_ll.parser)
				printf("parsed variable [%s] %s [%"PRIu64"]: '%s'\n",
				       hiden_visible(cur->hidden), vartype_get(cur->vartype), i, t);
			continue;
		}

		if (cur->opcode == AMTAIL_AST_OPCODE_VARIABLE && cur->vartype == ALLIGATOR_VARTYPE_CONST && cur->name)
		{
			uint32_t name_hash = amtail_hash(cur->name->s, cur->name->l);
			amtail_variable *var = amtail_variable_make(1, cur->vartype, cur->name->s, NULL, NULL, 0, 0);
			variable_parse_set_value(var, tok);
			alligator_ht_insert(const_data, &(var->node), var, name_hash);

			cur->facttype = var->facttype;
			if (var->facttype == ALLIGATOR_FACTTYPE_INT)
				cur->ivalue = var->i;
			else if (var->facttype == ALLIGATOR_FACTTYPE_DOUBLE)
				cur->dvalue = var->d;
			else if (var->facttype == ALLIGATOR_FACTTYPE_TEXT)
			{
				if (cur->svalue)
					string_free(cur->svalue);
				cur->svalue = string_string_init_dup(var->s);
			}
			continue;
		}

		/* Parse action statements */
		if (!strcmp(t, "stop"))
		{
			cur->opcode = AMTAIL_AST_OPCODE_FUNC_STOP;
			last_token = tok;
			continue;
		}
		if (!strcmp(t, "next"))
		{
			cur->opcode = AMTAIL_AST_OPCODE_FUNC_JMP;
			last_token = tok;
			continue;
		}
		if (!strcmp(t, "otherwise"))
		{			/*
			 * else-like branch marker; keep it explicit in AST so codegen/vm
			 * can wire control-flow later.
			 */
			cur->opcode = AMTAIL_AST_OPCODE_FUNC_CMP;
			cur->name = string_init_alloc("otherwise", 9);
			last_token = tok;
			continue;
		}

		if (!strcmp(t, "=") || !strcmp(t, "+=") || !strcmp(t, "-=") || !strcmp(t, "*=") || !strcmp(t, "/="))
		{
			string *lhs = amtail_collect_lhs_tokens(tokens, i);
			if (!lhs)
				continue;

			cur->opcode = AMTAIL_AST_OPCODE_ASSIGN;
			cur->name = lhs;
			amtail_ast_stack_push(stack, cur);
			cur->stem = amtail_ast_multi_init(2);
			cur = cur->stem[AMTAIL_AST_LEFT];

			calculation_expr = calculation_new(255);
			pstate.expression = 1;
			calculation_lastop = 0;

			if (!strcmp(t, "+=") || !strcmp(t, "-=") || !strcmp(t, "*=") || !strcmp(t, "/="))
			{
				calculation_push_queue(calculation_expr, lhs->s, lhs->l);
				calculation_push_stack(calculation_expr, t[0]);
				calculation_lastop = calculation_op[(uint8_t)t[0]];
			}
			continue;
		}

		if (pstate.expression && calculation_expr)
		{
			if (is_calculation_op(t[0]) && t[1] == '\0')
			{
				char curop = calculation_op[(uint8_t)t[0]];
				if (curop < calculation_lastop)
				{
					while (curop < calculation_lastop)
					{
						char elem = calculation_pop_stack(calculation_expr);
						if (!elem)
							break;
						calculation_push_queue(calculation_expr, &elem, 1);
						if (calculation_expr->scur < 1)
						{
							calculation_lastop = 0;
							break;
						}
						calculation_lastop = calculation_op[(uint8_t)calculation_peek_stack(calculation_expr)];
					}
				}
				calculation_push_stack(calculation_expr, t[0]);
				calculation_lastop = curop;
			}
			else
			{
				calculation_push_queue(calculation_expr, tok->s, tok->l);
			}
			last_token = tok;
			continue;
		}

		/* Parse condition operators used before branch blocks. */
		if (!strcmp(t, "<") || !strcmp(t, "<=") || !strcmp(t, ">") || !strcmp(t, ">=") ||
		    !strcmp(t, "==") || !strcmp(t, "!=") || !strcmp(t, "=~") || !strcmp(t, "!~") ||
		    !strcmp(t, "&&") || !strcmp(t, "||") || !strcmp(t, "!"))
		{
			if (!strcmp(t, "<"))
				cur->opcode = AMTAIL_AST_OPCODE_LT;
			else if (!strcmp(t, "<="))
				cur->opcode = AMTAIL_AST_OPCODE_LE;
			else if (!strcmp(t, ">"))
				cur->opcode = AMTAIL_AST_OPCODE_GT;
			else if (!strcmp(t, ">="))
				cur->opcode = AMTAIL_AST_OPCODE_GE;
			else if (!strcmp(t, "=="))
				cur->opcode = AMTAIL_AST_OPCODE_EQ;
			else if (!strcmp(t, "!="))
				cur->opcode = AMTAIL_AST_OPCODE_NE;
			else if (!strcmp(t, "=~"))
				cur->opcode = AMTAIL_AST_OPCODE_MATCH;
			else if (!strcmp(t, "!~"))
				cur->opcode = AMTAIL_AST_OPCODE_NOTMATCH;
			else if (!strcmp(t, "&&"))
				cur->opcode = AMTAIL_AST_OPCODE_AND;
			else if (!strcmp(t, "||"))
				cur->opcode = AMTAIL_AST_OPCODE_OR;
			else
				cur->opcode = AMTAIL_AST_OPCODE_NOT;

			if (!cur->name)
				cur->name = string_new();

			if (last_token)
				string_string_cat(cur->name, last_token);

			if (i + 1 < tokens->l && strcmp(tokens->str[i + 1]->s, "\n") && strcmp(tokens->str[i + 1]->s, "{"))
			{
				string_cat(cur->name, " ", 1);
				string_string_cat(cur->name, tokens->str[i + 1]);
				++i;
			}

			pstate.branch = 1;
			last_token = tok;
			continue;
		}

		if (!strcmp(t, "++") && last_token)
		{
			string *lhs = amtail_collect_lhs_tokens(tokens, i);
			if (!lhs)
				lhs = string_string_init_dup(last_token);
			cur->opcode = AMTAIL_AST_OPCODE_INC;
			cur->name = lhs;
			last_token = tok;
			continue;
		}

		if (!strcmp(t, "--") && last_token)
		{
			string *lhs = amtail_collect_lhs_tokens(tokens, i);
			if (!lhs)
				lhs = string_string_init_dup(last_token);
			cur->opcode = AMTAIL_AST_OPCODE_DEC;
			cur->name = lhs;
			last_token = tok;
			continue;
		}

		if (tok->l > 2 && strstr(t, "++") == t + tok->l - 2)
		{
			cur->opcode = AMTAIL_AST_OPCODE_INC;
			cur->name = string_init_alloc(tok->s, tok->l - 2);
			last_token = tok;
			continue;
		}

		if (tok->l > 2 && strstr(t, "--") == t + tok->l - 2)
		{
			cur->opcode = AMTAIL_AST_OPCODE_DEC;
			cur->name = string_init_alloc(tok->s, tok->l - 2);
			last_token = tok;
			continue;
		}

		if (t[0] == '/' || (!pstate.expression && t[0] == '+') || pstate.branch ||
			alligator_ht_search(const_data, amtail_variable_compare, t, amtail_hash(t, tok->l)))
		{
			uint8_t created_name = 0;
			pstate.branch = 1;
			if (!strcmp(t, "//") || !strcmp(t, "+"))
				continue;

			if (!cur->name)
			{
				cur->name = string_new();
				created_name = 1;
			}

			if (t[0] == '/')
			{
				if (t[tok->l - 1] == '/')
					string_cat(cur->name, t + 1, tok->l - 2);
				else
					string_string_cat(cur->name, tok);
			}
			else
			{
				amtail_variable *v = alligator_ht_search(const_data, amtail_variable_compare, t, amtail_hash(t, tok->l));
				if (!v)
				{
					if (created_name)
					{
						string_free(cur->name);
						cur->name = NULL;
					}
					continue;
				}

				if (v->facttype == ALLIGATOR_FACTTYPE_TEXT && v->s->l >= 2 && v->s->s[0] == '/' && v->s->s[v->s->l - 1] == '/')
					string_cat(cur->name, v->s->s + 1, v->s->l - 2);
				else if (v->facttype == ALLIGATOR_FACTTYPE_DOUBLE)
					string_double(cur->name, v->d);
				else if (v->facttype == ALLIGATOR_FACTTYPE_INT)
					string_int(cur->name, v->i);
				else if (v->facttype == ALLIGATOR_FACTTYPE_TEXT)
					string_string_cat(cur->name, v->s);
			}
			continue;
		}

		last_token = tok;
	}

	if (calculation_expr)
		calculation_flush(&calculation_expr, stack, &cur);

	if (amtail_ll.parser)
		puts("\t<-----> END PARSER <----->");

	if (const_data)
	{
		alligator_ht_forfree(const_data, amtail_parser_const_free);
		free(const_data);
	}
	amtail_ast_free(stack);

	return ast;
}
char *opname_from_code(uint64_t opcode) {
	char *namecodes[256] = {
		"AMTAIL_AST_OPCODE_NOOP",
		"AMTAIL_AST_OPCODE_VARIABLE",
		"AMTAIL_AST_OPCODE_BRANCH",
		"AMTAIL_AST_OPCODE_INC",
		"AMTAIL_AST_OPCODE_DEC",
		"AMTAIL_AST_OPCODE_FUNC_DECLARATION",
		"AMTAIL_AST_OPCODE_FUNC_CALL",
		"AMTAIL_AST_OPCODE_FUNC_STOP",
		"AMTAIL_AST_OPCODE_FUNC_MATCH",
		"AMTAIL_AST_OPCODE_FUNC_CMP",
		"AMTAIL_AST_OPCODE_FUNC_JMP",
		"AMTAIL_AST_OPCODE_ADD",
		"AMTAIL_AST_OPCODE_SUB",
		"AMTAIL_AST_OPCODE_MUL",
		"AMTAIL_AST_OPCODE_DIV",
		"AMTAIL_AST_OPCODE_MOD",
		"AMTAIL_AST_OPCODE_POW",
		"AMTAIL_AST_OPCODE_ASSIGN",
		"AMTAIL_AST_OPCODE_ADD_ASSIGN",
		"AMTAIL_AST_OPCODE_MATCH",
		"AMTAIL_AST_OPCODE_NOTMATCH",
		"AMTAIL_AST_OPCODE_AND",
		"AMTAIL_AST_OPCODE_OR",
		"AMTAIL_AST_OPCODE_LT",
		"AMTAIL_AST_OPCODE_GT",
		"AMTAIL_AST_OPCODE_LE",
		"AMTAIL_AST_OPCODE_GE",
		"AMTAIL_AST_OPCODE_EQ",
		"AMTAIL_AST_OPCODE_NE",
		"AMTAIL_AST_OPCODE_FUNC_STRPTIME",
		"AMTAIL_AST_OPCODE_FUNC_TIMESTAMP",
		"AMTAIL_AST_OPCODE_FUNC_TOLOWER",
		"AMTAIL_AST_OPCODE_FUNC_LEN",
		"AMTAIL_AST_OPCODE_FUNC_STRTOL",
		"AMTAIL_AST_OPCODE_FUNC_SETTIME",
		"AMTAIL_AST_OPCODE_FUNC_GETFILENAME",
		"AMTAIL_AST_OPCODE_FUNC_INT",
		"AMTAIL_AST_OPCODE_FUNC_BOOL",
		"AMTAIL_AST_OPCODE_FUNC_FLOAT",
		"AMTAIL_AST_OPCODE_FUNC_STRING",
		"AMTAIL_AST_OPCODE_FUNC_SUBST",
		"AMTAIL_AST_OPCODE_REGEX",
		"AMTAIL_AST_OPCODE_VAR",
		"AMTAIL_AST_OPCODE_RUN",
	};

	return namecodes[opcode];
}

