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
#include "amtail_pcre.h"

void run_tests(char *dir, char *file, string *logline)
{
	string *str = string_init_dup(dir);
	amtail_log_level amtail_ll = {
		.parser = 0,
		.lexer = 0,
		.compiler = 0,
		.pcre = 3,
	};
	amtail_bytecode* byte_code = amtail_compile(file, str, amtail_ll);
	amtail_run(byte_code, logline, amtail_ll);
	amtail_code_free(byte_code);
}

void run_tests_file(char *dir, char *file, char *log_filename)
{
	(void)dir;
	string *str = string_init_dup(file);
	amtail_log_level amtail_ll = {
		.parser = 2,
		.lexer = 0,
		.generator = 0,
		.compiler = 0,
		.pcre = 3,
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
	string *logline = string_init_dup(file_log->mem);
	//string_tokens *logline = readlogfile(log_filename);

	amtail_bytecode_dump(byte_code);
	amtail_run(byte_code, logline, amtail_ll);

	amtail_variables_dump(byte_code->variables);

	amtail_code_free(byte_code);

	string_free(logline);
	string_free(str);
	releasefile(file_log);
	//string_tokens_free(logline);
}

static int run_mtail_script_with_log(const char *script_path, const char *log_path)
{
	FILE *logf = fopen(log_path, "r");
	if (!logf)
	{
		printf("[FAIL][RUN] cannot read log: %s\n", log_path);
		return 1;
	}

	amtail_log_level amtail_ll = {
		.parser = 0,
		.lexer = 0,
		.generator = 0,
		.compiler = 0,
		.pcre = 1,
	};

	string *script = string_init_dup((char*)script_path);
	string_tokens *tokens = amtail_lex(script, (char*)script_path, amtail_ll);
	if (!tokens)
	{
		printf("[FAIL][RUN] lex failed: %s\n", script_path);
		string_free(script);
		fclose(logf);
		return 1;
	}
	amtail_ast *ast = amtail_parser(tokens, (char*)script_path, amtail_ll);
	if (!ast)
	{
		printf("[FAIL][RUN] parser failed: %s\n", script_path);
		string_tokens_free(tokens);
		string_free(script);
		fclose(logf);
		return 1;
	}
	amtail_bytecode *byte_code = amtail_code_generator(ast, amtail_ll);
	if (!byte_code)
	{
		printf("[FAIL][RUN] generator failed: %s\n", script_path);
		amtail_ast_free(ast);
		string_tokens_free(tokens);
		string_free(script);
		fclose(logf);
		return 1;
	}

	int rc = 1;
	char *linebuf = NULL;
	size_t linecap = 0;
	ssize_t linelen = 0;
	while ((linelen = getline(&linebuf, &linecap, logf)) != -1)
	{
		string *line = string_init_alloc(linebuf, (uint64_t)linelen);
		int line_rc = amtail_run(byte_code, line, amtail_ll);
		string_free(line);
		if (!line_rc)
		{
			rc = 0;
			break;
		}
	}
	free(linebuf);
	fclose(logf);

	printf("[RUN] script=%s log=%s rc=%d\n", script_path, log_path, rc);
	amtail_variables_dump(byte_code->variables);

	amtail_code_free(byte_code);
	amtail_ast_free(ast);
	string_tokens_free(tokens);
	string_free(script);
	return rc ? 0 : 1;
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

static void runtime_var_free_cb(void *arg)
{
	amtail_variable *var = arg;
	if (!var)
		return;

	if (var->export_name)
		string_free(var->export_name);
	if (var->key)
		free(var->key);
	if ((var->type == ALLIGATOR_VARTYPE_TEXT || var->type == ALLIGATOR_VARTYPE_CONST) && var->s)
		string_free(var->s);
	if (var->by_positions)
		free(var->by_positions);
	free(var);
}

static amtail_bytecode* runtime_bc_new(size_t op_count)
{
	amtail_bytecode *bc = calloc(1, sizeof(*bc));
	bc->ops = calloc(op_count, sizeof(*bc->ops));
	bc->m = op_count;
	bc->l = op_count;
	bc->variables = alligator_ht_init(NULL);
	return bc;
}

static void runtime_bc_free(amtail_bytecode *bc)
{
	if (!bc)
		return;
	for (uint64_t i = 0; i < bc->l; ++i)
	{
		if (bc->ops[i].export_name)
			string_free(bc->ops[i].export_name);
		if (bc->ops[i].re_match)
			amtail_regex_free(bc->ops[i].re_match);
	}
	if (bc->variables)
	{
		alligator_ht_forfree(bc->variables, runtime_var_free_cb);
		free(bc->variables);
	}
	free(bc->ops);
	free(bc);
}

static void runtime_insert_text(alligator_ht *variables, const char *name, const char *value)
{
	amtail_variable *var = calloc(1, sizeof(*var));
	var->type = ALLIGATOR_VARTYPE_TEXT;
	var->key = strdup(name);
	var->export_name = string_init_dup((char*)name);
	var->s = string_init_dup((char*)value);
	alligator_ht_insert(variables, &var->node, var, amtail_hash(var->key, strlen(var->key)));
}

static int runtime_expect_counter(alligator_ht *variables, const char *name, int64_t expect)
{
	amtail_variable *var = alligator_ht_search(variables, amtail_variable_compare, name, amtail_hash((char*)name, strlen(name)));
	return var && var->type == ALLIGATOR_VARTYPE_COUNTER && var->i == expect;
}

static int runtime_expect_gauge_positive(alligator_ht *variables, const char *name)
{
	amtail_variable *var = alligator_ht_search(variables, amtail_variable_compare, name, amtail_hash((char*)name, strlen(name)));
	return var && var->type == ALLIGATOR_VARTYPE_GAUGE && var->d > 0;
}

static int vm_runtime_test_timestamp(void)
{
	amtail_log_level amtail_ll = {0};
	amtail_bytecode *bc = runtime_bc_new(4);
	bc->ops[0].opcode = AMTAIL_AST_OPCODE_VARIABLE;
	bc->ops[0].vartype = ALLIGATOR_VARTYPE_GAUGE;
	bc->ops[0].export_name = string_init_dup("ts");
	bc->ops[1].opcode = AMTAIL_AST_OPCODE_ASSIGN;
	bc->ops[1].export_name = string_init_dup("ts");
	bc->ops[2].opcode = AMTAIL_AST_OPCODE_FUNC_TIMESTAMP;
	bc->ops[3].opcode = AMTAIL_AST_OPCODE_RUN;
	string *line = string_init_dup("x\n");
	int rc = amtail_run(bc, line, amtail_ll);
	int ok = rc && runtime_expect_gauge_positive(bc->variables, "ts");
	string_free(line);
	runtime_bc_free(bc);
	return ok;
}

static int vm_runtime_test_len_strtol(void)
{
	amtail_log_level amtail_ll = {0};
	int ok = 1;

	/* len("Hello") -> 5 */
	{
		amtail_bytecode *bc = runtime_bc_new(5);
		bc->ops[0].opcode = AMTAIL_AST_OPCODE_VARIABLE;
		bc->ops[0].vartype = ALLIGATOR_VARTYPE_COUNTER;
		bc->ops[0].export_name = string_init_dup("lenv");
		bc->ops[1].opcode = AMTAIL_AST_OPCODE_ASSIGN;
		bc->ops[1].export_name = string_init_dup("lenv");
		bc->ops[2].opcode = AMTAIL_AST_OPCODE_VAR;
		bc->ops[2].export_name = string_init_dup("msg");
		bc->ops[3].opcode = AMTAIL_AST_OPCODE_FUNC_LEN;
		bc->ops[4].opcode = AMTAIL_AST_OPCODE_RUN;
		runtime_insert_text(bc->variables, "msg", "Hello");
		string *line = string_init_dup("x\n");
		ok = ok && amtail_run(bc, line, amtail_ll) && runtime_expect_counter(bc->variables, "lenv", 5);
		string_free(line);
		runtime_bc_free(bc);
	}

	/* strtol("42") -> 42 */
	{
		amtail_bytecode *bc = runtime_bc_new(5);
		bc->ops[0].opcode = AMTAIL_AST_OPCODE_VARIABLE;
		bc->ops[0].vartype = ALLIGATOR_VARTYPE_COUNTER;
		bc->ops[0].export_name = string_init_dup("ival");
		bc->ops[1].opcode = AMTAIL_AST_OPCODE_ASSIGN;
		bc->ops[1].export_name = string_init_dup("ival");
		bc->ops[2].opcode = AMTAIL_AST_OPCODE_VAR;
		bc->ops[2].export_name = string_init_dup("numtxt");
		bc->ops[3].opcode = AMTAIL_AST_OPCODE_FUNC_STRTOL;
		bc->ops[4].opcode = AMTAIL_AST_OPCODE_RUN;
		runtime_insert_text(bc->variables, "numtxt", "42");
		string *line = string_init_dup("x\n");
		ok = ok && amtail_run(bc, line, amtail_ll) && runtime_expect_counter(bc->variables, "ival", 42);
		string_free(line);
		runtime_bc_free(bc);
	}

	return ok;
}

static int vm_runtime_test_strptime_and_match(void)
{
	amtail_log_level amtail_ll = {0};
	int ok = 1;

	/* strptime("2024-01-02 03:04:05") -> epoch > 0 */
	{
		amtail_bytecode *bc = runtime_bc_new(5);
		bc->ops[0].opcode = AMTAIL_AST_OPCODE_VARIABLE;
		bc->ops[0].vartype = ALLIGATOR_VARTYPE_GAUGE;
		bc->ops[0].export_name = string_init_dup("parsed");
		bc->ops[1].opcode = AMTAIL_AST_OPCODE_ASSIGN;
		bc->ops[1].export_name = string_init_dup("parsed");
		bc->ops[2].opcode = AMTAIL_AST_OPCODE_VAR;
		bc->ops[2].export_name = string_init_dup("datestr");
		bc->ops[3].opcode = AMTAIL_AST_OPCODE_FUNC_STRPTIME;
		bc->ops[4].opcode = AMTAIL_AST_OPCODE_RUN;
		runtime_insert_text(bc->variables, "datestr", "2024-01-02 03:04:05");
		string *line = string_init_dup("x\n");
		ok = ok && amtail_run(bc, line, amtail_ll) && runtime_expect_gauge_positive(bc->variables, "parsed");
		string_free(line);
		runtime_bc_free(bc);
	}

	/* match/notmatch */
	{
		amtail_bytecode *bc = runtime_bc_new(9);
		bc->ops[0].opcode = AMTAIL_AST_OPCODE_VARIABLE;
		bc->ops[0].vartype = ALLIGATOR_VARTYPE_COUNTER;
		bc->ops[0].export_name = string_init_dup("m");
		bc->ops[1].opcode = AMTAIL_AST_OPCODE_ASSIGN;
		bc->ops[1].export_name = string_init_dup("m");
		bc->ops[2].opcode = AMTAIL_AST_OPCODE_MATCH;
		bc->ops[2].export_name = string_init_dup("$line /foo/");
		bc->ops[2].re_match = amtail_regex_compile("foo");
		bc->ops[3].opcode = AMTAIL_AST_OPCODE_RUN;
		bc->ops[4].opcode = AMTAIL_AST_OPCODE_VARIABLE;
		bc->ops[4].vartype = ALLIGATOR_VARTYPE_COUNTER;
		bc->ops[4].export_name = string_init_dup("nm");
		bc->ops[5].opcode = AMTAIL_AST_OPCODE_ASSIGN;
		bc->ops[5].export_name = string_init_dup("nm");
		bc->ops[6].opcode = AMTAIL_AST_OPCODE_NOTMATCH;
		bc->ops[6].export_name = string_init_dup("$line /bar/");
		bc->ops[6].re_match = amtail_regex_compile("bar");
		bc->ops[7].opcode = AMTAIL_AST_OPCODE_RUN;
		bc->ops[8].opcode = AMTAIL_AST_OPCODE_NOOP;
		string *line = string_init_dup("foo baz\n");
		ok = ok && amtail_run(bc, line, amtail_ll) &&
		     runtime_expect_counter(bc->variables, "m", 1) &&
		     runtime_expect_counter(bc->variables, "nm", 1);
		string_free(line);
		runtime_bc_free(bc);
	}

	return ok;
}

static int vm_runtime_tests(void)
{
	int ok = 1;
	int rc_timestamp = vm_runtime_test_timestamp();
	int rc_len_strtol = vm_runtime_test_len_strtol();
	int rc_strptime_match = vm_runtime_test_strptime_and_match();
	ok = rc_timestamp && rc_len_strtol && rc_strptime_match;
	printf("[VM] timestamp=%d len_strtol=%d strptime_match=%d\n",
	       rc_timestamp, rc_len_strtol, rc_strptime_match);
	if (!ok)
		printf("[FAIL][VM] runtime feature tests\n");
	else
		printf("[OK][VM] runtime feature tests\n");
	return ok ? 0 : 1;
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
		if (!strcmp(argv[1], "--vm-runtime"))
			return vm_runtime_tests();
		if (!strcmp(argv[1], "--run"))
		{
			if (argc < 4)
			{
				printf("usage: %s --run <script.mtail> <logfile>\n", argv[0]);
				return 1;
			}
			return run_mtail_script_with_log(argv[2], argv[3]);
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
	if (failed_generator)
		return 1;

	return vm_runtime_tests();
}
