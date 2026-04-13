#pragma once
//#include "sstring.h"
#include "common/selector.h"
#include "log.h"
#define ALLIGATOR_VARTYPE_COUNTER 0
#define ALLIGATOR_VARTYPE_GAUGE 1
#define ALLIGATOR_VARTYPE_TEXT 2
#define ALLIGATOR_VARTYPE_CONST 3
#define ALLIGATOR_VARTYPE_HISTOGRAM 4
#define ALLIGATOR_FACTTYPE_INT 0
#define ALLIGATOR_FACTTYPE_DOUBLE 1
#define ALLIGATOR_FACTTYPE_TEXT 2
#define AMTAIL_AST_LEFT 0
#define AMTAIL_AST_RIGHT 1

#define AMTAIL_AST_OPCODE_NOOP 0
#define AMTAIL_AST_OPCODE_VARIABLE 1 // create variable
#define AMTAIL_AST_OPCODE_BRANCH 2
#define AMTAIL_AST_OPCODE_INC 3
#define AMTAIL_AST_OPCODE_DEC 4
#define AMTAIL_AST_OPCODE_FUNC_DECLARATION 5
#define AMTAIL_AST_OPCODE_FUNC_CALL 6
#define AMTAIL_AST_OPCODE_FUNC_STOP 7
#define AMTAIL_AST_OPCODE_FUNC_MATCH 8
#define AMTAIL_AST_OPCODE_FUNC_CMP 9 // compare and set match register
//#define AMTAIL_AST_OPCODE_FUNC_JNM 10 // jump if no match
#define AMTAIL_AST_OPCODE_FUNC_JMP 10 // unconditional jump
#define AMTAIL_AST_OPCODE_ADD 11
#define AMTAIL_AST_OPCODE_SUB 12
#define AMTAIL_AST_OPCODE_MUL 13
#define AMTAIL_AST_OPCODE_DIV 14
#define AMTAIL_AST_OPCODE_MOD 15
#define AMTAIL_AST_OPCODE_POW 16
#define AMTAIL_AST_OPCODE_ASSIGN 17
#define AMTAIL_AST_OPCODE_ADD_ASSIGN 18 // +=
#define AMTAIL_AST_OPCODE_MATCH 19 // =~
#define AMTAIL_AST_OPCODE_NOTMATCH 20 // !~
#define AMTAIL_AST_OPCODE_AND 21
#define AMTAIL_AST_OPCODE_OR 22
#define AMTAIL_AST_OPCODE_LT 23
#define AMTAIL_AST_OPCODE_GT 24
#define AMTAIL_AST_OPCODE_LE 25
#define AMTAIL_AST_OPCODE_GE 26
#define AMTAIL_AST_OPCODE_EQ 27
#define AMTAIL_AST_OPCODE_NE 28
#define AMTAIL_AST_OPCODE_NOT 29 // !
#define AMTAIL_AST_OPCODE_FUNC_STRPTIME 30 // strptime
#define AMTAIL_AST_OPCODE_FUNC_TIMESTAMP 31 // timestamp
#define AMTAIL_AST_OPCODE_FUNC_TOLOWER 32 // tolower
#define AMTAIL_AST_OPCODE_FUNC_LEN 33 // len
#define AMTAIL_AST_OPCODE_FUNC_STRTOL 34 // strtol
#define AMTAIL_AST_OPCODE_FUNC_SETTIME 35 // settime
#define AMTAIL_AST_OPCODE_FUNC_GETFILENAME 36 // getfilename
#define AMTAIL_AST_OPCODE_FUNC_INT 37 // int
#define AMTAIL_AST_OPCODE_FUNC_BOOL 38 // bool
#define AMTAIL_AST_OPCODE_FUNC_FLOAT 39 // float
#define AMTAIL_AST_OPCODE_FUNC_STRING 40 // string
#define AMTAIL_AST_OPCODE_FUNC_SUBST 41 //subst
#define AMTAIL_AST_OPCODE_REGEX 42
#define AMTAIL_AST_OPCODE_VAR 43 // use variable
#define AMTAIL_AST_OPCODE_RUN 44 // expression is ended, should be calculate and assign

typedef struct amtail_ast {
	enum { gauge, counter } tag;
	string *name;
	string *export_name;
	string *by[256];
	string *bucket[256];
	uint8_t by_count;
	uint8_t bucket_count;
	uint8_t opcode;
	uint8_t vartype;
	uint8_t facttype;
	uint8_t hidden;
	union {
		double dvalue;
		int64_t ivalue;
		string *svalue;
	};
	struct amtail_ast **stem;

	struct amtail_ast *tail; //for stack
	struct amtail_ast *prev; //for stack
} amtail_ast;

typedef struct parser_state {
	uint8_t variable_by;
	uint8_t variable_bucket;
	uint8_t expression;
	uint8_t branch;
} parser_state;


typedef struct calculation_t {
	union {
		double dvalue;
		int64_t ivalue;
		string *svalue;
	};
    uint8_t vartype;
} calculation_t;

typedef struct calculation_cluster {
    calculation_t *queue;
    char *stack;
    uint64_t qcur;
    uint64_t scur;
    uint64_t qmax;
    uint64_t smax;
} calculation_cluster;

amtail_ast* amtail_parser(string_tokens *tokens, char *name, amtail_log_level amtail_ll);
void amtail_ast_free(amtail_ast *ast);
void amtail_ast_print(amtail_ast *ast, uint64_t indent);
char *opname_from_code(uint64_t opcode);
void amtail_parser_init();
