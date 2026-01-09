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
#define DEBUGBUFFERSIZE 255

char* lexdebugprint(char *debugbuffer, size_t debugsize, char *token, size_t size) {
	uint64_t i;
	uint64_t j;
	for (i = 0, j = 0; i < size; ++i, ++j) {
		if (token[j] == '\n') {
			if (int_min(debugsize, size) == size) {
				int ret = strlcpy(debugbuffer+i, " %NEXTLINE%:", size);
				i += ret - 1;
				size += ret - 1;
				size = int_min(debugsize, size);
				debugsize -= ret - 1;
			}
		} else if (isprint(token[j])) {
			debugbuffer[i] = token[j];
			--debugsize;
		}
	}
	debugbuffer[i] = 0;
	return debugbuffer;
}

char *strmbtok(char *start, char *input, char **inout, uint64_t *size, char *delimit, char *openblock, char *closeblock, uint8_t *is_expression, int8_t *bracket, int8_t *squarebracket, amtail_log_level amtail_ll)
{
	*size = 0;
	char *token = input;
	char *lead = input;
	char *block = NULL;
	int iBlock = 0;
	int iBlockIndex = 0;
	int regexBlock = 0;
	char quota;
	uint8_t quotas = 0;
	char debugbuffer[DEBUGBUFFERSIZE];

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

		if (amtail_ll.lexer > 1)
			printf("> token: '%s', isexpression? %hhu\n", lexdebugprint(debugbuffer, DEBUGBUFFERSIZE, token, 20), *is_expression);

		if (!*token)
		{
			*size = *inout - input;

			if (amtail_ll.lexer > 1 && (token >= (start + 3)))
				printf("\t> no token, end: '%s'\n", lexdebugprint(debugbuffer, DEBUGBUFFERSIZE, token, 20));

			break;
		}

		if (!iBlock && !regexBlock && *token == '#') {
			token += strcspn(token, "\n");

			if (amtail_ll.lexer > 1)
				printf("> shifted: '%s'\n", lexdebugprint(debugbuffer, DEBUGBUFFERSIZE, token, 20));
		}

		if (iBlock)
		{
			//if ((closeblock[iBlockIndex] == *token) && (token > start) && (token[-1] != '\\') && (!quotas) && (!*is_expression))
			if ((closeblock[iBlockIndex] == *token) && (token > start) && (token[-1] != '\\') && (!quotas)) {
				if (amtail_ll.lexer > 1)
					printf("\t<<end quotas block '%s'\n", lexdebugprint(debugbuffer, DEBUGBUFFERSIZE, token, 20));

				iBlock = 0;
			}
			else
				if (amtail_ll.lexer > 1)
					printf("\t<<iBlock quotas continues '%s'\n", lexdebugprint(debugbuffer, DEBUGBUFFERSIZE, token, 20));

			++token;
			continue;
		}

		if (regexBlock)
		{
			if ((*token == '/') && (token > start) && (token[-1] != '\\') && (!quotas)) {
				if (amtail_ll.lexer > 1)
					printf("\t<<end regexp block '%s'\n", lexdebugprint(debugbuffer, DEBUGBUFFERSIZE, token, 20));

				regexBlock = 0;
			}
			else
				if (amtail_ll.lexer > 1)
					printf("\t<<regexBlock regexp continues '%s'\n", lexdebugprint(debugbuffer, DEBUGBUFFERSIZE, token, 20));

			++token;
			continue;
		}

		//if (((block = strchr(openblock, *token)) != NULL) && first &&  (!*is_expression))
		if (((block = strchr(openblock, *token)) != NULL) && first)
		{
			if (amtail_ll.lexer > 1)
				printf("\t<<start open1 block '%s'\n", lexdebugprint(debugbuffer, DEBUGBUFFERSIZE, token, 20));

			iBlock = 1;
			iBlockIndex = block - openblock;
			++token;
			continue;
		}
		//if (((block = strchr(openblock, *token)) != NULL) && !first && (token[-1] != '\\') && (!quotas) && (!*is_expression))
		if (((block = strchr(openblock, *token)) != NULL) && !first && (token[-1] != '\\') && (!quotas))
		{
			if (amtail_ll.lexer > 1)
				printf("\t<<start open2 block '%s'\n", lexdebugprint(debugbuffer, DEBUGBUFFERSIZE, token, 20));

			iBlock = 1;
			iBlockIndex = block - openblock;
			++token;
			continue;
		}

		if ((*token == '/') && first && (!*is_expression))
		{
			if (amtail_ll.lexer > 1)
				printf("\t>>find regexp block '%s'\n", lexdebugprint(debugbuffer, DEBUGBUFFERSIZE, token, 10));

			regexBlock = 1;
			++token;
			continue;
		}
		if ((*token == '/') && !first && (token[-1] != '\\') && (!quotas) && (!*is_expression))
		{
			if (amtail_ll.lexer > 1)
				printf("\t>>find regexp block '%s', is expression: %d\n", lexdebugprint(debugbuffer, DEBUGBUFFERSIZE, token, 10), *is_expression);

			regexBlock = 1;
			++token;
			continue;
		}

		if (strchr(delimit, *token) != NULL && !iBlock) {
			*token = '\0';
			*size = *inout - input;

			++token;
			*inout = token;

			if (amtail_ll.lexer > 1)
				printf("\t>>delimiter exit 1: '%s'\n", lexdebugprint(debugbuffer, DEBUGBUFFERSIZE, token, 15));
			break;
		}

		if (*token == '(' && *bracket >= 0 && !iBlock) {
			*size = *inout - input;

			if (amtail_ll.lexer > 1)
				printf("\t>>find quota block (%lluu) '%s'\n", *size, lexdebugprint(debugbuffer, DEBUGBUFFERSIZE, token, 10));

			if (!*size) {
				*size = 1;
				++token;
				*inout = token;
				++(*bracket);
			}

			break;
		}

		if (*token == ')' && !iBlock && *bracket >= 1) {
			*size = *inout - input;

			if (amtail_ll.lexer > 1)
				printf("\t>>end bracket block (%llu) '%s'\n", *size, lexdebugprint(debugbuffer, DEBUGBUFFERSIZE, token, 10));

			if (!*size) {
				*size = 1;
				++token;
				*inout = token;
				--(*bracket);
			}

			break;
		}

		if (*token == '[' && *squarebracket >= 0 && !iBlock) {
			*size = *inout - input;

			if (amtail_ll.lexer > 1)
				printf("\t>>find square block (%llu) '%s'\n", *size, lexdebugprint(debugbuffer, DEBUGBUFFERSIZE, token, 10));

			if (!*size) {
				*size = 1;
				++token;
				*inout = token;
				++(*squarebracket);
			}

			break;
		}

		if (*token == ']' && !iBlock && *squarebracket >= 1) {
			*size = *inout - input;

			if (amtail_ll.lexer > 1)
				printf("\t>>end square block (%llu) '%s'\n", *size, lexdebugprint(debugbuffer, DEBUGBUFFERSIZE, token, 10));

			if (!*size) {
				*size = 1;
				++token;
				*inout = token;
				--(*squarebracket);
			}

			break;
		}

		if ((*token == '=') && (token[1] != '~') && !iBlock) {
			if (amtail_ll.lexer > 1)
				printf("\t>>find expression block '%s'\n", lexdebugprint(debugbuffer, DEBUGBUFFERSIZE, token, 10));

			*is_expression = 1;
		}

		if (*token == '"' || *token == '\'')
		{
			if (amtail_ll.lexer > 1)
				printf("\t>>find quota '%c', quotas: %d, strings '%s'\n", *token, quotas, lexdebugprint(debugbuffer, DEBUGBUFFERSIZE, token, 10));

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
		if (amtail_ll.lexer > 1)
			printf("\t>> skip, next token\n");
		first = 0;
	}
	return lead;
}


