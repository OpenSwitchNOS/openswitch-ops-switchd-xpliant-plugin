/*
 * Copyright (C) 2016, Cavium, Inc.
 * All Rights Reserved.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License"); you may
 *   not use this file except in compliance with the License. You may obtain
 *   a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *   WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 *   License for the specific language governing permissions and limitations
 *   under the License.
 *
 * File: ops-xp-mac-learning.h
 *
 * Purpose: This file provides public definitions for OpenSwitch MAC learning
 *          related application code for the Cavium/XPliant SDK.
 */

#ifndef OPS_XP_MAC_LEARNING_H
#define OPS_XP_MAC_LEARNING_H 1

#include <time.h>
#include "hmap.h"
#include "list.h"
#include "dynamic-string.h"
#include "latch.h"
#include "ovs-atomic.h"
#include "ovs-thread.h"
#include "timeval.h"
#include "timer.h"
#include "mac-learning-plugin.h"
#include "plugin-extensions.h"

#include "ops-xp-ofproto-provider.h"
#include "ops-xp-vlan.h"
#include "openXpsAging.h"
#include "openXpsFdb.h"

struct xp_mac_learning;

/* Default maximum size of a MAC learning table, in entries. */
#define XP_ML_DEFAULT_SIZE (1024 * 32)

/* Time, in seconds, before expiring a xp_mac_entry due to inactivity. */
#define XP_ML_ENTRY_DEFAULT_IDLE_TIME 300

/* Time, in seconds, to lock an entry updated by a gratuitous ARP to avoid
 * relearning based on a reflection from a bond slave. */
#define XP_ML_GRAT_ARP_LOCK_TIME 5

/* The buffers are defined as 2 in order to allow simultaneous read access to
 * bridge.c and ops-xp-mac-learning.c code from different threads.
 */
#define XP_ML_MLEARN_MAX_BUFFERS   2

/* A MAC learning table entry.
 * Guarded by owning 'xp_mac_learning''s rwlock */
struct xp_mac_entry {
    struct hmap_node hmap_node; /* Node in a xp_mac_learning hmap. */
    time_t grat_arp_lock;       /* Gratuitous ARP lock expiration time. */
    xpsFdbEntry_t xps_fdb_entry;

    /* The following are marked guarded to prevent users from iterating over or
     * accessing a xp_mac_entry without holding the parent xp_mac_learning rwlock. */
    /* Learned port. */
    union {
        void *p;
        ofp_port_t ofp_port;
    } port OVS_GUARDED;
};

/* MAC learning table. */
struct xp_mac_learning {
    struct hmap table;              /* Learning table. */
    unsigned long *flood_vlans;     /* Bitmap of learning disabled VLANs. */
    unsigned int idle_time;         /* Max age before deleting an entry. */
    struct timer idle_timer;
    size_t max_entries;             /* Max number of learned MACs. */
    struct ovs_refcount ref_cnt;
    struct ovs_rwlock rwlock;
#ifdef OPS_XP_ML_EVENT_PROCESSING
    pthread_t ml_thread;         /* ML Thread ID. */
    struct latch exit_latch;     /* Tells child threads to exit. */
    struct latch event_latch;    /* Events receiving pipe of child thread. */
#endif /* OPS_XP_ML_EVENT_PROCESSING */
    struct xpliant_dev *xpdev;
    /* Tables which store mac learning events destined for main
     * processing in OPS mac-learning-plugin. */
    struct mlearn_hmap mlearn_event_tables[XP_ML_MLEARN_MAX_BUFFERS];
    /* Index of a mlearn table which is currently in use. */
    int curr_mlearn_table_in_use;
    struct timer mlearn_timer;
    struct mac_learning_plugin_interface *plugin_interface;
};

typedef enum {
    XP_ML_LEARNING_EVENT,
    XP_ML_AGING_EVENT,
    XP_ML_PORT_DOWN_EVENT,
    XP_ML_VLAN_REMOVED_EVENT
} xp_ml_event_type;

struct xp_ml_learning_data {
    xpsFdbEntry_t xps_fdb_entry;
    uint16_t reasonCode;
};

struct xp_ml_event {
    xp_ml_event_type type;
    union xp_ml_event_data {
        struct xp_ml_learning_data learning_data;
        uint32_t index;
        xpsInterfaceId_t  intfId;
        xpsVlan_t vlan;
    } data;
};

/* Sets a gratuitous ARP lock on 'mac' that will expire in
 * XP_MAC_GRAT_ARP_LOCK_TIME seconds. */
static inline void
ops_xp_mac_entry_set_grat_arp_lock(struct xp_mac_entry *mac)
{
    mac->grat_arp_lock = time_now() + XP_ML_GRAT_ARP_LOCK_TIME;
}

/* Returns true if a gratuitous ARP lock is in effect on 'mac', false if none
 * has ever been asserted or if it has expired. */
