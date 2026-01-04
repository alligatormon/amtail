#include <string.h>
#include <pcre.h>
#include "dstructures/tommy.h"
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <inttypes.h>
#include <sys/stat.h>
#include "common/selector.h"
//#include "sstring.h"
#define DATATYPE_INT 1
#define DATATYPE_UINT 2
#define DATATYPE_DOUBLE 3
#define u64 PRIu64
#define d64 PRId64

//void stlencat(stlen *str, char *str2, size_t len)
//{
//	//printf("CONCAT '%p'(SSIZE %zu) + '%s' (SIZE %zu)\n", str->s, strlen(str->s), str2, len);
//	strncat(str->s, str2, len);
//	str->l += len;
//}

char *ltrim(char *s)
{
	while(isspace(*s)) s++;
	return s;
}

char *rtrim(char *s)
{
	char* back = s + strlen(s);
	while(isspace(*--back));
	*(back+1) = '\0';
	return s;
}

char *trim(char *s)
{
	return rtrim(ltrim(s));
}

//void stlentext(stlen *str, char *str2)
//{
//	size_t len = strlen(str2) +1;
//	strlcpy(str->s, str2, len);
//	str->l += len;
//}

char *gettextfile(char *path, size_t *filesz)
{
	FILE *fd = fopen(path, "r");
	if (!fd)
		return NULL;

	fseek(fd, 0, SEEK_END);
	int64_t fdsize = ftell(fd)+1;
	rewind(fd);

	int32_t psize = 0;

	char *buf = malloc(fdsize+psize);
	size_t rc = fread(buf, 1, fdsize, fd);
	rc++;
	buf[rc] = 0;
	if (filesz)
		*filesz = rc-1;
	fclose(fd);

	return buf;
}

int64_t int_get_next(char *buf, size_t sz, char sep, uint64_t *cursor)
{
	for (; *cursor<sz; ++(*cursor))
	{
		for (; *cursor<sz && buf[*cursor]==sep; ++(*cursor));
		if (isdigit(buf[*cursor]) || buf[*cursor] == '-')
		{
			int64_t ret = atoll(buf+(*cursor));
			for (; *cursor<sz && (isdigit(buf[*cursor]) || buf[*cursor] == '-'); ++(*cursor));
			++(*cursor);

			return ret;
		}
	}
	return 0;
}

double double_get_next(char *buf, char *sep, uint64_t *cursor)
{
	uint64_t end = strcspn(buf + *cursor, sep);
	double ret = strtod(buf + *cursor, NULL);

	//printf("cursor %d, end %d, ret %lf, '%s'\n", *cursor, end, ret, buf + *cursor);
	*cursor += end;

	return ret;
}

//int64_t str_get_next(char *buf, char *ret, uint64_t ret_sz, char *sep, uint64_t *cursor)
//{
//	uint64_t end = strcspn(buf + *cursor, sep);
//	//printf("end is %"u64": '%s' (buf+%"u64"), find:('%s')\n", end, buf + *cursor, *cursor, sep);
//	uint64_t copysize = (end + 1) > ret_sz ? ret_sz : (end + 1);
//	//printf("copysize %"u64": (%d)\n", copysize, ((end + 1) > ret_sz));
//
//	strlcpy(ret, buf + *cursor, copysize);
//	(*cursor) += end;
//
//	return end;
//}

int64_t uint_get_next(char *buf, size_t sz, char sep, uint64_t *cursor)
{
	for (; *cursor<sz; ++(*cursor))
	{
		for (; *cursor<sz && buf[*cursor]==sep; ++(*cursor));
		if (isdigit(buf[*cursor]) || buf[*cursor] == '-')
		{
			int64_t ret = strtoull(buf+(*cursor), NULL, 10);
			for (; *cursor<sz && (isdigit(buf[*cursor]) || buf[*cursor] == '-'); ++(*cursor));
			++(*cursor);

			return ret;
		}
	}
	return 0;
}

int64_t getkvfile(char *file)
{
	char temp[20];
	FILE *fd = fopen(file, "r");
	if (!fd)
		return 0;

	if ( !fgets(temp, 20, fd) )
	{
		fclose(fd);
		return 0;
	}

	fclose(fd);

	return atoll(temp);
}

string* string_init(size_t max)
{
	++max;
	string *ret = malloc(sizeof(*ret));
	ret->m = max;
	ret->s = malloc(max);
	*ret->s = 0;
	ret->l = 0;
	//printf("alloc: %p\n", ret->s);

	return ret;
}

