#include <stdio.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include "file.h"
#define OPLEN 255

file *readfile(char *path)
{
	file *a = calloc(1, sizeof(file));
	a->path = path;
	a->fd = open(a->path,O_RDONLY);
	if (a->fd < 0)
		return 0;

	fstat(a->fd, &a->st);
	a->size=a->st.st_size;
	a->mem=calloc(1, a->size);
	read(a->fd, a->mem, a->size);

	return a;
}

string_tokens *readlogfile(char *path)
{
	FILE *fd = fopen(path, "r");
	if (!fd)
		return 0;

    string_tokens *dt = string_tokens_new();
    char buf[10000];
    while (fgets(buf, 10000, fd))
    {
        string_tokens_push(dt, buf, 10000);
    }

	return dt;
}

void releasefile(file *a)
{
	free(a->mem);
	close(a->fd);
	free(a);
}
