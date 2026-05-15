# Disk QDevice Design Overview

## Background Information

### NVMe/SCSI-PR

Both NVMe and SCSI3-PR support a method to requestion a lock on a disk called a
reservation. In both protocols the hosts all have an associated 64 bit key. The
list of keys and which one is associated with the host holding a lock can be
queried by any other host.

#### Reservation and Registration

When using either mechansim the host participating must first register a key
with the disk. This registration is seperate from actually aquiring a lock on
the disk. All hosts must have a unique key as the key is what is used to control
the reservation.

Only one key can have a reservation associated with it at a time. In order to
aquire a reservation when another host already has one it is first required to
explicitly abort the reservation. So when multiple hosts race to aquire an
exclusive reservation only one will actually do so.

See the NVMe specification Secton 8.8 for further information on how the
reservations operate. SCSI3-PR reservations operate in very similar manor with
nearly all the differences due simply to the differences between SCSI and NVMe.

#### SCSI3PR Commands

- `sg_persist --out --register-ignore --param-sark=0x<key> <device>`
  - Clear any existing reservations on this host
- `sg_persist --out --register --param-rk=0x<key> --param-sark=0x0 <device>`
  - Register a key for this host
- `sg_persist --out --reserve --param-rk=0x<key> --prout-type=5 <device>`
  - Attempt to reserve the device
- `sg_persist --out --release --param-rk=0x<key> --prout-type=5 <device>`
  - Release an existing reservation
- `sg_persist --in --read-reservation <device>`
  - Read the current reservation status
- `sg_persist --out --preempt-abort --param-rk=0x<old_key> --param-sark=0x<key> --prout-type=5 <device>`
  - Preempt an remove a held reservation from another host.
- `sg_persist --in --read-full-status <device>`
  - Report the keys registered and reservation status.
- `lsblk --nodeps --noheadings --output WWN <device>`
  - List block devices with their WWN
- `sg_verify <device>`
  - Check that a device is a SCSI device.

#### NVMe Commands

- `nvme resv-report <device>`
  - Report the keys registered and reservation status.
- `nvme resv-register <device> --crkey=0x0 --nrkey=0x<key> --rrega=0`
  - Register a key with the device
- `nvme resv-register <device> --crkey=0x<key> --nrkey=0x0 --rrega=1`
  - Clear the registration key
- `nvme resv-acquire <device> --crkey=0x<key> --racqa=0 --rtype=3`
  - Attempt to reserve the device
- `nvme resv-release <device> --crkey=0x<key> --rtype=3 --rrela=0`
  - Release an existing reservation
- `nvme resv-release <device> --crkey=0x<key> --rtype=3 --rrela=1`
  - Preempt an remove a held reservation from another host.

## Tiebreaker Design Overview

To support disk tiebreakers, an alternative daemon called `corosync-qdisk` has
been created to manage persistent reserve capabilities. This daemon will run on
each full cluster host along side corosync similar to how `corosync-qdevice`
works today.

During startup `corosync-qdisk` initializes communication with corosync, and registers
a SCSI/NVMe key with the configured disk. The disk is specified using a WWN
in the `corosync.conf`.

During a tiebreaker scenario where exactly half of the cluster nodes are availble within
the local partition, the daemon on each host races to reserve the disk using
sg_persist or nvme depending on the disk type. The partition which fails in reserving
the disk will rescind the QDisk vote causing the partition to lose quorum.

Once the network partition is resolved, the daemon with the reservation will
release it and additional vote will also be restored. If instead another host is
lost, again the reservation will be released and the remaining hosts will lose
quorum.

**Status During Normal Operation**
```
Quorum information
------------------
Date:             Tue Mar 17 12:59:44 2026
Quorum provider:  corosync_votequorum
Nodes:            4
Node ID:          4
Ring ID:          1.1642
Quorate:          Yes

Votequorum information
----------------------
Expected votes:   5
Highest expected: 5
Total votes:      5
Quorum:           3  
Flags:            Quorate Qdevice

Membership information
----------------------
    Nodeid      Votes    Qdevice Name
         1          1    A,V,NMW cst-wtj-srv-1 (key:ea1b2a62b1dd8f34)
         2          1    A,V,NMW cst-wtj-srv-2 (key:5d09f266e76da9cc)
         3          1    A,V,NMW cst-wtj-srv-3 (key:f719de4ff4de777f)
         4          1    A,V,NMW cst-wtj-srv-4 (key:219dc16f68bbe6c7) (local)
         0          1            QDisk
```