string* string_new()
{
	string *ret = malloc(sizeof(*ret));
	ret->m = 0;
	ret->s = NULL;
	ret->l = 0;

	return ret;
}

void string_new_size(string *str, size_t len)
{
	if (!str->m)
	{
		str->m = len + 1;
		str->s = malloc(str->m);
		str->l = 0;
		*str->s = 0;
		return;
	}
	uint64_t newsize = str->m*2+len;
	//printf("alloc %zu, copy %zu, str->m %zu, str->m*2 %zu, str->m*2+len %zu, len %zu\n", newsize, str->l+1, str->m, str->m*2, str->m*2+len, len);
	char *newstr_char = malloc(newsize);
	memcpy(newstr_char, str->s, str->l+1);
	char *oldstr_char = str->s;
	str->s = newstr_char;
	str->m = newsize;

	free(oldstr_char);
}

void string_null(string *str)
{
	if (str->s)
		*str->s = 0;
	str->l = 0;
}

void string_free(string *str)
{
	//printf("free str->s is '%s'/%p\n", str->s, str->s);
	if (str->s)
		free(str->s);
	free(str);
}

string* string_init_str(char *str, size_t max)
{
	string *ret = malloc(sizeof(*ret));
	ret->m = max;
	ret->s = str;
	*ret->s = 0;
	ret->l = 0;

	return ret;
}

string* string_init_add(char *str, size_t len, size_t max)
{
	string *ret = malloc(sizeof(*ret));
	ret->m = max;
	ret->s = str;
	ret->l = len;
	ret->s[len] = 0;

	return ret;
}


string* string_init_add_auto(char *str)
{
	string *ret = malloc(sizeof(*ret));
	ret->m = ret->l = strlen(str);
	ret->s = str;

	return ret;
}

string* string_init_dup(char *str)
{
	string *ret = malloc(sizeof(*ret));
	ret->m = ret->l = strlen(str);
	ret->s = strdup(str);

	return ret;
}

void string_cat(string *str, char *strcat, size_t len)
{
	size_t str_len = str->l;
	//printf("1212str_len=%zu\n", str_len);
	if(str_len+len >= str->m)
		string_new_size(str, len);

	size_t copy_size = len < (str->m - str_len) ? len : str_len;
	//printf("+++++ string cat (%zu) +++++++\n%s\n", len, strcat);
	//printf("string '%s'\nstr_len=%zu\nstrcat='%s'\ncopy_size=%zu\nlen=%zu\n", str->s, str_len, strcat, copy_size+1, len);
	memcpy(str->s+str_len, strcat, copy_size);
	str->l += copy_size;
	str->s[str->l] = 0;
}

void string_string_cat(string *str, string *src)
{
	size_t str_len = str->l;
	size_t src_len = src->l;
	if(str_len+src_len >= str->m)
		string_new_size(str, src_len);

	memcpy(str->s+str_len, src->s, src_len);
	str->l += src_len;
	str->s[str->l] = 0;
}


void string_string_copy(string *dst, string *src)
{
	size_t src_len = src->l;

    //if (!src_len)
    //    return;

	if (src_len > dst->m)
		string_new_size(dst, src_len);

	memcpy(dst->s, src->s, src_len);
	dst->l = src_len;
	dst->s[dst->l] = 0;
}

void string_merge(string *str, string *src)
{
	string_string_cat(str, src);
	string_free(src);
}

void string_uint(string *str, uint64_t u)
{
	size_t str_len = str->l;
	if(str_len+20 >= str->m)
		string_new_size(str, 20);

	char num[20];
	snprintf(num, 20, "%"u64, u);

	size_t len = strlen(num);
	size_t copy_size = len < (str->m - str_len) ? len : str_len;
	strlcpy(str->s+str_len, num, copy_size+1);
	str->l += copy_size;
}

void string_int(string *str, int64_t i)
{
	size_t str_len = str->l;
	if(str_len+20 >= str->m)
		string_new_size(str, 20);

	char num[20];
	snprintf(num, 20, "%"d64, i);

	size_t len = strlen(num);
	size_t copy_size = len < (str->m - str_len) ? len : str_len;
	strlcpy(str->s+str_len, num, copy_size+1);
	str->l += copy_size;
}

