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

//typedef struct { 
//	int fd;
//	char *path;
//	int size;
//	char *mem;
//	struct stat st;
//} file;
//
//file *readfile(char *path)
//{
//	file *a = calloc(1, sizeof(file));
//	a->path = path;
//	a->fd = open(a->path,O_RDONLY);
//	if (a->fd < 0)
//		return 0;
//
//	fstat(a->fd, &a->st);
//	a->size=a->st.st_size;
//	a->mem=calloc(1, a->size);
//	read(a->fd, a->mem, a->size);
//
//	return a;
//}
//
//void releasefile(file *a)
//{
//	free(a->mem);
//	close(a->fd);
//	free(a);
//}

char *strmbtok(char *input, char **inout, uint64_t *size, char *delimit, char *openblock, char *closeblock)
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
			//printf("exit 0: '%s'\n", strndup(token-3, 3));
			break;
		}

		if (!iBlock && *token == '#') // need exit?
			token += strcspn(token, "\n");

		if (iBlock)
		{
			if ((closeblock[iBlockIndex] == *token) && (token[-1] != '\\') && (!quotas))
				iBlock = 0;

			++token;
			continue;
		}
		if (((block = strchr(openblock, *token)) != NULL) && first)
		{
			//printf("\ttoken '%c', token pre: '%c'\n", *token, token[-1]);
			iBlock = 1;
			iBlockIndex = block - openblock;
			++token;
			continue;
		}
		if (((block = strchr(openblock, *token)) != NULL) && !first && (token[-1] != '\\') && (!quotas))
		{
			//printf("\ttoken '%c', token pre: '%c'\n", *token, token[-1]);
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

			//printf("exit 1: '%s'\n", strndup(token, 10));
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


void strmbtok_amtail_parser(char *inp, string_tokens *st, char *openblock, char *closeblock, char *name)
{
	char *ltoken, *token;
	uint64_t sz;

	while ((ltoken = strmbtok (inp, &inp, &sz, "\n", openblock, closeblock)) != NULL)
	{
		char *word = ltoken;
		//printf("\tTOK: '%s'\n", ltoken);
		while ((token = strmbtok(word, &word, &sz, " \t", openblock, closeblock)))
		{
			// skip comment
			if (!strncmp(token, "#", 1))
				break;

			if (sz < 1)
			{
				//printf("NO SIZE: '%s' (%zu)\n", token, sz);
				continue;
			}

			//printf("FILE %s TOKEN SIZE: '%s' (%zu)\n", name, token, sz);
			string_tokens_push(st, strdup(token), sz);
		}
		string_tokens_push(st, strdup("\n"), 1);
	}
}

string_tokens* amtail_lex(string *arg, char *name, amtail_log_level amtail_ll)
{
	file *a = readfile(arg->s);

	string_tokens *st = string_tokens_new();
	strmbtok_amtail_parser(a->mem, st, "/", "/", name);

	releasefile(a);
	return st;
}