**Status in Tiebreaker Senario**
```
Quorum information
------------------
Date:             Tue Mar 17 13:07:35 2026
Quorum provider:  corosync_votequorum
Nodes:            2
Node ID:          2
Ring ID:          2.164a
Quorate:          Yes

Votequorum information
----------------------
Expected votes:   5
Highest expected: 5
Total votes:      3
Quorum:           3  
Flags:            Quorate Qdevice

Membership information
----------------------
    Nodeid      Votes    Qdevice Name
         2          1    A,V,NMW cst-wtj-srv-2 (key:5d09f266e76da9cc) (local)
         4          1    A,V,NMW cst-wtj-srv-4 (key:219dc16f68bbe6c7)
         0          1            QDisk
```

The flags under Qdevice are the same as for the existing Qdevice

**Key Differences between `corosync-device` and `corosync-qdisk`**

While using a corosync disk tiebreaker there a number of key differences from
network qdevice as follows.

- Quorum disk tiebreaker has a single service running on each cluster hosts,
  while Qdevice has a single service running on each cluster host and an additional
  `corosync-qnetd` arbitrator service running on a QDevice host.

## Persistant Reservation Abstraction

Both the SCSI3-PR based and NVMe based reservation code works by running an
external command, including parsing outputs. The libraries that exist for
working with SCSI devices and NVMe devices work at lower level of abstraction
and for both systems the command line tool contains a significant amount of the
logic involved in working with reservations. So even though the licencing of
the libraries is not an issue they aren't useful directly.

### Abstraction Layer

The NVMe and SCSI reservation are very similar. We abstract the actions we need.

```C
struct persistent_reserve_device {
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

// Constructors
struct persistent_reserve_device *persistent_reserve_device_new(const char *device_path, const char *key_path);
struct persistent_reserve_device *persistent_reserve_device_new_key(const char *device_path, uint64_t ikey);

// Wrappers
static inline pr_dev_err persistent_reserve_device_register_key(struct persistent_reserve_device *handle) {
    return handle->clazz->register_key(handle);
}
static inline pr_dev_err persistent_reserve_device_unregister_key(struct persistent_reserve_device *handle);
static inline pr_dev_err persistent_reserve_device_reserve(struct persistent_reserve_device *handle);
static inline pr_dev_err persistent_reserve_device_release(struct persistent_reserve_device *handle);
static inline int persistent_reserve_device_is_reserved(struct persistent_reserve_device *handle);
static inline const char* persistent_reserve_device_get_name(struct persistent_reserve_device *handle);
static inline uint64_t persistent_reserve_device_get_ikey(struct persistent_reserve_device *handle);
static inline const char* persistent_reserve_device_get_device_id(struct persistent_reserve_device *handle);
static inline pr_dev_err persistent_reserve_device_get_registered_keys(struct persistent_reserve_device *handle, size_t *num_keys, uint64_t **keys);
static inline pr_dev_err persistent_reserve_device_reservation_owner_key(struct persistent_reserve_device *handle, uint64_t *key);
static inline pr_dev_err persistent_reserve_device_abort(struct persistent_reserve_device *handle);

static inline void persistent_reserve_device_free(struct persistent_reserve_device *handle);
```

There are inline wrappers that hide the dispatch via the `clazz` pointer.

