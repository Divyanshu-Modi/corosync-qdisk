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

#ifndef PERSISTENT_RESERVE_DEVICE_H
#define PERSISTENT_RESERVE_DEVICE_H

#include <unistd.h>
#include <stddef.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PERSISTENT_RESERVE_DEVICE_MAXLEN 300U
#define PERSISTENT_RESERVE_KEY_LENGTH 16
#define PERSISTENT_RESERVE_PRI_KEY "016" PRIx64

#define PERSISTENT_RESERVE_DEFAULT_KEYFILE "/etc/corosync/pr_reserve_key"

typedef enum {
	PR_ERR_OK = 0,
	PR_ERR_UNKNOWN = -127,
	PR_ERR_DEVICE_LEN_EXCEEDED,
	PR_ERR_DEVICE_INVALID_DEVICE_TYPE,
	PR_ERR_DEVICE_MEMORY_ALLOCATION_FAILED,
	PR_ERR_DEVICE_CANT_READ_KEYFILE,
	PR_ERR_DEVICE_NOT_READY,
	PR_ERR_DEVICE_NOT_OPERABLE,
	PR_ERR_DEVICE_NOT_RESERVED,
	PR_ERR_REGISTRATION_FAILED,
	PR_ERR_RESERVATION_FAILED,
	PR_ERR_UNREGISTER_FAILED,
	PR_ERR_RELEASE_FAILED,
	PR_ERR_ABORT_FAILED,
	PR_ERR_FOUND_UNREGISTERED_NODE_KEY,
	PR_ERR_EXEC_FAILED,
	PR_ERR_PARSE_DATA_ERR,
} pr_dev_err;

struct persistent_reserve_device_class;

struct persistent_reserve_device
{
	const struct persistent_reserve_device_class *clazz;
};

struct persistent_reserve_device_class {
	pr_dev_err (*register_key)(struct persistent_reserve_device *handle);
	pr_dev_err (*unregister_key)(struct persistent_reserve_device *handle);
	pr_dev_err (*reserve)(struct persistent_reserve_device *handle);
	pr_dev_err (*release)(struct persistent_reserve_device *handle);
	int (*is_reserved)(struct persistent_reserve_device *handle);
	const char* (*get_name)(struct persistent_reserve_device *handle);
	const char* (*get_key)(struct persistent_reserve_device *handle);
	uint64_t (*get_ikey)(struct persistent_reserve_device *handle);
	pr_dev_err (*get_registered_keys)(struct persistent_reserve_device *handle, size_t *num_keys, uint64_t **keys);
	pr_dev_err (*abort)(struct persistent_reserve_device *handle);
	const char * (*get_device_id)(struct persistent_reserve_device *);
	pr_dev_err (*get_reservation_owner_key)(struct persistent_reserve_device *handle, uint64_t *key);
	void (*free)(struct persistent_reserve_device *handle);
};

/// Register our key with the device
static inline pr_dev_err persistent_reserve_device_register_key(struct persistent_reserve_device *handle) { return handle->clazz->register_key(handle); }

/// Remove the registration our key with the device
static inline pr_dev_err persistent_reserve_device_unregister_key(struct persistent_reserve_device *handle) { return handle->clazz->unregister_key(handle); }

/// Attempt to acquire a reservation on the device
static inline pr_dev_err persistent_reserve_device_reserve(struct persistent_reserve_device *handle) { return handle->clazz->reserve(handle); }

/// Release the reservation if we hold it
static inline pr_dev_err persistent_reserve_device_release(struct persistent_reserve_device *handle) { return handle->clazz->release(handle); }

/**
 * Check if the device is already reserved
 * @return negative for error, 0 for false, positive for true
 */
static inline int persistent_reserve_device_is_reserved(struct persistent_reserve_device *handle) { return handle->clazz->is_reserved(handle); }

static inline const char* persistent_reserve_device_get_name(struct persistent_reserve_device *handle) { return handle->clazz->get_name(handle); }

/**
 * @brief Get the key used with this device
 */
static inline uint64_t persistent_reserve_device_get_key(struct persistent_reserve_device *handle) { return handle->clazz->get_ikey(handle); }

/** 
 * @brief Get the unique identifier used for this device. WWN for SCSI devices, manufacturer and serial number for NVMe
 */
static inline const char* persistent_reserve_device_get_device_id(struct persistent_reserve_device *handle) { return handle->clazz->get_device_id(handle); }

/**
 * @brief Get a list of keys registered with the device
 * @param handle
 * @param num_keys [out]
 * @param keys [out] array of keys, must call free() when done
 */
static inline pr_dev_err persistent_reserve_device_get_registered_keys(struct persistent_reserve_device *handle, size_t *num_keys, uint64_t **keys) { return handle->clazz->get_registered_keys(handle, num_keys, keys); }

/**
 * @brief Get the key registered by the current holder of a reservation
 * @param handle device handle of a persistent reserver device
 * @param key [out] pointer to a uint64_t to store the key in.
 * @return Error code or PR_ERR_OK on success, PR_ERR_DEVICE_NOT_RESERVED if the device wasn't reserved
 */
static inline pr_dev_err persistent_reserve_device_reservation_owner_key(struct persistent_reserve_device *handle, uint64_t *key) { return handle->clazz->get_reservation_owner_key(handle, key); }

/** 
 * @brief Clear an existing reservation no matter which host holds it.
 */
static inline pr_dev_err persistent_reserve_device_abort(struct persistent_reserve_device *handle) { return handle->clazz->abort(handle); }

int is_scsi_device(const char* device);
int is_nvme_device(const char* device);

int is_scsi_device_usable(const char* device);
int is_nvme_device_usable(const char* device);

struct persistent_reserve_device *persistent_reserve_device_new(const char *device_path, const char *key_path);
struct persistent_reserve_device *persistent_reserve_device_new_key(const char *device_path, uint64_t ikey);
static inline void persistent_reserve_device_free(struct persistent_reserve_device *handle) { handle->clazz->free(handle); }
#ifdef __cplusplus
}
#endif

#endif 
