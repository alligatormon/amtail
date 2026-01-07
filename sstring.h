#pragma once
typedef struct string_tokens {
	string **str;
	uint64_t l;
	uint64_t m;
} string_tokens;

uint8_t string_tokens_push(string_tokens *st, char *s, uint64_t l);
string_tokens *string_tokens_new();
void string_cat(string *str, char *strcat, size_t len);
void string_string_cat(string *str, string *src);
void string_free(string *str);
void string_tokens_free(string_tokens *st);
void string_string_copy(string *dst, string *src);

