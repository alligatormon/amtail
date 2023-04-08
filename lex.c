#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "common/selector.h"
#include "sstring.h"
#include "log.h"
#include "file.h"
#define OPLEN 255

char *strmbtok(char *input, char **inout, uint64_t *size, char *delimit, char *openblock, char *closeblock, uint8_t *is_expression, amtail_log_level amtail_ll)
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

			if (amtail_ll.lexer > 1)
				printf("exit 0: '%s'\n", strndup(token-3, 3));

			break;
		}

		if (!iBlock && *token == '#') // need exit?
			token += strcspn(token, "\n");

		if (iBlock)
		{
			if ((closeblock[iBlockIndex] == *token) && (token[-1] != '\\') && (!quotas) && (!*is_expression))
				iBlock = 0;

			++token;
			continue;
		}
		if (((block = strchr(openblock, *token)) != NULL) && first &&  (!*is_expression))
		{
			if (amtail_ll.lexer > 1)
				printf("\ttoken '%c', token pre: '%c'\n", *token, token[-1]);

			iBlock = 1;
			iBlockIndex = block - openblock;
			++token;
			continue;
		}
		if (((block = strchr(openblock, *token)) != NULL) && !first && (token[-1] != '\\') && (!quotas) && (!*is_expression))
		{
			if (amtail_ll.lexer > 1)
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
				printf("exit 1: '%s'\n", strndup(token, 10));
			break;
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


void strmbtok_amtail_parser(char *inp, string_tokens *st, char *openblock, char *closeblock, char *name, amtail_log_level amtail_ll)
{
	char *ltoken, *token;
	uint64_t sz;

	uint8_t glob_expression = 0;
	while ((ltoken = strmbtok (inp, &inp, &sz, "\n", openblock, closeblock, &glob_expression, amtail_ll)) != NULL)
	{
		char *word = ltoken;
		uint8_t is_expression = 0;

		if (amtail_ll.lexer > 0)
			printf("\tTOK: '%s'\n", ltoken);

		while ((token = strmbtok(word, &word, &sz, " \t\n", openblock, closeblock, &is_expression, amtail_ll)))
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
				printf("FILE %s TOKEN SIZE: '%s' (%llu) expr: %d\n", name, token, sz, is_expression);

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

	string_tokens *st = string_tokens_new();
	strmbtok_amtail_parser(a->mem, st, "/", "/", name, amtail_ll);

	releasefile(a);

	if (amtail_ll.lexer > 0)
		printf("=======\nend lexer\n========");
	return st;
}
