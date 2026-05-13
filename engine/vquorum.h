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

#ifndef ENGN_VOTEQUORUM_H_
#define ENGN_VOTEQUORUM_H_

#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

#include <corosync/votequorum.h>

#ifdef __cplusplus
extern "C" {
#endif

cs_error_t vquorum_init(void);

cs_error_t vquorum_get_fd(int *fd);

cs_error_t vquorum_dispatch_all(void);
cs_error_t vquorum_heartbeat(void);

unsigned int vquorum_get_total_votes(void);
uint32_t vquorum_get_expected_votes(void);
bool vquorum_get_quorate(void);

cs_error_t vquorum_qdevice_register(void);
cs_error_t vquorum_qdevice_unregister(void);
cs_error_t vquorum_qdevice_poll(int cast_vote);

cs_error_t vquorum_qdisk_set_qdisk_key(uint64_t key);
cs_error_t vquorum_qdisk_share_ikey(uint64_t key);
cs_error_t vquorum_get_node_key(uint32_t nodeid, uint64_t *key);

uint32_t vquorum_get_node_count(void);

void vquorum_get_node_list(uint32_t *num_nodes, uint32_t **node_list);

cs_error_t vquorum_get_node_key_list(uint32_t *num_keys, uint64_t **key_list);

#ifdef __cplusplus
}
#endif

#endif