void strmbtok_amtail_lexer(char *inp, string_tokens *st, char *openblock, char *closeblock, char *name, amtail_log_level amtail_ll)
{
	char *start = inp;
	char *ltoken;
	uint64_t sz;

	uint8_t glob_expression = 0;
	int8_t glob_bracket = -1;
	int8_t glob_squarebracket = -1;

	int amtail_lexer_log = amtail_ll.lexer;
	amtail_ll.lexer = 0;

	while ((ltoken = strmbtok (start, inp, &inp, &sz, "\n", openblock, closeblock, &glob_expression, &glob_bracket, &glob_squarebracket, amtail_ll)) != NULL)
	{
		char *word = ltoken;
		uint8_t is_expression = 0;
		int8_t is_bracket = 0;
		int8_t is_squarebracket = 0;
		char *token;

		if (amtail_ll.lexer > 0)
			printf("\tTOK: '%s'\n", ltoken);

		amtail_ll.lexer = amtail_lexer_log;
		while ((token = strmbtok(start, word, &word, &sz, " \t\n,", openblock, closeblock, &is_expression, &is_bracket, &is_squarebracket, amtail_ll)))
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

			//if (*token == '=' && sz == 1)
			//	is_expression = 1;

			if (amtail_ll.lexer > 0)
				printf("FILE %s TOKEN SIZE: '%s' (%llu) expr: %d, index %llu\n", name, token, sz, is_expression, st->l+1);

			string_tokens_push(st, strdup(token), sz);
		}

		string_tokens_push(st, strdup("\n"), 1);
		is_expression = 0;
		glob_expression = 0;

		amtail_ll.lexer = 0;
	}
}

string_tokens* amtail_lex(string *arg, char *name, amtail_log_level amtail_ll)
{
	if (amtail_ll.lexer > 0)
		printf("=======\nstart lexer\n========");
	file *a = readfile(arg->s);
	printf("'%s'\n", a->mem);

	string_tokens *st = string_tokens_new();

	//strmbtok_amtail_lexer(a->mem, st, "/'\"", "/'\"", name, amtail_ll);
	strmbtok_amtail_lexer(a->mem, st, "'\"", "'\"", name, amtail_ll);

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
		printf("too many tokens: lines in file %zu, tokens: %llu\n", line_no, st->l);
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
