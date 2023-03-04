#include "compile.h"
#include "log.h"
#include "sstring.h"
#include "common/selector.h"

amtail_bytecode* amtail_compile(char *name, string *str, amtail_log_level amtail_ll)
{
	string_tokens *tokens = amtail_lex(str, name, amtail_ll);
	if (!tokens)
	{
		if (amtail_ll.compiler > 0)
			printf("Lexical analyze error for script: %s\n", name);

		return 0;
	}

	amtail_ast *ast = amtail_parser(tokens, name, amtail_ll);
	if (!ast)
	{
		if (amtail_ll.compiler > 0 > 0)
			printf("Syntax analyze error for script: %s\n", name);

		string_tokens_free(tokens);
		return 0;
	}

	if (amtail_ll.compiler > 0)
		amtail_ast_print(ast, 0);

	amtail_bytecode *byte_code = amtail_code_generator(ast, amtail_ll);
	if (!byte_code)
	{
		if (amtail_ll.compiler > 0)
			printf("Generator error for script: %s\n", name);

		string_tokens_free(tokens);
		amtail_ast_free(ast);
		return 0;
	}


	string_tokens_free(tokens);
	amtail_ast_free(ast);

	return byte_code;
}
