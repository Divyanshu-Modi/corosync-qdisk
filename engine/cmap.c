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

#include "cmap.h"
#include "log.h"
#include <corosync/corodefs.h>
#include <corosync/votequorum.h>

static uint32_t g_node_count = 0;

static cmap_handle_t cmap_handle;

static cmap_track_handle_t cmap_reload_track_handle;
static cmap_track_handle_t cmap_nodelist_track_handle;

static void qdisk_cmap_reload_cb(cmap_handle_t, cmap_track_handle_t, int32_t, const char *, struct cmap_notify_value,
                                 struct cmap_notify_value, void *);
static void qdisk_cmap_nodelist_cb(cmap_handle_t cmap_handle, cmap_track_handle_t cmap_track_handle,
    int32_t event, const char *key_name,
    struct cmap_notify_value new_value, struct cmap_notify_value old_value,
    void *user_data);

static void update_node_count(void);

cs_error_t qdisk_cmap_init(void)
{
	int err = CS_OK;

	if(cmap_initialize(&cmap_handle) != CS_OK) {
		fprintf(stderr, "Cannot initialize CMAP service\n");
		cmap_handle = 0;
		return CS_ERR_INIT;
	}

	err = cmap_track_add(cmap_handle, "config.totemconfig_reload_in_progress",
	    CMAP_TRACK_ADD | CMAP_TRACK_MODIFY, qdisk_cmap_reload_cb,
	    NULL, &cmap_reload_track_handle);
	if(CS_OK != err) {
		ENGN_LOG(LOG_ERR, "Failed to track cmap reload. (%s)", cs_strerror(err));
		return err;
	}

	err = cmap_track_add(cmap_handle, "nodelist.",
	    CMAP_TRACK_ADD | CMAP_TRACK_DELETE | CMAP_TRACK_MODIFY | CMAP_TRACK_PREFIX,
	    qdisk_cmap_nodelist_cb,
	    NULL, &cmap_nodelist_track_handle);
	if(CS_OK != err) {
		ENGN_LOG(LOG_ERR, "Failed to track cmap nodelist. (%s)", cs_strerror(err));
		return err;
	}

	update_node_count();

	return err;
}

cs_error_t qdisk_cmap_shutdown(void)
{
	return cmap_finalize(cmap_handle);
}

cs_error_t qdisk_cmap_get_node_count(uint32_t *node_count)
{
	*node_count = g_node_count;
	return CS_OK;
}

cs_error_t qdisk_cmap_get_qdisk_device(char **device_path)
{
	if(0 == cmap_handle) {
		return CS_ERR_INIT;
	}
	return cmap_get_string(cmap_handle, "quorum.device.disk.device", device_path);
}

cs_error_t qdisk_cmap_get_qdisk_key_file(char **key_file)
{
	if(0 == cmap_handle) {
		return CS_ERR_INIT;
	}
	return cmap_get_string(cmap_handle, "quorum.device.disk.keyfile", key_file);
}

cs_error_t qdisk_cmap_get_qdisk_keystr(char **key)
{
	if(0 == cmap_handle) {
		return CS_ERR_INIT;
	}
	return cmap_get_string(cmap_handle, "quorum.device.disk.key", key);
}

cs_error_t qdisk_cmap_get_heartbeat(uint32_t *heartbeat)
{
	if(0 == cmap_handle) {
		return CS_ERR_INIT;
	}
	if(CS_OK != cmap_get_uint32(cmap_handle, "quorum.device.disk.heartbeat", heartbeat)) {
		if(CS_OK != cmap_get_uint32(cmap_handle, "totem.token", heartbeat)){
			*heartbeat = 3000;
		}
		*heartbeat /= 4; // want to deafult to 1/4 token timeout
	}
	return CS_OK;
}

cs_error_t qdisk_cmap_get_timeout(uint32_t *timeout)
{
	if(0 == cmap_handle) {
		return CS_ERR_INIT;
	}
	if(CS_OK != cmap_get_uint32(cmap_handle, "quorum.device.disk.timeout", timeout)) {
		*timeout = VOTEQUORUM_QDEVICE_DEFAULT_TIMEOUT*2;
	}
	return CS_OK;
}

