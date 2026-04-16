#pragma once
#include "common/pcre_parser.h"
#include "common/selector.h"
#include "log.h"
regex_match* amtail_regex_compile(char *regexstring);
void amtail_regex_free(regex_match *rematch);
uint8_t amtail_regex_exec(regex_match *rematch, char *regex_match_string, uint64_t regex_match_size, amtail_log_level amtail_ll);
