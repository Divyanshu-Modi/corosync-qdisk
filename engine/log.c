/*
 * Copyright (c) 2025 IBM.
 *
 * All rights reserved.
 *
 * Author: Thomas Jones (thomas.jones@ibm.com)
 *         Michael Baker
 *
 * This software licensed under BSD license, the text of which follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the Red Hat, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <libgen.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

#include "log.h"
#include "persistent_reserve/persistent_reserve.h"

static const char* TAGS[] = {
	"UNKNOWN",
	"MAIN",
	"ALGORITHM",
	"MISC",
	"PR",
};

const char *qdisk_log_tag_to_string(uint32_t tags) {
	return TAGS[(tags<=4)?tags:0];
}

void pr_log(int level, const char *fmt, ...)
{
	int res = 0;
	va_list args1;
	va_start(args1, fmt);
	va_list args2;
	va_copy(args2, args1);
	res = vsnprintf(NULL, 0, fmt, args1);
	if(res < 0) abort();
	char buf[1+res];
	va_end(args1);
	res = vsnprintf(buf, sizeof buf, fmt, args2);
	if(res < 0) abort();
	va_end(args2);

	qb_logt(level, TAGS_PR, "%s", buf);
}

static char blackbox_path[PATH_MAX] = "/var/log/corosync-qdisk.fdata";

void update_blackbox_path_with_timestamp(const char *dir)
{
	char timestr[30] = { 0 }; // 29 chars + '\0'
	time_t now = time(NULL);
	strftime(timestr, 30, "%F-%T%z", localtime(&now));
	snprintf(blackbox_path, PATH_MAX, "%s/corosync-qdiskd-%s.fdata", dir, timestr);
}

void qdisk_dump_blackbox(void)
{
	qb_log_blackbox_write_to_file(blackbox_path);
}

