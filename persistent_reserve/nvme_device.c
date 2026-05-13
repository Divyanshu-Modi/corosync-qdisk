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

#include "persistent_reserve.h"
#include "nvme_device.h"
#include "helpers.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define NVMEPRDEV_KEY_LENGTH 16

//reservation types
enum nvme_dev_reservation_type { 
    NVME_WRITE_EXCLUSIVE=1,
    NVME_EXCLUSIVE_ACCESS=2,
    NVME_WRITE_EXCLUSIVE_REGISTRANTS_ONLY=3,
    NVME_EXCLUSIVE_ACCESS_REGISTRANTS_ONLY=4,
    NVME_WRITE_EXCLUSIVE_ALL_REGISTRANTS=5,
    NVME_EXCLUSIVE_ACCESS_ALL_REGISTRANTS=6
};

//acquire actions
enum nvme_dev_acquire_actions { 
    NVME_RACQA_ACQUIRE=0,
    NVME_RACQA_PREEMPT=1,
    NVME_RACQA_PREEMPT_AND_ABORT=2
};

//acquire actions
enum nvme_dev_release_actions { 
    NVME_RRELA_RELEASE=0,
    NVME_RRELA_CLEAR=1
};

struct device_priv {
	struct persistent_reserve_device super;
	uint64_t ikey;
	char key[PERSISTENT_RESERVE_KEY_LENGTH + 1];
	char device_name[PERSISTENT_RESERVE_DEVICE_MAXLEN + 1];
	char id[PERSISTENT_RESERVE_DEVICE_MAXLEN+1];
};

struct resv_rep_controller {
	uint64_t hostid;
	uint64_t ikey;
	char key[NVMEPRDEV_KEY_LENGTH + 1];
	uint8_t rcsts; ///< Reservation Status (bit 0 is one if this controler holds a reservation)
	uint16_t id;
};

struct resv_report {
	uint32_t gen;
	uint8_t rtype;
	uint8_t ptpls; ///< Persist Through Power Loss State
	uint16_t num_controllers;

	struct resv_rep_controller controllers[];
};

static pr_dev_err get_reservation_report(struct device_priv *device, struct resv_report **report)
{
	char cmd[CMD_LENGTH] = { 0 };
	char line[CMD_RESULT_LENGTH] = { 0 };
	FILE *pipe = NULL;

	uint32_t gen = 0;
	uint8_t rtype = 0;
	uint8_t ptpls = 0;
	uint16_t num_controllers = 0;

	struct resv_report *res = NULL;

	sprintf(cmd, "nvme resv-report %s", device->device_name);

	pipe = popen(cmd, "r");
	if(!pipe) {
		fprintf(stderr, "Error opening pipe to run command '%s'", cmd);
		return PR_ERR_EXEC_FAILED;
	}

	// Parse headers
	while(fgets(line, CMD_RESULT_LENGTH, pipe) != NULL) {
		uintmax_t value;
		char *split = strchr(line, ':');
		if(!split) continue; // blank line or something we don't care about

		value = strtoumax(split + 1, NULL, 10);

		if(strstr(line, "regctlext")) {
			break;
		} else if(strstr(line, "gen")) {
			gen = (uint32_t)value;
		} else if(strstr(line, "rtype")) {
			rtype = (uint8_t)value;
		} else if(strstr(line, "regctl")) {
			num_controllers = (uint16_t)value;
		} else if(strstr(line, "ptpls")) {
			ptpls = (uint8_t)value;
		}
	}

	res = calloc(1, sizeof(*res) + num_controllers * sizeof(*res->controllers));

	if(!res) {
		pclose(pipe);
		return PR_ERR_DEVICE_MEMORY_ALLOCATION_FAILED;
	}

	res->gen = gen;
	res->rtype = rtype;
	res->ptpls = ptpls;
	res->num_controllers = num_controllers;
#ifdef DEBUG
	printf("gen = %" PRIu32 "\n"
	       "rtype =%" PRIu8 "\n"
	       "regctl =%" PRIu16 "\n"
	       "ptpls =%" PRIu8 "\n",
	       gen, rtype, num_controllers, ptpls);
#endif

	for(size_t i = 0; i < num_controllers; i++) {
		while(fgets(line, CMD_RESULT_LENGTH, pipe) != NULL) {
			uintmax_t value;
			char *split = NULL;
			if(strstr(line, "regctl")) break;

			split = strchr(line, ':');
			if(!split) continue;
			*split = '\0';

			value = strtoumax(split + 1, NULL, 16);

			if(strstr(line, "cntlid")) {
				res->controllers[i].id = (uint16_t)value;
			} else if(strstr(line, "rcsts")) {
				res->controllers[i].rcsts = (uint8_t)value;
			} else if(strstr(line, "rkey")) {
				res->controllers[i].ikey = value;
				sprintf(res->controllers[i].key, "%" PERSISTENT_RESERVE_PRI_KEY, // Can't overrun, always produces same length string
				        res->controllers[i].ikey);
			} else if(strstr(line, "hostid")) {
				res->controllers[i].hostid = (uint64_t)value;
			}
		}
#ifdef DEBUG
		printf("cntlid = %" PRIu16 "\n"
		       "rcsts = %" PRIu8 "\n"
		       "rkey = %s\n"
		       "hostid = %" PRIu64 "\n",
		       res->controllers[i].id, res->controllers[i].rcsts, res->controllers[i].key, res->controllers[i].hostid);
#endif
	}

	int rc = pclose(pipe);
	*report = res;
	return rc;
}

