
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <fcntl.h>
#include <syslog.h>

#include "helpers.h"
#include "persistent_reserve.h"

int exec_cmd(const char *cmd)
{
	FILE *pipe = popen(cmd, "r");
	if(!pipe) {
		fprintf(stderr, "Error opening pipe to run command");
		return -1;
	}

	pr_log(LOG_TRACE, "running cmd:%s\n", cmd);

	char result[CMD_LENGTH+1];
	// read the command output
	while(fgets(result, CMD_LENGTH, pipe) != NULL) {
		pr_log(LOG_TRACE, "%s\n", result);
	}

	// close the pipe
	int ret = pclose(pipe);
	pr_log(LOG_DEBUG, "cmd:%s\nrc:%d\n", cmd, ret);
	return ret;
}

int exec_cmd_output(const char* cmd, char *result, size_t result_len)
{
	pr_log(LOG_TRACE, "running cmd:%s\n", cmd);

	FILE *pipe = popen(cmd, "r");
	if(!pipe) {
		fprintf(stderr, "Error opening pipe to run command");
		return -1;
	}

	size_t pos = 0;
	// read the command output
	while(pos < result_len-1 && !(feof(pipe) || ferror(pipe))) {
		pos += fread(result+pos, 1, result_len - pos - 1, pipe);
	}
	result[pos] = '\0';

	if(!(feof(pipe) || ferror(pipe))) {
		pr_log(LOG_WARNING, "cmd %s\noutput too long, truncating with this output: %s\n", cmd, result);
	}
	while(!(feof(pipe) || ferror(pipe))) {
		char tmp[513];
		size_t bytes = fread(tmp, 1, 512, pipe);
		tmp[bytes] = '\0';
		pr_log(LOG_WARNING, "cmd output too long extra output: %s\n", tmp);
	}

	// close the pipe
	int ret = pclose(pipe);
	pr_log(LOG_DEBUG, "cmd:%s\nrc:%d\n%s\n", cmd, ret, result);
	return ret;
}
