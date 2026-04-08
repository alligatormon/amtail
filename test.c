#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "common/selector.h"
#include "compile.h"
#include "log.h"
#include "file.h"
#include "variables.h"
#include "vm.h"
#include "lex.h"
#include "parser.h"
#include "generator.h"

void run_tests(char *dir, char *file, string *logline)
{
	string *str = string_init_dup(dir);
	amtail_log_level amtail_ll = {
		.parser = 0,
		.lexer = 0,
		.compiler = 0,
	};
	amtail_bytecode* byte_code = amtail_compile(file, str, amtail_ll);
	amtail_run(byte_code, logline);
	amtail_code_free(byte_code);
}

void run_tests_file(char *dir, char *file, char *log_filename)
{
	string *str = string_init_dup(dir);
	amtail_log_level amtail_ll = {
		.parser = 2,
		.lexer = 0,
		.generator = 0,
		.compiler = 0,
	};
	amtail_bytecode* byte_code = amtail_compile(file, str, amtail_ll);
	if (!byte_code)
	{
		printf("byte_code doest not ready, exit program\n");
		return;
		//exit(1);
	}

	struct file *file_log = readfile(log_filename);
	if (!file_log || !file_log->mem) {
		printf("cannot read logfile: %s\n", log_filename);
		return;
	}
	string *logline = string_init_add(file_log->mem, file_log->size, file_log->size);
	//string_tokens *logline = readlogfile(log_filename);

	amtail_bytecode_dump(byte_code);
	exit(0);
	amtail_run(byte_code, logline);

	amtail_variables_dump(byte_code->variables);

	amtail_code_free(byte_code);

	releasefile(file_log);
	//string_tokens_free(logline);
}

static int parser_test_file(const char *script_path, amtail_log_level amtail_ll)
{
	printf("[RUN] %s\n", script_path);

	string *src = string_init_dup((char*)script_path);
	string_tokens *tokens = amtail_lex(src, (char*)script_path, amtail_ll);
	if (!tokens)
	{
		printf("[FAIL] lex: %s\n", script_path);
		string_free(src);
		return 1;
	}

	amtail_ast *ast = amtail_parser(tokens, (char*)script_path, amtail_ll);
	if (!ast)
	{
		printf("[FAIL] parser: %s\n", script_path);
		string_tokens_free(tokens);
		string_free(src);
		return 1;
	}

	printf("[OK] %s\n", script_path);
	amtail_ast_free(ast);
	string_tokens_free(tokens);
	string_free(src);
	return 0;
}

static int token_equals(string *tok, const char *lit)
{
	return tok && tok->s && !strcmp(tok->s, lit);
}

static int token_has_regex_shape(string *tok)
{
	return tok && tok->s && tok->l >= 2 && tok->s[0] == '/' && tok->s[tok->l - 1] == '/';
}

static int generator_validate_bytecode(const char *script_path, string_tokens *tokens, amtail_bytecode *byte_code)
{
	if (!byte_code)
	{
		printf("[FAIL] generator null bytecode: %s\n", script_path);
		return 1;
	}

	if (!byte_code->ops || !byte_code->l)
	{
		printf("[FAIL] generator empty bytecode: %s\n", script_path);
		return 1;
	}

	int has_decl = 0;
	int has_assignment = 0;
	int has_regex = 0;
	for (uint64_t i = 0; tokens && i < tokens->l; ++i)
	{
		string *tok = tokens->str[i];
		if (token_equals(tok, "counter") || token_equals(tok, "gauge") ||
		    token_equals(tok, "histogram") || token_equals(tok, "const"))
			has_decl = 1;
		if (token_equals(tok, "=") || token_equals(tok, "+=") || token_equals(tok, "-=") ||
		    token_equals(tok, "*=") || token_equals(tok, "/=") || token_equals(tok, "++") ||
		    token_equals(tok, "--"))
			has_assignment = 1;
		if (token_has_regex_shape(tok))
			has_regex = 1;
	}

	int seen_decl = 0;
	int seen_assign = 0;
	int seen_run = 0;
	int seen_regexish = 0;

	for (uint64_t i = 0; i < byte_code->l; ++i)
	{
		amtail_byteop *op = &byte_code->ops[i];

		if (op->by_count && !op->by)
		{
			printf("[FAIL] by_count without by labels: %s [%"PRIu64"]\n", script_path, i);
			return 1;
		}

		if (op->bucket_count && !op->bucket)
		{
			printf("[FAIL] bucket_count without bucket labels: %s [%"PRIu64"]\n", script_path, i);
			return 1;
		}

		if (op->right_opcounter >= byte_code->l && op->right_opcounter != 0)
		{
			printf("[FAIL] invalid jump target: %s [%"PRIu64"] -> %"PRIu64"/%"PRIu64"\n",
			       script_path, i, op->right_opcounter, byte_code->l);
			return 1;
		}

		if (op->opcode == AMTAIL_AST_OPCODE_VARIABLE)
			seen_decl = 1;
		else if (op->opcode == AMTAIL_AST_OPCODE_ASSIGN)
			seen_assign = 1;
		else if (op->opcode == AMTAIL_AST_OPCODE_RUN)
			seen_run = 1;
		else if (op->opcode == AMTAIL_AST_OPCODE_BRANCH ||
		         op->opcode == AMTAIL_AST_OPCODE_MATCH ||
		         op->opcode == AMTAIL_AST_OPCODE_NOTMATCH)
			seen_regexish = 1;
	}

	if (has_decl && !seen_decl)
	{
		printf("[FAIL] generator missing variable declarations: %s\n", script_path);
		return 1;
	}

	if (has_assignment && (!seen_assign || !seen_run))
	{
		printf("[FAIL] generator missing assign/run ops: %s (assign=%d run=%d)\n",
		       script_path, seen_assign, seen_run);
		return 1;
	}

	if (has_regex && !seen_regexish)
	{
		printf("[FAIL] generator missing regex-related opcodes: %s\n", script_path);
		return 1;
	}

	return 0;
}