static pr_dev_err device_get_registered_keys(struct persistent_reserve_device *handle, size_t *num_keys, uint64_t **keys)
{
	struct device_priv *nvme_device = (struct device_priv *)handle;
	pr_dev_err ret = 0;
	struct resv_report *report = NULL;

	ret = get_reservation_report(nvme_device, &report);
	if(ret != PR_ERR_OK) {
		return ret;
	}

	*keys = calloc(report->num_controllers, sizeof(**keys));
	if(*keys == NULL) {
		free(report);
		return PR_ERR_DEVICE_MEMORY_ALLOCATION_FAILED;
	}

	*num_keys = report->num_controllers;
	for(size_t i = 0; i < report->num_controllers; i++) {
		(*keys)[i] = report->controllers[i].ikey;
	}
	free(report);
	return 0;
}

static pr_dev_err device_register_key(struct persistent_reserve_device *handle)
{
	struct device_priv *nvme_handle = (struct device_priv *)handle;
	char cmd[CMD_LENGTH] = { 0 };
	int ret = 0;

	sprintf(cmd, "nvme resv-register %s --crkey=0x0 --nrkey=0x%" PERSISTENT_RESERVE_PRI_KEY " --rrega=0 2>&1 > /dev/null",
	        nvme_handle->device_name, nvme_handle->ikey);
	ret = exec_cmd(cmd);

	return ret;
}

static pr_dev_err device_unregister_key(struct persistent_reserve_device *handle)
{
	struct device_priv *nvme_handle = (struct device_priv *)handle;
	char cmd[CMD_LENGTH] = {};
	int ret = 0;

	sprintf(cmd, "nvme resv-register %s --crkey=0x%" PERSISTENT_RESERVE_PRI_KEY " --nrkey=0x0 --rrega=1 2>&1 > /dev/null", nvme_handle->device_name,
	        nvme_handle->ikey);
	ret = exec_cmd(cmd);

	return ret;
}

static pr_dev_err device_reserve(struct persistent_reserve_device *handle)
{
	struct device_priv *nvme_handle = (struct device_priv *)handle;
	char cmd[CMD_LENGTH] = { 0 };
	int ret = 0;

	sprintf(cmd, "nvme resv-acquire %s --crkey=0x%" PERSISTENT_RESERVE_PRI_KEY " --racqa=0 --rtype=%d 2>&1 > /dev/null", nvme_handle->device_name,
	        nvme_handle->ikey, NVME_WRITE_EXCLUSIVE_REGISTRANTS_ONLY);
	ret = exec_cmd(cmd);
	return ret;
}

