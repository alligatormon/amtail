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
		.parser = 1,
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

	struct file *file_log = readfile(log_filename);
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
	//run_tests("tests/apache_combined.mtail", "apache_combined.mtail", logline);
	//run_tests("tests/apache_common.mtail", "apache_common.mtail");
	//run_tests("tests/apache_metrics.mtail", "apache_metrics.mtail");
	//run_tests("tests/dhcpd.mtail", "dhcpd.mtail");
	//run_tests("tests/histogram.mtail", "histogram.mtail");
	//run_tests("tests/lighttpd.mtail", "lighttpd.mtail");
	//run_tests("tests/linecount.mtail", "linecount.mtail");
	//run_tests("tests/mysql_slowqueries.mtail", "mysql_slowqueries.mtail");
	//run_tests("tests/nocode.mtail", "nocode.mtail");
	//run_tests("tests/ntpd.mtail", "ntpd.mtail");
	//run_tests("tests/ntpd_peerstats.mtail", "ntpd_peerstats.mtail");
	//run_tests("tests/postfix.mtail", "postfix.mtail");
	//run_tests("tests/rails.mtail", "rails.mtail");
	//run_tests("tests/rsyncd.mtail", "rsyncd.mtail");
	//run_tests("tests/sftp.mtail", "sftp.mtail");
	//run_tests("tests/timer.mtail", "timer.mtail");
	//run_tests("tests/vsftpd.mtail", "vsftpd.mtail");
}
