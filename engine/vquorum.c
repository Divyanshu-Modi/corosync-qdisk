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

#include "vquorum.h"
#include "log.h"

#include <unistd.h>
#include <inttypes.h>

#include <qb/qblist.h>

/* vquorum global vars*/
static votequorum_handle_t v_handle;
static struct votequorum_info info;
static uint32_t g_our_nodeid = 0;
votequorum_ring_id_t g_ring_id = { 0 };

static uint32_t g_visible_node_list_capacity = 0;
static uint32_t g_visible_node_list_entries = 0;
static uint32_t *g_visible_node_list = NULL;

static uint32_t g_node_count = 0;
static uint32_t g_vqnode_list_entries = 0;
static votequorum_node_t *g_vqnode_list = NULL;

static uint32_t g_expected_votes = 0;
static uint32_t g_quorate = 0;

static uint64_t qdisk_key = 0;

struct node_info {
	uint32_t nodeid;
	uint32_t state;
	uint64_t disk_key;
} *g_node_list = NULL;

static void quorum_notification_fn (
	votequorum_handle_t handle,
	uint64_t context,
	uint32_t quorate,
	uint32_t node_list_entries,
	votequorum_node_t node_list[])
{
	(void)handle; (void)context;

	if(g_vqnode_list) {
		free(g_vqnode_list);
	}
	g_vqnode_list_entries = node_list_entries;
	g_vqnode_list = malloc(sizeof(*g_vqnode_list) * node_list_entries);
	memcpy(g_vqnode_list, node_list, sizeof(*g_vqnode_list) * node_list_entries);

	uint32_t new_node_count = 0;
	struct node_info *new_node_list = malloc(sizeof(*new_node_list) * node_list_entries);
	for(uint32_t i=0; i<node_list_entries; i++) {
		if(node_list[i].nodeid == VOTEQUORUM_QDEVICE_NODEID) { // Don't count dummy qdevice node votequorum creates
			break;
		}
		new_node_list[new_node_count].nodeid = node_list[i].nodeid;
		new_node_list[new_node_count].state = node_list[i].state;
		new_node_list[new_node_count].disk_key = 0;
		for(uint32_t j=0; j<g_node_count; j++) { // O(n*n) would like to do better but would need a lot of code to do so
			if(node_list[i].nodeid == g_node_list[j].nodeid) {
				new_node_list[new_node_count].disk_key = g_node_list[j].disk_key;
				break;
			}
		}

		new_node_count++;
	}
	if(g_node_list) {
		free(g_node_list);
	}
	g_node_list = new_node_list;
	g_node_count = new_node_count;

	if(g_quorate != quorate) {
		ENGN_LOG(LOG_INFO, "Quorate changed %" PRIu32 "->%" PRIu32, g_quorate, quorate);
	}
	g_quorate = quorate;

	if(qdisk_key) {
		vquorum_qdisk_share_ikey(qdisk_key);
	}
}

uint32_t vquorum_get_node_count(void)
{
	return g_node_count;
}

static void expected_votes_notification_fn(votequorum_handle_t handle, uint64_t context,
                                           uint32_t expected_votes)
{
	(void)handle; (void)context;
	if(g_expected_votes != expected_votes) {
		ENGN_LOG(LOG_DEBUG, "Expected votes changed %" PRIu32 "->%" PRIu32, g_expected_votes, expected_votes);
	}
	g_expected_votes = expected_votes;
}

static void nodelist_notification_fn(votequorum_handle_t handle, uint64_t context, votequorum_ring_id_t ring_id,
                                       uint32_t node_list_entries, uint32_t node_list[])
{
	(void)handle; (void)context;

	if(g_visible_node_list_entries != node_list_entries) {
		ENGN_LOG(LOG_DEBUG, "Visible node count chaged %" PRIu32 "->%" PRIu32, g_visible_node_list_entries, node_list_entries);
	}

	if(g_visible_node_list_capacity < node_list_entries) {
		uint32_t *new_list = realloc(g_visible_node_list, sizeof(*node_list) * node_list_entries);
		if(!new_list) {
			ENGN_LOG(LOG_CRIT, "Can't alloc votequorum node list memory");
			abort();
		}
		g_visible_node_list = new_list;
		g_visible_node_list_capacity = node_list_entries;
	}
	memcpy(g_visible_node_list, node_list, sizeof(*node_list) * node_list_entries);
	g_visible_node_list_entries = node_list_entries;
	g_ring_id = ring_id;

	// dump debug log of visible nodes
	char buf[node_list_entries*5+1];
	memset(buf, 0, sizeof(buf));
	size_t pos = 0;
	for(uint32_t i=0; i < node_list_entries && pos+1 < sizeof(buf); i++) {
		int r = snprintf(buf+pos, sizeof(buf)-pos-1, "%03" PRIu32 "\n", node_list[i]);
		if(r > 0) {
			pos += r;
		}
	}
	ENGN_LOG(LOG_TRACE, "Visible nodes:\n%s\n", buf);

	cs_error_t err = votequorum_getinfo(v_handle, g_our_nodeid, &info);
	if(CS_OK != err) {
		ENGN_LOG(LOG_ERR, "Unable to query votequorum info: %s\n", cs_strerror(err));
	}
}