static pr_dev_err device_release(struct persistent_reserve_device *handle)
{
	struct device_priv *nvme_handle = (struct device_priv *)handle;
	char cmd[CMD_LENGTH] = {};
	int ret = 0;

	sprintf(cmd, "nvme resv-release %s --crkey=0x%" PERSISTENT_RESERVE_PRI_KEY " --rtype=%d --rrela=%d 2>&1 > /dev/null", nvme_handle->device_name,
	        nvme_handle->ikey, NVME_WRITE_EXCLUSIVE_REGISTRANTS_ONLY, NVME_RRELA_RELEASE);
	ret = exec_cmd(cmd);

	return ret;
}

static pr_dev_err device_abort(struct persistent_reserve_device *handle)
{
	struct device_priv *nvme_handle = (struct device_priv *)handle;
	char cmd[CMD_LENGTH] = { 0 };
	int ret = 0;

	sprintf(cmd, "nvme resv-release %s --crkey=0x%" PERSISTENT_RESERVE_PRI_KEY " --rtype=%d --rrela=%d 2>&1 > /dev/null", nvme_handle->device_name,
	        nvme_handle->ikey, NVME_WRITE_EXCLUSIVE_REGISTRANTS_ONLY, NVME_RRELA_CLEAR);
	ret = exec_cmd(cmd);
	if(ret != 0) {
		return PR_ERR_REGISTRATION_FAILED;
	}

	return PR_ERR_OK;
}

static int device_is_reserved(struct persistent_reserve_device *handle)
{
	struct device_priv *nvme_device = (struct device_priv *)handle;
	pr_dev_err ret = 0;

	struct resv_report *report = NULL;

	ret = get_reservation_report(nvme_device, &report);
	if(ret != PR_ERR_OK) {
		return ret;
	}

	for(size_t i = 0; i < report->num_controllers; i++) {
		if(report->controllers[i].rcsts) {
			free(report);
			return 1;
		}
	}
	free(report);
	return 0;
}

static const char *device_get_name(struct persistent_reserve_device *handle)
{
	struct device_priv *nvme_device = (struct device_priv *)handle;
	return nvme_device->device_name;
}

static const char *device_get_key(struct persistent_reserve_device *handle)
{
	struct device_priv *nvme_device = (struct device_priv *)handle;
	return nvme_device->key;
}

static uint64_t device_get_ikey(struct persistent_reserve_device *handle)
{
	struct device_priv *nvme_device = (struct device_priv *)handle;
	return nvme_device->ikey;
}

static const char *device_get_id(struct persistent_reserve_device *handle)
{
	struct device_priv *device = (struct device_priv *)handle;
	return device->id;
}

static pr_dev_err device_get_reservation_owner_key(struct persistent_reserve_device *handle, uint64_t *key)
{
	struct device_priv *nvme_device = (struct device_priv *)handle;
	struct resv_report *report = NULL;

	pr_dev_err ret = PR_ERR_OK;

	ret = get_reservation_report(nvme_device, &report);
	if(ret != PR_ERR_OK) {
		return ret;
	}

	// Return an error if the device is not reserved to indicate we didn't set the key
	ret = PR_ERR_DEVICE_NOT_RESERVED;
	for(size_t i = 0; i < report->num_controllers; i++) {
		if(report->controllers[i].rcsts) {
			*key = report->controllers[i].ikey;
			ret = PR_ERR_OK;
		}
	}

	free(report);
	return ret;
}

static void device_delete(struct persistent_reserve_device *device)
{
	free(device);
}

static const struct persistent_reserve_device_class nvme_device_vtable = { 
	.register_key = device_register_key,
	.unregister_key = device_unregister_key,
	.reserve = device_reserve,
	.release = device_release,
	.is_reserved = device_is_reserved,
	.abort = device_abort,
	.get_name = device_get_name,
	.get_key = device_get_key,
	.get_ikey = device_get_ikey,
	.get_registered_keys = device_get_registered_keys,
	.get_device_id = device_get_id,
	.get_reservation_owner_key = device_get_reservation_owner_key,
	.free = device_delete,
};

