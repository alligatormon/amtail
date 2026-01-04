#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "common/selector.h"
//#include "sstring.h"
#include "log.h"
#include "file.h"
#define OPLEN 255

char *strmbtok(char *start, char *input, char **inout, uint64_t *size, char *delimit, char *openblock, char *closeblock, uint8_t *is_expression, int8_t *bracket, amtail_log_level amtail_ll)
{
	*size = 0;
	char *token = input;
	char *lead = input;
	char *block = NULL;
	int iBlock = 0;
	int iBlockIndex = 0;
	char quota;
	uint8_t quotas = 0;

	if (!input)
		*inout = input;

	if (*input == '\0')
	{
		*inout = input;
		return NULL;
	}

	if (*token == '\0')
		lead = NULL;

	uint8_t first = 1;
	while (1)
	{
		*inout = token;

		if (!*token)
		{
			*size = *inout - input;

			if (amtail_ll.lexer > 1 && ((token - 3) >= start))
				printf("\texit 0: '%s'\n", strndup(token-3, 3));

			break;
		}

		if (!iBlock && *token == '#') // need exit?
			token += strcspn(token, "\n");

		if (iBlock)
		{
			if ((closeblock[iBlockIndex] == *token) && (token > start) && (token[-1] != '\\') && (!quotas) && (!*is_expression))
				iBlock = 0;

			++token;
			continue;
		}
		if (((block = strchr(openblock, *token)) != NULL) && first &&  (!*is_expression))
		{
			if (amtail_ll.lexer > 1 && ((token - 1) >= start))
				printf("\ttoken '%c', token pre: '%c'\n", *token, token[-1]);

			iBlock = 1;
			iBlockIndex = block - openblock;
			++token;
			continue;
		}
		if (((block = strchr(openblock, *token)) != NULL) && !first && (token[-1] != '\\') && (!quotas) && (!*is_expression))
		{
			if (amtail_ll.lexer > 1 && ((token - 1) >= start))
				printf("\ttoken '%c', token pre: '%c'\n", *token, token[-1]);

			iBlock = 1;
			iBlockIndex = block - openblock;
			++token;
			continue;
		}
		if (strchr(delimit, *token) != NULL) {
			*token = '\0';
			*size = *inout - input;

			++token;
			*inout = token;

			if (amtail_ll.lexer > 1)
				printf("\texit 1: '%s'\n", strndup(token, 15));
			break;
		}

		if (*token == '(' && !*bracket && !iBlock) {
			*size = *inout - input;
			++token;
			*inout = token;

			*bracket = 1;
			break;
		}

		if (*token == ')' && !iBlock && *bracket == 1) {
			*size = *inout - input;
			++token;
			*inout = token;

			*bracket = 0;
			break;
		}

		if ((*token == '=') && !iBlock) {
			*is_expression = 1;
		}

		if (*token == '"' || *token == '\'')
		{
			if (quotas)
			{
				if (quota == *token)
				{
					quotas = 0;
				}
			}
			else
			{
				quotas = 1;
				quota = *token;
			}
		}

		++token;
		first = 0;
	}
	return lead;
}


void strmbtok_amtail_lexer(char *inp, string_tokens *st, char *openblock, char *closeblock, char *name, amtail_log_level amtail_ll)
{
	char *start = inp;
	char *ltoken;
	char *token;
	uint64_t sz;

	uint8_t glob_expression = 0;
	int8_t glob_bracket = -1;
	while ((ltoken = strmbtok (start, inp, &inp, &sz, "\n", openblock, closeblock, &glob_expression, &glob_bracket, amtail_ll)) != NULL)
	{
		char *word = ltoken;
		uint8_t is_expression = 0;
		int8_t is_bracket = 0;

		if (amtail_ll.lexer > 0)
			printf("\tTOK: '%s'\n", ltoken);

		while ((token = strmbtok(start, word, &word, &sz, " \t\n,", openblock, closeblock, &is_expression, &is_bracket, amtail_ll)))
		{
			// skip comment
			if (!strncmp(token, "#", 1))
				break;

			if (sz < 1)
			{
				if (amtail_ll.lexer > 0)
					printf("NO SIZE: '%s' (%llu)\n", token, sz);
				continue;
			}

			if (*token == '=' && sz == 1)
				is_expression = 1;

			if (amtail_ll.lexer > 0)
				printf("FILE %s TOKEN SIZE: '%s' (%llu) expr: %d, index %llu\n", name, token, sz, is_expression, st->l+1);

			string_tokens_push(st, strdup(token), sz);
		}

		string_tokens_push(st, strdup("\n"), 1);
		is_expression = 0;
	}
}

string_tokens* amtail_lex(string *arg, char *name, amtail_log_level amtail_ll)
{
	if (amtail_ll.lexer > 0)
		printf("=======\nstart lexer\n========");
	file *a = readfile(arg->s);
	printf("'%s'\n", a->mem);

	string_tokens *st = string_tokens_new();

	strmbtok_amtail_lexer(a->mem, st, "/'\"", "/'\"", name, amtail_ll);

	releasefile(a);

	if (amtail_ll.lexer > 0)
		printf("=======\nend lexer\n========");
	return st;
}

static inline int string_eq(const char *a, size_t alen, const string *b)
{
	return alen == b->l && memcmp(a, b->s, alen) == 0;
}

void compare_mem_lines_with_tokens(char *mem, size_t mem_len, string_tokens *st)
{
	char   *line = mem;
	char   *end  = mem + mem_len;
	size_t  line_no = 0;

	for (char *p = mem; p <= end; ++p) {
		if (p == end || *p == '\n') {
			size_t len = p - line;

			if (len && line[len - 1] == '\r')
				len--;

			if (line_no < st->l) {
				string *tok = st->str[line_no];

				if (string_eq(line, len, tok)) {
					printf("OK[%lu]: '%s'(%zu) != '%s'\n", line_no+1, tok->s, tok->l, strndup(tok->s, len));
				} else {
					if (((len == 0) || (len == 1)) && (tok->l == 1) && (tok->s[0] == '\n') && (line[0] == '\n')) {
						puts("OK: nl");
					}
					else {
						printf("DONTOK[%lu]: '%s' != '%s' (%d:%zu:%d:%d:'%c')\n", line_no+1, tok->s, strndup(line, len), ((len == 0) || (len == 1)), tok->l, (tok->s[0] == '\n'), (line[0] == '\n'), line[0]);
					}
				}
			} else {
				if (strlen(line) > 0) {
					printf("too many strings: %s(%zu)\n", line, strlen(line));
				}
			}

			line = p + 1;
			line_no++;
		}
	}

	if (line_no < st->l) {
		puts("too many tokens");
	}
}
int amtail_lex_test(string *arg, char *name, amtail_log_level amtail_ll, string_tokens *st)
{
	if (amtail_ll.lexer > 0)
		printf("=======\nstart lex tester\n========");
	file *a = readfile(arg->s);
	if (!a)
		return 1;

	compare_mem_lines_with_tokens(a->mem, a->size, st);

	releasefile(a);

	if (amtail_ll.lexer > 0)
		printf("=======\nend lex tester\n========");
	return 1;
}