static void qdevice_extra_info_notify(
	votequorum_handle_t handle,
	uint64_t context,
	uint32_t nodeid,
	uint32_t ei_size,
	void *extra_info)
{
	(void)handle; (void)context;

	uint64_t disk_key = 0;
	if(ei_size) { //TODO: more validation
		sscanf(extra_info, "Qdsk %016" SCNx64, &disk_key);
	}
	for(uint32_t i=0; i<g_node_count; i++) {
		if(nodeid == g_node_list[i].nodeid) {
			ENGN_LOG(LOG_INFO, "Node %" PRIu32 " disk key %016" PRIx64 "->%016" PRIx64, nodeid, g_node_list[i].disk_key, disk_key);
			g_node_list[i].disk_key = disk_key;
			return;
		}
	}

	ENGN_LOG(LOG_NOTICE, "Got key update for node %" PRIu32 " disk key %016" PRIx64 " but that node isn't in our current list", nodeid, disk_key);

	struct node_info *new_node_list = realloc(g_node_list, sizeof(*g_node_list) * (g_node_count + 1));
	if(!new_node_list) {
		return;
	}
	g_node_list = new_node_list;

	g_node_list[g_node_count].nodeid = nodeid;
	g_node_list[g_node_count].state = 0;
	g_node_list[g_node_count].disk_key = disk_key;
	g_node_count++;
}

votequorum_model_v1_data_t callbacks = {
	.model = VOTEQUORUM_MODEL_V1,
	.votequorum_quorum_notify_fn = quorum_notification_fn,
	.votequorum_expectedvotes_notify_fn = expected_votes_notification_fn,
	.votequorum_nodelist_notify_fn = nodelist_notification_fn,
	.votequorum_qdevice_extra_info_fn = qdevice_extra_info_notify,
};


cs_error_t vquorum_init(void)
{
	cs_error_t err = CS_OK;

	// initalize votequorum service
	err = votequorum_model_initialize(&v_handle, &callbacks);
	if(err != CS_OK) {
		ENGN_LOG(LOG_ERR, "Cannot initialise VOTEQUORUM service");
		v_handle = 0;
		return err;
	}

	err = votequorum_getinfo(v_handle, g_our_nodeid, &info);
	if(CS_OK != err) {
		ENGN_LOG(LOG_ERR, "Unable to query votequorum info: %s\n", cs_strerror(err));
		return err;
	}

	g_our_nodeid = info.node_id;
	g_expected_votes = info.node_expected_votes;

	ENGN_LOG(LOG_DEBUG, "Votequorum initialized. handle:%p, callbacks:%p\n", v_handle, callbacks);
	err = votequorum_trackstart(v_handle, 0LL, CS_TRACK_CHANGES);
	if(err != CS_OK) {
		ENGN_LOG(LOG_ERR, "Unable to start votequorum status tracking: %s\n", cs_strerror(err));
		return err;
	}

	err = votequorum_dispatch(v_handle, CS_DISPATCH_ALL); // Run a dispatch cycle to try to get initial values
	//TODO: wait for one pass through quorum_notification_fn()?
	return err;
}

cs_error_t vquorum_qdisk_set_qdisk_key(uint64_t key)
{
	qdisk_key = key;
	return CS_OK;
}

cs_error_t vquorum_get_fd(int *fd)
{
	return votequorum_fd_get(v_handle, fd);
}