Each type of device has it's own `struct` type that has a `struct
persistent_reserve_device` as it's first member, the
`persistent_reserve_device_new` function dispatches to the correct underlying
device creation function.

This allows the rest of the code to ignore differences between NVMe and SCSI,
and could in future allow using a dummy implementation for testing.

### SCSI3-PR and Multipath Devices

The SCSI3-PR layer also deals with detecting multipath devices and using the
`mpathpersist` command in place of sg_persist in that case. The `mpathpersist`
command is a nearly exact drop in replacement for sg_persist on multipath
systems.

## `corosync-qdisk`

The `corosync-qdisk` service is a new service responsible for using a shared
disk to provide tiebreaker capabilities to a Corosync cluster.

- Run as a `systemd` service unit. Must be enabled to be restarted automatically
  after reboot/crash. Starts automatically with the Corosync service.
  - When enabled `systemd` will always try to start `corosync-qdisk` if
    `corosync` is started, unless `corosync-qdisk` fails with an exit status
    that is configured as a hard failure in the unit file.
- Communicates changes in disk state to the Corosync votequorum service engine.
- Disk and registration key are configured within the quorum section of the `corosync.conf`.
- Provides an extra vote similar to QDevice hosts during normal operations, and
  rescinds that extra vote on which ever partition fails to reserve the disk
  during a split-brain.
- If a partition which successfully reserved the disk loses an additional host, resulting
  in less than half the cluster being available in the current partition, the QDisk
  service will enter the 'RESIGNED' state and release its reservation. The remaining
  host will lose quorum, and the cluster will suffer a total outage. The corosync-qdisk
  server will not leave RESIGNED state and not provide tiebreaking until quorum
  is restored (half + 1).
- If a partition which successfully reserved the disk gains an additional host,
  resulting in more than half the cluster being available in the current partition,
  the Qdisk service will return to the IDLE state after releasing the reservation
  and continue to provide tiebreaking functionality.
- If a partition which had previously lost quorum rejoins the quorate partition,
  the QDisk will return to IDLE state.
- If a host that has just booted up is not in a quorate partition, it remain in
  the 'START' state and provide no sevice until after it has joined a quorate
  partition.

### Votequorum and `corosync-qdisk`

Similar to `corosync-qdevice`, `corosync-qdisk` sends a message to
inform votequorum of it's status while it is in a state that should assert a vote.
`corosync-qdisk` continuously asserts this vote to Corosync and will explicitly
stop asserting the vote when it is in the loosing partition, or the partition has too
few hosts to be quorate even with the extra vote. The votequorum service engine uses
the same voting logic for both `corosync-qdevice` and `corosync-qdisk`.

### Configuration

The configuration will be in the existing `corosync.conf` file, in the
`quorum.device.disk` section.

#### Settings

- **`quorum.device.timeout`**: votequorum Q-Device timeout, defaults to 10
  seconds
- **`quorum.device.disk.device`**: The shared disk to use for tiebreaking.
- **`quorum.device.disk.keyfile`**: path to a file to store the registration key
  in. Defaults to `/etc/corosync/pr_reserve_key`
- **`quorum.device.disk.heartbeat`**: interval that the qdisk daemon runs it's
  state machine on. Every 'heartbeat' miliseconds we will wake up and run one
  step of the state machine. Defaults to one quarter of `totem.token`.
- **`quorum.device.disk.timeout`**: time in miliseconds we will remain in a
  state without making progress before we reset to the `IDLE` state. Defaults to
  twice `quorum.device.timeout`

```
quorum {
	provider: corosync_votequorum
	device {
		model: disk
		votes: 1
		disk {
			device: wwn:6001405a0c65fd7089c484db0e3ca405
			keyfile: /etc/corosync/qdiskkey
		}
	}
}
```

#### Configuration considerations

##### heartbeat (Optional)

Within the Corosync votequorum engine, there is a votequorum timeout controlling when
a quorum device's vote times outs and get rescinded.
This timeout is configureable in the corosync.conf as `quorum.device.timeout`,
and defaults to 10 seconds.

Therefore, the heartbeat interval for `corosync-qdisk` must be less than that
votequorum qdevice timeout to avoid spurious loss of quorum during tiebreaker scenarios.

The heartbeat is configurable in corosync.conf, but defaults to half the
corosync token timeout.

##### timeout (Optional)

The timeout parameter configures the minimum time `corosync-qdisk` should wait
before falling back to the `IDLE` state after attempting to reserve the disk, or
receiving a vote. See the following section on the algorithm for more details.
This timeout must be greater than the votequorum qdevice timeout in order to avoid
changing `corosync-qdisk` states in situations where Corosync may experience delays
reporting cluster state across the cluster.

##### device

A path to a Linux device node. Can be normal
`/dev/sdx` or from `/dev/disk/by-id/` or anything else as long as it's a
SCSI3-PR or NVMe disk device node file. Can also be of the form `wwn:<WWN>`
for SCSI devices or `nvme:<Manufacturer>_<Serial Number>_1` for NVMe devices
where the manufacturer string has spaces replaced with `_`. The `_1` on the end
forces reference to the disk, not the controller.

##### keyfile

If `corosync-qdisk` starts with the keyfile empty it will set the key to a 64
bit value obtained by a non-cryptographic hash of the hostname and will save it
to the file. The key created by `corosync-qdisk` will have an eye catcher ('c070')
in the first two bytes to indicate these keys belong to it.

### `corosync-qdisk` Algorithm

#### Start up

1. Check disk and read registration keys that have been registered. Verify that every
   host in the cluster that has a key is registered with the disk to prevent
   misconfiguration pointing different hosts to different disks.
1. Register our key with the shared disk.
1. Notify votequorum that `corosync-qdisk` has started, provide our registration key.
   - corosync then broadcasts a message with the qdisk related status to all
     nodes in the cluster.
     - This happens any time any of the status's change.
1. Start main loop, provided by `libqb`
   - State machine updates on a timer, interval is 'heartbeat' time.
   - Handle polling for messages/replies from corosync.
   - Singal Handlers for normal operation

#### State Machine

- "Heartbeat" - time based check, runs a state machine. We first check the
  quorum state from the local votequorum service engine and verify and read
  disk state.
  - **`START`** state: Initial state. Does not cast a vote.
    - Validate keys of visible nodes against the keys on disk, abort if
      votequorum lists a key that does not exist on disk. This indicates a
      misconfiguration.
    - If corosync votequorum service engine indicates "quorate" change state to `IDLE`
  - **`IDLE`** state: Main state during normal operation, casts a vote.
    - set "timeout" time to a point in the future (configurable)
    - if number of nodes visible needs 1 more to maintain quorum change state to
      `RESERVE_DISK`
    - if the disk shows a reservation and we can see the node holding it via
      votequorum change state to `RECV_VOTE`, if we cannot see the node holding
      the reservation via votequorum change state to `RESIGNED`
    - if our partition is too small to retain quorum even with a tiebreaking
      extra vote change state to `RESIGNED`
    - Otherwise stay in `IDLE` state
  - **`RESERVE_DISK`** state: First state during a tiebreak scenario where
    `corosync-qdisk` races to acquire disk on each host. Casts vote.
    - attempt to grab the disk reservation on the shared disk.
      - if we get the reservation change to `HAVE_QDISK` state, otherwise:
      - if the disk is reserved and we can see the node holding the reservation
        via votequorum change state to `RECV_VOTE`
      - if we have passed the timeout time, change to `RESIGNED` state.
      - otherwise, log failure to obtain reservation, remain in `RESERVE_DISK`
        state to retry
  - **`RECV_VOTE`** state: Some other host in the local partition has acquired
    the disk. Casts vote.
    - if the disk is reserved and we can still see the node holding it via
      votequorum reset timeout time, remain in `RECV_VOTE` state
    - if we have passed the timeout time, return to `IDLE` state.
  - **`HAVE_QDISK`** state: `corosync-qdisk` on the local host has successfully
    reserved the shared disk. Casts vote
    - if a tiebreak is no longer needed, change to state `RELEASE_QDISK`
    - if we still have the reservation and tiebreak is needed, remain in `HAVE_QDISK` state
  - **`RELEASE_QDISK`** state: No longer in a tiebreaker scenario, need to release
    the reservation.
    - release the reservation and transition to `IDLE` state if successful,
      otherwise remain in `RELEASE_QDISK` state
  - **`RESIGNED`** state: Every host in this partition failed to acquire the disk
    reservation, or otherwise cannot become quorate. Does not cast a vote.
    - If host rejoins quorate partition, return to IDLE state.

### Shutdown

#### Normal Shutdown

Normal shutdown occurs when `corosync-qdisk` gets a SIGTERM from systemd. This
will occur if the cluster is shutdown, the host is gracefully rebooted, or if
corosync crashes in some cases.

The `corosync-qdisk` daemon implements a signal handler to catch and ensure the
following actions are taken.

1. Release any reservation we hold.
2. Unregister our key from the disk.

#### Abnormal Shutdown

During abnormal shut downs such as a system crash or SIGSEGV, `corosync-qdisk`
will be unable to reliably release its reservations or registrations from the disk.

In these situations, `corosync-qdisk` will simply attempt to dump diagnostics
and crash. See the "scenarios" section below for details on how the cluster handles
orphaned reservations.

### Expected behaviours

#### When tiebreaking is required

- Nodes in loosing partition(s) will either be fenced and restart or enter
  `RESIGNED` state until quorum is restored. If fenced, when the
  `corosync-qdisk` daemon starts it should remain in the `START` state until
  quorum is restored.
- Nodes in a winning partition should enter either the `HAVE_QDISK` or
  `RECV_VOTE` state. Only the node holding the disk reservation should every be
  in the `HAVE_QDISK` state.

#### When tiebreaking is not required

- Nodes in a partition too small to support quorum even with the extra vote from
  a the qdisk should always transition to `RESIGNED` state or be fenced. If
  fenced, when the `corosync-qdisk` daemon starts it should remain in the
  `START` state until quorum is restored.
- Nodes in a partition larger than exactly half the cluster should always either
  remain in. or transition to, the `IDLE` state. This includes normal cluster
  operation where there is only one partition with the entire cluster in it.