static inline bool
ops_xp_mac_entry_is_grat_arp_locked(const struct xp_mac_entry *mac)
{
    return time_now() < mac->grat_arp_lock;
}

struct xp_mac_learning *ops_xp_mac_learning_create(struct xpliant_dev *xpdev,
                                                   unsigned int idle_time);
struct xp_mac_learning *ops_xp_mac_learning_ref(const struct xp_mac_learning *);
void ops_xp_mac_learning_unref(struct xp_mac_learning *);

int ops_xp_mac_learning_set_idle_time(struct xp_mac_learning *ml,
                                      unsigned int idle_time)
    OVS_REQ_WRLOCK(ml->rwlock);

void ops_xp_mac_learning_set_max_entries(struct xp_mac_learning *ml,
                                         size_t max_entries)
    OVS_REQ_WRLOCK(ml->rwlock);

bool ops_xp_mac_learning_may_learn(const struct xp_mac_learning *ml,
                                   const macAddr_t src_mac, xpsVlan_t vlan)
    OVS_REQ_RDLOCK(ml->rwlock);

int ops_xp_mac_learning_insert(struct xp_mac_learning *ml,
                               struct xp_mac_entry *e);
    OVS_REQ_WRLOCK(ml->rwlock);

int ops_xp_mac_learning_learn(struct xp_mac_learning *ml,
                              struct xp_ml_learning_data *data)
    OVS_REQ_WRLOCK(ml->rwlock);

int ops_xp_mac_learning_age_by_index(struct xp_mac_learning *ml, uint32_t index)
    OVS_REQ_WRLOCK(ml->rwlock);

int ops_xp_mac_learning_age_by_vlan_and_mac(struct xp_mac_learning *ml,
                                            xpsVlan_t vlan,
                                            macAddr_t macAddr)
    OVS_REQ_WRLOCK(ml->rwlock);

struct xp_mac_entry *ops_xp_mac_learning_lookup(const struct xp_mac_learning *ml,
                                                uint32_t hash)
    OVS_REQ_RDLOCK(ml->rwlock);

struct xp_mac_entry *ops_xp_mac_learning_lookup_by_vlan_and_mac(
                                    const struct xp_mac_learning *ml,
                                    xpsVlan_t vlan_id, macAddr_t macAddr)
    OVS_REQ_RDLOCK(ml->rwlock);

int ops_xp_mac_learning_expire(struct xp_mac_learning *ml,
                               struct xp_mac_entry *e)
    OVS_REQ_WRLOCK(ml->rwlock);

void ops_xp_mac_learning_flush(struct xp_mac_learning *ml, bool dynamic_only)
    OVS_REQ_WRLOCK(ml->rwlock);

int ops_xp_mac_learning_flush_vlan(struct xp_mac_learning *ml,
                                   xpsVlan_t vlan_id)
    OVS_REQ_WRLOCK(ml->rwlock);

int ops_xp_mac_learning_flush_intfId(struct xp_mac_learning *ml,
                                     xpsInterfaceId_t intfId,
                                     bool dynamic_only)
    OVS_REQ_WRLOCK(ml->rwlock);

int ops_xp_mac_learning_flush_vlan_intf(struct xp_mac_learning *ml,
                                        xpsVlan_t vlan_id,
                                        xpsInterfaceId_t intf_id)
    OVS_REQ_WRLOCK(ml->rwlock);

int ops_xp_mac_learning_process_vlan_removed(struct xp_mac_learning *ml,
                                             xpsVlan_t vlan)
    OVS_REQ_WRLOCK(ml->rwlock);

XP_STATUS ops_xp_mac_learning_on_learning(xpsDevice_t devId, uint32_t ingressVif,
                                          uint32_t reasonCode, uint32_t bdId,
                                          void *buf, uint16_t bufSize,
                                          void *userData);
void ops_xp_mac_learning_on_aging(xpsDevice_t devId, uint32_t *index,
                                  void *userData);
int ops_xp_mac_learning_on_vlan_removed(struct xp_mac_learning *ml,
                                        xpsVlan_t vlanId);
int ops_xp_mac_learning_on_port_down(struct xp_mac_learning *ml,
                                     xpsInterfaceId_t intfId);
void ops_xp_mac_learning_on_vni_removed(struct xp_mac_learning *ml,
                                        xpsVlan_t vlan, uint32_t vni,
                                        xpsInterfaceId_t if_id);
void ops_xp_mac_learning_dump_table(struct xp_mac_learning *ml,
                                    struct ds *d_str);
bool ops_xp_ml_addr_is_multicast(const macAddr_t mac, bool normal_order);

void ops_xp_mac_learning_on_idle_timer_expired(struct xp_mac_learning *ml);

void ops_xp_mac_learning_on_mlearn_timer_expired(struct xp_mac_learning *ml);

int ops_xp_mac_learning_hmap_get(struct mlearn_hmap **mhmap);

#endif /* ops-xp-mac-learning.h */
