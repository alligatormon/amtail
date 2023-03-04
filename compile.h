#pragma once
#include "common/selector.h"
#include "generator.h"
#include "parser.h"
#include "lex.h"

amtail_bytecode* amtail_compile(char *name, string *str, amtail_log_level amtail_ll);
