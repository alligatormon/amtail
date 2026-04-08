#include <stdio.h>
#include <stdlib.h>
#include "common/selector.h"
#include "compile.h"
#include "log.h"
#include "file.h"
#include "variables.h"
#include "vm.h"
#include "lex.h"
#include "parser.h"

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
	/* TODO: replace with recursive AST destructor after parser stabilizes. */
	/* amtail_ast_free(ast); */
	string_tokens_free(tokens);
	string_free(src);
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
		return parser_test_file(argv[1], amtail_ll);

	int failed = 0;
	size_t script_count = sizeof(scripts) / sizeof(scripts[0]);
	for (size_t i = 0; i < script_count; ++i)
		failed += parser_test_file(scripts[i], amtail_ll);

	printf("\nParser tests: %zu total, %d failed, %zu passed\n", script_count, failed, script_count - (size_t)failed);
	return failed ? 1 : 0;
}