void string_double(string *str, double d)
{
	size_t str_len = str->l;
	if(str_len+20 >= str->m)
		string_new_size(str, 20);

	char num[20];
	snprintf(num, 20, "%lf", d);

	size_t len = strlen(num);
	size_t copy_size = len < (str->m - str_len) ? len : str_len;
	strlcpy(str->s+str_len, num, copy_size+1);
	str->l += copy_size;
}

void string_number(string *str, void* value, int8_t type)
{
	if (type == DATATYPE_INT)
		string_int(str, *(int64_t*)value);
	else if (type == DATATYPE_UINT)
		string_uint(str, *(uint64_t*)value);
	else if (type == DATATYPE_DOUBLE)
		string_double(str, *(double*)value);
}

string* string_init_alloc(char *str, size_t max)
{
	if (!max)
		max = strlen(str);
	string *ret = malloc(sizeof(*ret));
	ret->m = max;
	ret->s = malloc(max+1);
	memcpy(ret->s, str, max);
	ret->s[max] = 0;
	ret->l = max;

	return ret;
}

void string_cut(string *str, uint64_t offset, size_t len)
{
	//printf("================%"u64":%zu:%zu================\n", offset, len, str->l);
	//printf("'%s'\n", str->s + offset);
	//puts("--------------------------------");
	if (!str)
		return;

	//if (offset == str->l)
	//	return;

	if (!len)
		return;

	if (!str->l)
		return;

	memcpy(str->s + offset, str->s + offset + len, str->l - len - offset);
	str->l -= len;
	str->s[str->l] = 0;
	printf("'%s'\n", str->s + offset);
	puts("================================");
}

void string_vprintf(string *str, const char *fmt, va_list ap)
{
    if (!str || !fmt)
        return;

    va_list ap_copy;
    va_copy(ap_copy, ap);
    int needed = vsnprintf(NULL, 0, fmt, ap_copy);
    va_end(ap_copy);

    if (needed <= 0)
        return;

    if (str->l + (size_t)needed + 1 >= str->m) {

        size_t new_size = str->m ? str->m : 64;
        while (str->l + (size_t)needed + 1 >= new_size)
            new_size *= 2;

        string_new_size(str, new_size);
    }

    vsnprintf(str->s + str->l, str->m - str->l, fmt, ap);

    str->l += (size_t)needed;
    str->s[str->l] = '\0';
}

void string_sprintf(string *str, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    string_vprintf(str, fmt, ap);
    va_end(ap);
}

void string_break(string *str, uint64_t start, uint64_t end)
{
	uint64_t newend = end - start;
	//printf("1start is %"u64", end is %"u64", newend is %"u64", l is %zu, m is %zu\n", start, end, newend, str->l, str->m);
	if (!end)
		newend = str->l - start;
	//printf("2start is %"u64", end is %"u64", newend is %"u64", l is %zu, m is %zu\n", start, end, newend, str->l, str->m);

	if (start)
		memcpy(str->s, str->s + start, newend);
	str->l = newend;
	str->s[str->l] = 0;
}

void string_free_callback(char *data)
{
	string *str = (string*)data;
	string_free(str);
}

string_tokens *string_tokens_new()
{
	string_tokens *ret = calloc(1, sizeof(*ret));

	return ret;
}

void string_tokens_scale(string_tokens *st)
{
	if (!st)
		return;

	uint64_t max = st->m ? st->m * 2 : 2;
	//printf("SCALE to %"PRIu64"\n", max);
	
	string **new = calloc(1, sizeof(**new) * max);
	memcpy(new, st->str, st->l * sizeof(string));

	string **old = st->str;
	st->str = new;

	if (old)
		free(old);

	st->m = max;
}

uint8_t string_tokens_push(string_tokens *st, char *s, uint64_t l)
{
	if (!st)
		return  0;

	if (!st->m)
		string_tokens_scale(st);

	if (st->m <= st->l)
		string_tokens_scale(st);

	st->str[st->l] = string_init_add(s, l, l);

	++st->l;

	return 1;
}


void string_tokens_print(string_tokens *st)
{
    for (uint64_t i = 0; i < st->l; i++)
    {
        printf("token[%"PRIu64"]: '%s'\n", i, st->str[i]->s);
    }
}

void string_tokens_free(string_tokens *st)
{
	//printf("st->l size %"PRIu64"\n", st->l);
	for (uint64_t i = 0; i < st->l; i++)
	{
		//printf("string tokens free %"PRIu64" %p\n", i, st->str[i]);
		string_free(st->str[i]);
	}
	free(st->str);
	free(st);
}