static char *map_device_path(const char *device_name)
{
	if(strncmp(device_name, "nvme:", 5) == 0) {
		char tmp[PERSISTENT_RESERVE_DEVICE_MAXLEN + 1] = { 0 };
		strncpy(tmp, device_name+5, PERSISTENT_RESERVE_DEVICE_MAXLEN);
		// trim spaces
		size_t skip = strspn(tmp, " \t\n");
		tmp[skip + strcspn(tmp+skip, " \t\n")] = '\0';

		char *res = calloc(sizeof(char), PERSISTENT_RESERVE_DEVICE_MAXLEN + 1);
		if(!res) {
			abort();
		}
		snprintf(res, PERSISTENT_RESERVE_DEVICE_MAXLEN, "/dev/disk/by-id/nvme-%s", tmp + skip);
		return res;
	}

	return strdup(device_name);
}

struct persistent_reserve_device *nvme_pr_device_init(const char *device_name, uint64_t ikey)
{
	char cmd[CMD_LENGTH] = {};
	char output[CMD_RESULT_LENGTH];
	struct device_priv *device = NULL;
	int err = 0;

	if(strlen(device_name) >= PERSISTENT_RESERVE_DEVICE_MAXLEN) {
		return NULL;
	}

	device = malloc(sizeof(*device));
	if(NULL == device) {
		return NULL;
	}
	device->super.clazz = &nvme_device_vtable;
	char *tmp = map_device_path(device_name);
	strncpy(device->device_name, tmp, PERSISTENT_RESERVE_DEVICE_MAXLEN);
	free(tmp);


	sprintf(cmd, "nvme id-ctrl %s", device->device_name);
	err = exec_cmd_output(cmd, output, sizeof(output));
	if(err != 0) {
		free(device);
		return NULL;
	} 

	// Parse out the parts we need from the command output
	char *mn = NULL;
	char *sn = NULL;
	char *token = strtok(output, ":\r\n");
	while(token) {
		char *label = token;
		char *value = strtok(NULL, ":\r\n");
		if(!value) {
			break;
		}

		// trim string
		value += strspn(value, " \t");
		long i = (long)strlen(value) - 1;
		for(; i > 0 && isspace(value[i-1]); i--) { }
		if(i >= 0) {
			value[i] = '\0';
		}

		if(strstr(label, "mn")) {
			mn = value;
		}
		if(strstr(label, "sn")) {
			sn = value;
		}
	}

	if(!mn || !sn) {
		free(device);
		return NULL;
	} 

	// Build device id
	strncpy(device->id, mn, PERSISTENT_RESERVE_DEVICE_MAXLEN);
	strncat(device->id, "_", PERSISTENT_RESERVE_DEVICE_MAXLEN);
	strncat(device->id, sn, PERSISTENT_RESERVE_DEVICE_MAXLEN);
	for(size_t i=0; device->id[i]; i++) {
		if(isspace(device->id[i])) {
			device->id[i] = '_';
		}
	}

	device->ikey = ikey;
	sprintf(device->key, "%" PERSISTENT_RESERVE_PRI_KEY, device->ikey);

	return (struct persistent_reserve_device *)device;
}

int is_nvme_device(const char *device)
{
	int ret = 0;
	char cmd[CMD_LENGTH] = {};

	char *device_path = map_device_path(device);
	sprintf(cmd, "nvme id-ctrl %s 2>&1 1>/dev/null", device_path);
	free(device_path);
	ret = exec_cmd(cmd);

	return ret == 0;
}

int is_nvme_device_usable(const char* device)
{
	int ret = 0;
	char cmd[CMD_LENGTH] = {};
	char result[CMD_RESULT_LENGTH];

	char *device_path = map_device_path(device);
	sprintf(cmd, "nvme id-ns %s 2>&1", device_path);
	free(device_path);
	ret = exec_cmd_output(cmd, result, sizeof(result));
	if(ret != 0) {
		return 0;
	} 

	// Look for a non-zero value for the "rescap" feild indicating support for NVMe reservations
	char * found = strstr(result, "rescap");
	if(!found) {
		return 0;
	}
	found = strchr(found, ':');
	if(!found) {
		return 0;
	}
	found++;
	return 0 != strtol(found, NULL, 0);
}