cs_error_t vquorum_dispatch_all(void)
{
	cs_error_t err = votequorum_dispatch(v_handle, CS_DISPATCH_ALL);
	if(err == CS_ERR_LIBRARY) {
		votequorum_finalize(v_handle);
		sleep(1);
		if(CS_OK != vquorum_init()) {
			abort();
		}
	}
	return err;
}

cs_error_t vquorum_heartbeat(void)
{
	cs_error_t err = votequorum_getinfo(v_handle, g_our_nodeid, &info);
	if(CS_OK != err) {
		ENGN_LOG(LOG_ERR, "Unable to query votequorum info: %s\n", cs_strerror(err));
		return err;
	}
	g_expected_votes = info.node_expected_votes;
	return CS_OK;
}

bool vquorum_get_quorate(void)
{
	return g_quorate;
}

uint32_t vquorum_get_expected_votes(void)
{
	return g_expected_votes;
}

unsigned int vquorum_get_total_votes(void)
{
	return info.total_votes;
}

void vquorum_get_node_list(uint32_t *num_nodes, uint32_t **node_list)
{
	*num_nodes = g_visible_node_list_entries;
	*node_list = g_visible_node_list;
}

cs_error_t vquorum_qdisk_share_ikey(uint64_t key)
{
	cs_error_t err = CS_OK;

	char buf[VOTEQUORUM_QDEVICE_EXTRA_NODEINFO_MAXSIZE] = { 0 };
	uint32_t ei_size = 1 + sprintf(buf, "Qdsk %016" PRIx64, key); // include 1 for nul byte
	do {
		err = votequorum_set_qdevice_extra_info(v_handle, ei_size, buf);
	} while(CS_ERR_TRY_AGAIN == err);
	if(CS_OK != err) {
		ENGN_LOG(LOG_ERR, "Unable to share persistent reserve key: %s\n", cs_strerror(err));
		return err;
	}
	return CS_OK;
}

cs_error_t vquorum_get_node_key(uint32_t nodeid, uint64_t *key)
{
	for(uint32_t i=0; i<g_node_count; i++) {
		if(g_node_list[i].nodeid == nodeid) {
			*key = g_node_list[i].disk_key;
			return CS_OK;
		}
	}

	cs_error_t err = CS_OK;
	char buf[VOTEQUORUM_QDEVICE_EXTRA_NODEINFO_MAXSIZE] = { 0 };
	uint32_t ei_size = 0;

	do {
		err = votequorum_get_qdevice_extra_info(v_handle, nodeid, &ei_size, buf);
	} while(CS_ERR_TRY_AGAIN == err);
	if(CS_OK != err) {
		ENGN_LOG(LOG_ERR, "Unable to query persistent reserve key: %s\n", cs_strerror(err));
		return err;
	}

	sscanf(buf, "Qdsk %016" PRIx64, key);

	ENGN_LOG(LOG_TRACE, "Got key: %016" PRIx64" for node " CS_PRI_NODE_ID "\n", *key, nodeid);
	return err;
}

cs_error_t vquorum_get_node_key_list(uint32_t *num_keys, uint64_t **key_list)
{
	cs_error_t err = CS_OK;
	uint64_t *res = malloc(g_visible_node_list_entries*sizeof(*res));

	if(!res) {
		return CS_ERR_NO_MEMORY;
	}

	size_t j = 0;
	for(size_t i=0; i<g_visible_node_list_entries; i++) {
		err = vquorum_get_node_key(g_visible_node_list[i], &res[j]); // O(n*n), would like to do better...
		if(CS_OK != err) {
			free(res);
			return err;
		}
		if(0 != res[j]) { // if the node has a key move up a slot
			j++;
		}
	}

	*num_keys = j;
	*key_list = res;

	return err;
}


cs_error_t vquorum_qdevice_poll(int cast_vote)
{
	cs_error_t err = CS_OK;

	err = votequorum_dispatch(v_handle, CS_DISPATCH_ALL);
	if(err != CS_OK) {
		ENGN_LOG(LOG_ERR, "Unable to dispatch votequorum status: %s\n", cs_strerror(err));
		return err;
	}

	err = votequorum_qdevice_poll(v_handle, "QDisk", cast_vote, g_ring_id);
	return err;
}

cs_error_t vquorum_qdevice_register(void)
{
	return votequorum_qdevice_register(v_handle, "QDisk");
}

cs_error_t vquorum_qdevice_unregister(void)
{
	return votequorum_qdevice_unregister(v_handle, "QDisk");
}