static int generator_test_file(const char *script_path, amtail_log_level amtail_ll)
{
	printf("[RUN][GEN] %s\n", script_path);
	fflush(stdout);

	string *src = string_init_dup((char*)script_path);
	string_tokens *tokens = amtail_lex(src, (char*)script_path, amtail_ll);
	if (!tokens)
	{
		printf("[FAIL][GEN] lex: %s\n", script_path);
		string_free(src);
		return 1;
	}

	amtail_ast *ast = amtail_parser(tokens, (char*)script_path, amtail_ll);
	if (!ast)
	{
		printf("[FAIL][GEN] parser: %s\n", script_path);
		string_tokens_free(tokens);
		string_free(src);
		return 1;
	}

	amtail_bytecode *byte_code = amtail_code_generator(ast, amtail_ll);
	if (!byte_code)
	{
		printf("[FAIL] compile/generator: %s\n", script_path);
		/* amtail_ast_free(ast); */
		string_tokens_free(tokens);
		string_free(src);
		return 1;
	}

	int rc = generator_validate_bytecode(script_path, tokens, byte_code);
	if (rc)
	{
		amtail_code_free(byte_code);
		amtail_ast_free(ast);
		string_tokens_free(tokens);
		string_free(src);
		return rc;
	}

	printf("[OK][GEN] %s\n", script_path);
	amtail_code_free(byte_code);
	amtail_ast_free(ast);
	string_tokens_free(tokens);
	string_free(src);
	return 0;
}

static int generator_test_file_subprocess(const char *script_path, amtail_log_level amtail_ll)
{
	fflush(NULL);
	pid_t pid = fork();
	if (pid < 0)
	{
		printf("[FAIL][GEN] fork failed: %s\n", script_path);
		return 1;
	}

	if (pid == 0)
	{
		int rc = generator_test_file(script_path, amtail_ll);
		_exit(rc ? 1 : 0);
	}

	int status = 0;
	if (waitpid(pid, &status, 0) < 0)
	{
		printf("[FAIL][GEN] waitpid failed: %s\n", script_path);
		return 1;
	}

	if (WIFSIGNALED(status))
	{
		printf("[FAIL][GEN] crashed with signal %d: %s\n", WTERMSIG(status), script_path);
		return 1;
	}

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		printf("[FAIL][GEN] failed: %s\n", script_path);
		return 1;
	}

	return 0;
}

int main(int argc, char **argv)
{
	amtail_parser_init();
	amtail_vm_init();

	amtail_log_level amtail_ll = {
		.parser = 0,
		.lexer = 0,
		.generator = 0,
		.compiler = 0,
	};

	const char *scripts[] = {
		"tests/apache_combined.mtail",
		"tests/apache_common.mtail",
		"tests/apache_metrics.mtail",
		"tests/dhcpd.mtail",
		"tests/histogram.mtail",
		"tests/lighttpd.mtail",
		"tests/linecount.mtail",
		"tests/mysql_slowqueries.mtail",
		"tests/nginx.mtail",
		"tests/nocode.mtail",
		"tests/ntpd.mtail",
		"tests/ntpd_peerstats.mtail",
		"tests/postfix.mtail",
		"tests/postfix2.mtail",
		"tests/postfix3.mtail",
		"tests/rails.mtail",
		"tests/rsyncd.mtail",
		"tests/sftp.mtail",
		"tests/timer.mtail",
		"tests/vsftpd.mtail",
	};

	if (argc > 1)
	{
		if (!strcmp(argv[1], "--generator"))
		{
			if (argc > 2)
				return generator_test_file_subprocess(argv[2], amtail_ll);

			int failed = 0;
			size_t script_count = sizeof(scripts) / sizeof(scripts[0]);
			for (size_t i = 0; i < script_count; ++i)
				failed += generator_test_file_subprocess(scripts[i], amtail_ll);

			printf("\nGenerator tests: %zu total, %d failed, %zu passed\n", script_count, failed, script_count - (size_t)failed);
			return failed ? 1 : 0;
		}
		if (!strcmp(argv[1], "--generator-direct"))
		{
			if (argc > 2)
			{
				amtail_ll.generator = 2;
				return generator_test_file(argv[2], amtail_ll);
			}
			return 1;
		}

		return parser_test_file(argv[1], amtail_ll);
	}

	int failed = 0;
	size_t script_count = sizeof(scripts) / sizeof(scripts[0]);
	for (size_t i = 0; i < script_count; ++i)
		failed += parser_test_file(scripts[i], amtail_ll);

	printf("\nParser tests: %zu total, %d failed, %zu passed\n", script_count, failed, script_count - (size_t)failed);
	if (failed)
		return 1;

	int failed_generator = 0;
	for (size_t i = 0; i < script_count; ++i)
		failed_generator += generator_test_file_subprocess(scripts[i], amtail_ll);

	printf("\nGenerator tests: %zu total, %d failed, %zu passed\n", script_count, failed_generator, script_count - (size_t)failed_generator);
	return failed_generator ? 1 : 0;
}