cs_error_t qdisk_cmap_get_qdisk_can_operate(uint8_t *qdisk_can_operate)
{
	if(0 == cmap_handle) {
		return CS_ERR_INIT;
	}
	return cmap_get_uint8(cmap_handle, "runtime.votequorum.qdisk_can_operate", qdisk_can_operate);
}

cs_error_t qdisk_cmap_get_corosync_logfile(char** path)
{
	if(0 == cmap_handle) {
		return CS_ERR_INIT;
	}
	return cmap_get_string(cmap_handle, "logging.logfile", path);
}

cs_error_t qdisk_cmap_dispatch_all(void)
{
	if(0 == cmap_handle) {
		return CS_ERR_INIT;
	}
	cs_error_t err = cmap_dispatch(cmap_handle, CS_DISPATCH_ALL);

	if(err == CS_ERR_LIBRARY) {
		cmap_finalize(cmap_handle);
		sleep(1);
		if(CS_OK != qdisk_cmap_init()) {
			abort();
		}
	}
	return err;
}

cs_error_t qdisk_cmap_get_fd(int *fd)
{
	return cmap_fd_get(cmap_handle, fd);
}


static int cmap_reload_in_progress = 0;

static void update_node_count(void)
{
	cs_error_t cs_err;
	uint32_t new_nodecount = 0;
	uint32_t node_pos, last_node_pos=-1;
	size_t value_len = 0;
	cmap_iter_handle_t iter;
	char iter_key[CMAP_KEYNAME_MAXLEN+1];
	char tmp_key[CMAP_KEYNAME_MAXLEN+1];

	cs_err = cmap_iter_init(cmap_handle, "nodelist.node.", &iter);
	if(cs_err != CS_OK) {
		ENGN_LOG(LOG_CRIT, "Failed to fetch node count from cmap! (%s)", cs_strerror(cs_err));
	}

	while(CS_OK == cmap_iter_next(cmap_handle, iter, iter_key, &value_len, NULL)) {
		int res = sscanf(iter_key, "nodelist.node.%u.%s", &node_pos, tmp_key);
		if (res != 2) {
			continue;
		}
		/* If current node_pos is the same as the last_node_pos then skip it
		 * so we only do the code below once per node. (icmap keys are always in order)
		 */
		if (last_node_pos == node_pos) {
			continue;
		}
		last_node_pos = node_pos;

		new_nodecount++;
	}
	cmap_iter_finalize(cmap_handle, iter);

	if(g_node_count != new_nodecount) {
		ENGN_LOG(LOG_DEBUG, "Node count changed %" PRIu32 "->%" PRIu32, new_nodecount, g_node_count);
		g_node_count = new_nodecount;
	}
}

static void qdisk_cmap_reload_cb(cmap_handle_t handle, cmap_track_handle_t cmap_track_handle,
    int32_t event, const char *key_name,
    struct cmap_notify_value new_value, struct cmap_notify_value old_value,
    void *user_data)
{
	(void)handle; (void)cmap_track_handle;(void)event;(void)old_value;(void)user_data;

	uint8_t reload;

	// Wait for full reload
	if (strcmp(key_name, "config.totemconfig_reload_in_progress") == 0 &&
	    new_value.type == CMAP_VALUETYPE_UINT8 && new_value.len == sizeof(reload)) {
		reload = 1;
		if (memcmp(new_value.data, &reload, sizeof(reload)) == 0) {
			cmap_reload_in_progress = 1;
			return ;
		} else {
			cmap_reload_in_progress = 0;
		}
	}

	if (cmap_reload_in_progress) {
		return ;
	}

	update_node_count();
}

static void qdisk_cmap_nodelist_cb(cmap_handle_t handle, cmap_track_handle_t cmap_track_handle,
    int32_t event, const char *key_name,
    struct cmap_notify_value new_value, struct cmap_notify_value old_value,
    void *user_data)
{
	(void)handle; (void)cmap_track_handle;(void)event;(void)old_value;(void)user_data;
	(void)key_name;(void)new_value;
	update_node_count();
}
