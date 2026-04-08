#pragma once
#include "common/pcre_parser.h"
#include "common/selector.h"
regex_match* amtail_regex_compile(char *regexstring);
void amtail_regex_free(regex_match *rematch);
