#pragma once
#include <stdio.h>
#include <sys/stat.h>
#include "common/selector.h"
//#include "sstring.h"

typedef struct file { 
	int fd;
	char *path;
	size_t size;
	char *mem;
	struct stat st;
} file;


file *readfile(char *path);
void releasefile(file *a);
string_tokens *readlogfile(char *path);
