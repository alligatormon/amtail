#pragma once

typedef struct amtail_log_level {
	uint8_t parser;
	uint8_t lexer;
	uint8_t compiler;
    uint8_t generator;
    uint8_t vm;
	uint8_t pcre;
} amtail_log_level;

