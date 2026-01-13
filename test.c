#include <stdio.h>
#include <stdlib.h>
#include "common/selector.h"
#include "compile.h"
#include "log.h"
#include "file.h"
#include "variables.h"
#include "vm.h"

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
		.parser = 0,
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
	//exit(0);
	amtail_run(byte_code, logline);

	amtail_variables_dump(byte_code->variables);

	amtail_code_free(byte_code);

	releasefile(file_log);
	//string_tokens_free(logline);
}

int main()
{
	amtail_vm_init();
	run_tests_file("tests/nginx.mtail", "nginx.mtail", "log/test2.log");
	//string *logline = string_init_alloc("test 1", 6);
	run_tests_file("tests/apache_combined.mtail", "apache_combined.mtail", "log/test2.log");
	run_tests_file("tests/apache_common.mtail", "apache_common.mtail", "log/test2.log");
	run_tests_file("tests/apache_metrics.mtail", "apache_metrics.mtail", "log/test2.log");

	run_tests_file("tests/dhcpd.mtail", "dhcpd.mtail", "log/test2.log");
	run_tests_file("tests/histogram.mtail", "histogram.mtail", "log/test2.log");
	run_tests_file("tests/lighttpd.mtail", "lighttpd.mtail", "log/test2.log");
	run_tests_file("tests/linecount.mtail", "linecount.mtail", "log/test2.log");
	run_tests_file("tests/mysql_slowqueries.mtail", "mysql_slowqueries.mtail", "log/test2.log");
	run_tests_file("tests/nocode.mtail", "nocode.mtail", "log/test2.log");
	run_tests_file("tests/ntpd.mtail", "ntpd.mtail", "log/test2.log");
	run_tests_file("tests/ntpd_peerstats.mtail", "ntpd_peerstats.mtail", "log/test2.log");
	run_tests_file("tests/postfix.mtail", "postfix.mtail", "log/test2.log");
	run_tests_file("tests/rails.mtail", "rails.mtail", "log/test2.log");
	run_tests_file("tests/rsyncd.mtail", "rsyncd.mtail", "log/test2.log");
	run_tests_file("tests/sftp.mtail", "sftp.mtail", "log/test2.log");
	run_tests_file("tests/timer.mtail", "timer.mtail", "log/test2.log");
	run_tests_file("tests/vsftpd.mtail", "vsftpd.mtail", "log/test2.log");
}
