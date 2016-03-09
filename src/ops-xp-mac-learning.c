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
 * File: ops-xp-mac-learning.c
 *
 * Purpose: This file contains OpenSwitch MAC learning related application code
 *          for the Cavium/XPliant SDK.
 */

#include <config.h>
#include "ops-xp-mac-learning.h"

#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "bitmap.h"
#include "coverage.h"
#include "hash.h"
#include "list.h"
#include "poll-loop.h"
#include "timeval.h"
#include "unaligned.h"
#include "util.h"
#include <openvswitch/vlog.h>
#include <linux/if_ether.h>
#include "unixctl.h"

#include "ops-xp-netdev.h"
#include "ops-xp-ofproto-provider.h"
#include "ops-xp-util.h"
#include "ops-xp-mac-learning.h"
#include "ops-xp-vlan.h"
#include "openXpsReasonCodeTable.h"

VLOG_DEFINE_THIS_MODULE(xp_mac_learning);

/*struct xp_mac_learning* g_xp_ml = NULL;*/
static struct vlog_rate_limit ml_rl = VLOG_RATE_LIMIT_INIT(5, 20);

static void *mac_learning_events_handler(void *arg);
static void unixctl_init(void);

bool
ops_xp_ml_addr_is_multicast(const macAddr_t mac, bool normal_order)
{
    /* The function is used for MAC checks in normal byte order or reverse one
     * used in XDK APIs.*/
    if (normal_order) {
        return mac[0] & 1;
    } else {
        return mac[5] & 1;
    }
}

static unsigned int
normalize_idle_time(unsigned int idle_time)
{
    return (idle_time < 15 ? 15
            : idle_time > 3600 ? 3600
            : idle_time);
}

/* Creates and returns a new MAC learning table with an initial MAC aging
 * timeout of 'idle_time' seconds and an initial maximum of XP_MAC_DEFAULT_MAX
 * entries. */
struct xp_mac_learning *
ops_xp_mac_learning_create(xpsDevice_t dev_id, struct ofproto_xpliant *ofproto,
                           unsigned int idle_time)
{
    struct xp_mac_learning *ml = NULL;
    XP_STATUS status = XP_NO_ERR;

    ovs_assert(ofproto);

    ml = xmalloc(sizeof *ml);
    hmap_init(&ml->table);
    ml->max_entries = XP_ML_DEFAULT_SIZE;
    ml->devId = dev_id;
    ml->ofproto = ofproto;
    ml->idle_time = normalize_idle_time(idle_time);
    ml->flood_vlans = NULL;

    ovs_refcount_init(&ml->ref_cnt);
    ovs_rwlock_init(&ml->rwlock);
    latch_init(&ml->exit_latch);   
    latch_init(&ml->event_latch);

    /* Start event handling thread */
    ml->ml_thread = ovs_thread_create("ops-xp-ml-handler",
                                      mac_learning_events_handler, ml);

    VLOG_INFO("XPliant device's FDB events processing thread started");

    status = xpsFdbRegisterLearn(ml->devId,
                                 ops_xp_mac_learning_on_learning, ml);
    if (status != XP_NO_ERR) {
        /* TODO: abort can be changed with some log if ofproto init fail
         * shouldn't cause crash of OVS */
        ovs_abort(0, "xp_mac_learning_create failed due to inability"
                     "of fdb learning handler registration");
    }

    status = xpsFdbRegisterAgingHandler(ml->devId,
                                        ops_xp_mac_learning_on_aging, ml);
    if (status != XP_NO_ERR) {
        /* TODO: abort can be changed with some log if ofproto init fail 
         *shouldn't cause crash of OVS */
        ovs_abort(0, "xp_mac_learning_create failed due to inability"
                     "of fdb aging handler registration");
    }

    return ml;
}

struct xp_mac_learning *
ops_xp_mac_learning_ref(const struct xp_mac_learning *ml_)
{
    struct xp_mac_learning *ml = CONST_CAST(struct xp_mac_learning *, ml_);
    if (ml) {
        ovs_refcount_ref(&ml->ref_cnt);
    }
    return ml;
}

/* Unreferences (and possibly destroys) MAC learning table 'ml'. */
void
ops_xp_mac_learning_unref(struct xp_mac_learning *ml)
{
    XP_STATUS status = XP_NO_ERR;

    if (ml && ovs_refcount_unref(&ml->ref_cnt) == 1) {

        status = xpsFdbUnregisterLearnHandler(ml->devId);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Could not deregister l2 learning handler. Status: %d",
                     status);
        }

        status = xpsFdbUnregisterAgingHandler(ml->devId);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Could not deregister l2 aging handler. Status: %d",
                     status);
        }

        latch_set(&ml->exit_latch);
        xpthread_join(ml->ml_thread, NULL);

        ops_xp_mac_learning_flush(ml);
        hmap_destroy(&ml->table);

        latch_destroy(&ml->exit_latch);
        latch_destroy(&ml->event_latch);

        bitmap_free(ml->flood_vlans);

        ovs_rwlock_destroy(&ml->rwlock);

        free(ml);
    }
}

/* Changes the MAC aging timeout of 'ml' to 'idle_time' seconds. */
void
ops_xp_mac_learning_set_idle_time(struct xp_mac_learning *ml,
                                  unsigned int idle_time)
{
    idle_time = normalize_idle_time(idle_time);
    ml->idle_time = idle_time;
}

/* Sets the maximum number of entries in 'ml' to 'max_entries', adjusting it
 * to be within a reasonable range. */
void
ops_xp_mac_learning_set_max_entries(struct xp_mac_learning *ml,
                                    size_t max_entries)
{
    ml->max_entries = (max_entries < 10 ? 10
                       : max_entries > 1000 * 1000 ? 1000 * 1000
                       : max_entries);
}

/* Returns true if 'src_mac' may be learned on 'vlan' for 'ml'.
 * Returns false if 'ml' is NULL, if src_mac is not valid for learning, or if
 * 'vlan' is configured on 'ml' to flood all packets. */
bool
ops_xp_mac_learning_may_learn(const struct xp_mac_learning *ml,
                              const macAddr_t src_mac, xpsVlan_t vlan)
{
    bool is_learning = false;

    ovs_assert(ml);

    ovs_rwlock_rdlock(&ml->ofproto->vlan_mgr->rwlock);
    is_learning = ops_xp_vlan_is_learning(ml->ofproto->vlan_mgr, vlan);
    ovs_rwlock_unlock(&ml->ofproto->vlan_mgr->rwlock);

    return (is_learning && !ops_xp_ml_addr_is_multicast(src_mac, false));
}

/* Inserts a new entry into mac learning table.
 * In case of fail - releases memory allocated for the entry and
 * removes correspondent entry from the hardware table. */
int
ops_xp_mac_learning_insert(struct xp_mac_learning *ml, struct xp_mac_entry *e)
{
    XP_STATUS status = XP_NO_ERR;
    uint32_t index = 0;
    uint32_t reHashIndex = 0;

    ovs_assert(ml);
    ovs_assert(e);

    if (hmap_count(&ml->table) >= ml->max_entries) {
        VLOG_WARN_RL(&ml_rl, "%s: Unable to insert entry for VLAN %d "
                             "and MAC: " XP_ETH_ADDR_FMT
                             " to the software FDB table. The table is full\n",
                     __FUNCTION__, e->xps_fdb_entry.vlanId, 
                     XP_ETH_ADDR_ARGS(e->xps_fdb_entry.macAddr));
        free(e);
        return EPERM;
    }

    status = xpsFdbAddEntry(ml->devId, &e->xps_fdb_entry, &index, &reHashIndex);
    if (status != XP_NO_ERR)
    {
         VLOG_ERR("%s: Unable to install entry with VLAN: %d, "
                  "MAC: " XP_ETH_ADDR_FMT
                  " in the hardware FDB table. Reason: %d\n", 
                   __FUNCTION__, e->xps_fdb_entry.vlanId, 
                   XP_ETH_ADDR_ARGS(e->xps_fdb_entry.macAddr), status);
         free(e);
         return EPERM;
    }

    if (index != reHashIndex) {
        /*  The new entry in the hardware has been put to a place
         *  occupied by another one(pointed by index) which has been
         *  previously relocated to a different place
         *  and now is pointed by reHashIndex.
         *  According to this need to make corresponding relocations in
         *  our FDB table. */

        struct xp_mac_entry *old_e = ops_xp_mac_learning_lookup(ml, index);

        if (old_e) {
            hmap_remove(&ml->table, &old_e->hmap_node);
            hmap_insert(&ml->table, &old_e->hmap_node, reHashIndex);
        } else {
            VLOG_ERR("%s: Unable to lookup entry for VLAN %d and "
                      "MAC: " XP_ETH_ADDR_FMT
                      " with index 0x%x from the software FDB table.\n", 
                      __FUNCTION__, e->xps_fdb_entry.vlanId,
                      XP_ETH_ADDR_ARGS(e->xps_fdb_entry.macAddr), index);

            status = xpsFdbRemoveEntry(ml->devId, &e->xps_fdb_entry);
            if (status != XP_NO_ERR) {
                VLOG_ERR("%s: Unable to remove entry for VLAN %d and "
                         "MAC: " XP_ETH_ADDR_FMT
                         " with index 0x%x from the hardware FDB table. "
                         "Reason: %d\n",
                         __FUNCTION__,
                         e->xps_fdb_entry.vlanId,
                         XP_ETH_ADDR_ARGS(e->xps_fdb_entry.macAddr),
                         index, status);
            }

            status = xpsFdbRemoveEntryByIndex(ml->devId, reHashIndex);
            if (status != XP_NO_ERR) {
                VLOG_ERR("%s: Unable to remove entry "
                         "with index 0x%x from the hardware FDB table. "
                         "Reason: %d\n", __FUNCTION__, index, status);
            }

            free(e);
            return ENOENT;
        }
    }

    hmap_insert(&ml->table, &e->hmap_node, index);
    VLOG_INFO_RL(&ml_rl, "Inserted new entry into ML table: VLAN %d, "
                         "MAC: " XP_ETH_ADDR_FMT ", Intf ID: %u, index 0x%lx\n",
                 e->xps_fdb_entry.vlanId, 
                 XP_ETH_ADDR_ARGS(e->xps_fdb_entry.macAddr),
                 e->xps_fdb_entry.intfId,
                 e->hmap_node.hash);

    return 0;
}

struct xp_mac_entry *
ops_xp_mac_learning_lookup(const struct xp_mac_learning *ml,
                           uint32_t hash)
{
    struct xp_mac_entry *e = NULL;
    ovs_assert(ml);
    e = (struct xp_mac_entry*)hmap_first_with_hash(&ml->table, hash);
    return e;
}

struct xp_mac_entry *
ops_xp_mac_learning_lookup_by_vlan_and_mac(const struct xp_mac_learning *ml,
                                           xpsVlan_t vlan_id, macAddr_t macAddr)
{
    struct xp_mac_entry *e = NULL;
    ovs_assert(ml);

    HMAP_FOR_EACH (e, hmap_node, &ml->table) {
        if (memcmp(e->xps_fdb_entry.macAddr, macAddr, ETH_ADDR_LEN) &&
            (e->xps_fdb_entry.vlanId == vlan_id)) {

            return e;
        }
    }

    return NULL;
}

/* Expires 'e' from the 'ml' hash table and from the hardware table. */
int
ops_xp_mac_learning_expire(struct xp_mac_learning *ml, struct xp_mac_entry *e)
{
    XP_STATUS status = XP_NO_ERR;

    status = xpsFdbRemoveEntryByIndex(ml->devId, e->hmap_node.hash);
    if (status != XP_NO_ERR) {
         VLOG_ERR("%s: Unable to remove entry for VLAN %d and "
                  "MAC: " XP_ETH_ADDR_FMT
                  " from the hardware FDB table. Reason: %d\n", 
                  __FUNCTION__, e->xps_fdb_entry.vlanId, 
                  XP_ETH_ADDR_ARGS(e->xps_fdb_entry.macAddr), status);

         return EPERM;
    }

    hmap_remove(&ml->table, &e->hmap_node);
    free(e);

    return 0;
}

/* Expires all the mac-learning entries in 'ml'.  If not NULL, the tags in 'ml'
 * are added to 'tags'.  Otherwise the tags in 'ml' are discarded.  The client
 * is responsible for revalidating any flows that depend on 'ml', if
 * necessary. */
void
ops_xp_mac_learning_flush(struct xp_mac_learning *ml)
{
    struct xp_mac_entry *e = NULL;
    struct xp_mac_entry *next = NULL;

    ovs_assert(ml);

    HMAP_FOR_EACH_SAFE (e, next, hmap_node, &ml->table) {
        ops_xp_mac_learning_expire(ml, e);
    }
}

/* Installs entry into hardware FDB table and then in the software table. 
 * After that releases the memory allocated for the members of data. */

int
ops_xp_mac_learning_learn(struct xp_mac_learning *ml,
                          struct xp_ml_learning_data *data)
{
    XP_STATUS status = XP_NO_ERR;
    int retVal = 0;

    ovs_assert(ml);
    ovs_assert(data);

    if (!ops_xp_mac_learning_may_learn(ml, data->xps_fdb_entry.macAddr,
                                       data->xps_fdb_entry.vlanId)) {
        VLOG_WARN_RL(&ml_rl, "%s: Either learning is disabled on the VLAN %u"
                             " or wrong MAC: "XP_ETH_ADDR_FMT" has to be learned."
                             " Skipping.",
                     __FUNCTION__, data->xps_fdb_entry.vlanId,
                     XP_ETH_ADDR_ARGS(data->xps_fdb_entry.macAddr));
        return EPERM;
    }

    data->xps_fdb_entry.pktCmd = XP_PKTCMD_FWD;
    data->xps_fdb_entry.isControl = 0;

    VLOG_DBG_RL(&ml_rl, "%s, New XP_ML_LEARNING_EVENT, MAC: " XP_ETH_ADDR_FMT
                        " VLAN: %u, IVIF: %d reasonCode: %d, serviceInstId: %d",
                __FUNCTION__, XP_ETH_ADDR_ARGS(data->xps_fdb_entry.macAddr),
                data->xps_fdb_entry.vlanId, data->xps_fdb_entry.intfId,
                data->reasonCode, data->xps_fdb_entry.serviceInstId);

    switch (data->reasonCode) {
    case XP_BRIDGE_MAC_SA_NEW:
    case XP_BRIDGE_RC_IVIF_SA_MISS:
        {
            uint32_t index = 0;

            /* Lookup if the VLAN and MAC pair exists */
            status = xpsFdbFindEntry(ml->devId, &data->xps_fdb_entry, &index);
            if (status == XP_ERR_PM_HWLOOKUP_FAIL) {
                /* Entry not found. Add new entry to FDB */
                struct xp_mac_entry *e = xmalloc(sizeof(*e));
                memcpy(&e->xps_fdb_entry, &data->xps_fdb_entry,
                       sizeof(e->xps_fdb_entry));
                e->port.p = NULL;

                return ops_xp_mac_learning_insert(ml, e);
            } else if (status != XP_NO_ERR) {
                /* Other error happen */
                VLOG_ERR("%s: Unable to get entry index for VLAN %d, "
                         "MAC: " XP_ETH_ADDR_FMT
                         " from the hardware FDB table. Reason: %d\n",
                         __FUNCTION__, data->xps_fdb_entry.vlanId,
                         XP_ETH_ADDR_ARGS(data->xps_fdb_entry.macAddr),
                         status);
                return ENOENT;
            } else {
                /* Entry already exist */
                return EEXIST;
            }
        }
        break;

    case XP_BRIDGE_MAC_SA_MOVE:
        {
            struct xp_mac_entry *upd_e = NULL;
            uint32_t index = 0;

            /* Lookup the original entry */
            status = xpsFdbFindEntry(ml->devId, &data->xps_fdb_entry, &index);
            if (status != XP_NO_ERR) {
                VLOG_ERR("%s: Unable to get entry index for VLAN %d, "
                         "MAC: " XP_ETH_ADDR_FMT
                         " from the hardware FDB table. Reason: %d\n",
                         __FUNCTION__, data->xps_fdb_entry.vlanId,
                         XP_ETH_ADDR_ARGS(data->xps_fdb_entry.macAddr),
                         status);
                return ENOENT;
            }

            /* Update the entry */
            upd_e = ops_xp_mac_learning_lookup(ml, index);

            if (!upd_e) {
                VLOG_ERR("%s: Unable to lookup entry for VLAN %d and "
                         "MAC: " XP_ETH_ADDR_FMT
                         " with index 0x%x from the software FDB table.\n",
                         __FUNCTION__,
                         data->xps_fdb_entry.vlanId,
                         XP_ETH_ADDR_ARGS(data->xps_fdb_entry.macAddr), index);

                status = xpsFdbRemoveEntry(ml->devId, &data->xps_fdb_entry);
                if (status != XP_NO_ERR) {
                     VLOG_ERR("%s: Unable to remove entry for VLAN %d and "
                              "MAC: " XP_ETH_ADDR_FMT
                              " with index 0x%x from the hardware "
                              "FDB table. Reason: %d\n",
                              __FUNCTION__,
                              data->xps_fdb_entry.vlanId,
                              XP_ETH_ADDR_ARGS(data->xps_fdb_entry.macAddr),
                              index, status);
                }

                return EPERM;
            }

            upd_e->xps_fdb_entry.intfId = data->xps_fdb_entry.intfId;

            status = xpsFdbWriteEntry(ml->devId, index, &upd_e->xps_fdb_entry);
            if (status != XP_NO_ERR) {
                VLOG_ERR("%s: Unable to update entry for VLAN %d and "
                         "MAC: " XP_ETH_ADDR_FMT
                         " with index 0x%x in the hardware FDB table. "
                         "Reason: %d\n",
                          __FUNCTION__,
                         data->xps_fdb_entry.vlanId,
                         XP_ETH_ADDR_ARGS(data->xps_fdb_entry.macAddr),
                         index, status);

                ops_xp_mac_learning_expire(ml, upd_e);
            }
        }
        break;

    default:
        break;
    } /* switch (data->xphHdr->reasonCode) */

    VLOG_DBG_RL(&ml_rl, "%s, New XP_ML_LEARNING_EVENT handled", __FUNCTION__);

    return 0;
}

/* Removes entry from the software and hardware FDB tables using its index. */
int
ops_xp_mac_learning_age_by_index(struct xp_mac_learning *ml, uint32_t index)
{
    XP_STATUS status = XP_NO_ERR;
    struct xp_mac_entry *e = NULL;

    ovs_assert(ml);

    e = ops_xp_mac_learning_lookup(ml, index);

    if (e) {
        return ops_xp_mac_learning_expire(ml, e);
    }

    return 0;
}

/* Removes entry from the software and hardware FDB tables using
 * VLAN and MAC. */
int
ops_xp_mac_learning_age_by_vlan_and_mac(struct xp_mac_learning *ml,
                                        xpsVlan_t vlan, macAddr_t macAddr)
{
    struct xp_mac_entry *e = NULL;

    ovs_assert(ml);

    e = ops_xp_mac_learning_lookup_by_vlan_and_mac(ml, vlan, macAddr);

    if (e) {
        return ops_xp_mac_learning_expire(ml, e);
    } else {
        VLOG_WARN_RL(&ml_rl, "%s: No entry with VLAN %d "
                             "and MAC: " XP_ETH_ADDR_FMT
                             " found in FDB\n",
                     __FUNCTION__, vlan,
                     XP_ETH_ADDR_ARGS(macAddr));
        return EPERM;
    }

    return 0;
}

/* Removes all dynamic entries associated with this VIF (port, bond, etc)
 * from the software and hardware FDB tables. */
int
ops_xp_mac_learning_flush_intfId(struct xp_mac_learning *ml,
                                 xpsInterfaceId_t  intfId)
{
    struct xp_mac_entry *e = NULL;
    struct xp_mac_entry *next = NULL;

    ovs_assert(ml);

    HMAP_FOR_EACH_SAFE (e, next, hmap_node, &ml->table) {
        if (e->xps_fdb_entry.intfId == intfId) {
            ops_xp_mac_learning_expire(ml, e);
        }
    }

    return 0;
}

/* Processes interface down event.
 * Removes all entries with this inerface intfId from the software and
 * hardware FDB tables. */
int
ops_xp_mac_learning_process_port_down(struct xp_mac_learning *ml,
                                      xpsInterfaceId_t  intfId)
{
    return ops_xp_mac_learning_flush_intfId(ml, intfId);
}


/* Removes all dynamic entries associated with this VLAN
 * from the software and hardware FDB tables. */
int
ops_xp_mac_learning_flush_vlan(struct xp_mac_learning *ml, xpsVlan_t vlan_id)
{
    struct xp_mac_entry *e = NULL;
    struct xp_mac_entry *next = NULL;

    ovs_assert(ml);

    HMAP_FOR_EACH_SAFE (e, next, hmap_node, &ml->table) {
        if (e->xps_fdb_entry.vlanId == vlan_id) {
            ops_xp_mac_learning_expire(ml, e);
        }
    }

    return 0;
}

/* Processes vlan removed event.
 * Removes all entries with this vlan id from the software and
 * hardware FDB tables. */
int
ops_xp_mac_learning_process_vlan_removed(struct xp_mac_learning *ml,
                                         xpsVlan_t vlan)
{
    return ops_xp_mac_learning_flush_vlan(ml, vlan);
}

/* This handler thread receives incoming learning events
 * of different types and handles them correspondingly. */
static void *
mac_learning_events_handler(void *arg)
{
    struct xp_mac_learning *ml = arg;
    struct xp_ml_event event;
    int retval = 0;

    ovs_assert(ml);

    /* Receiving and processing events loop. */
    while (!latch_is_set(&ml->exit_latch)) {

        latch_wait(&ml->exit_latch);
        latch_wait(&ml->event_latch);
        poll_block();

        do {
            retval = read(ml->event_latch.fds[0], &event, sizeof(event));
        } while (retval < 0 && errno == EINTR);

        if (retval < 0) {

            if (errno != EAGAIN) {
                VLOG_WARN_RL(&ml_rl, "Failed to receive event. %s.",
                             ovs_strerror(errno));
            }

            continue;

        } else if (retval != sizeof(event)) {
              VLOG_WARN("Incomplete event received");
              continue;
        }

        /*VLOG_DBG_RL(&ml_rl, "%s, Received event: %d Handling...", 
                    __FUNCTION__, event.type);*/

        switch (event.type) {
        case XP_ML_LEARNING_EVENT:
            ovs_rwlock_wrlock(&ml->rwlock);
            ops_xp_mac_learning_learn(ml, &event.data.learning_data);
            ovs_rwlock_unlock(&ml->rwlock);
            break;

        case XP_ML_AGING_EVENT:
            ovs_rwlock_wrlock(&ml->rwlock);
            ops_xp_mac_learning_age_by_index(ml, event.data.index);
            ovs_rwlock_unlock(&ml->rwlock);
            break;

        case XP_ML_PORT_DOWN_EVENT:
            ovs_rwlock_wrlock(&ml->rwlock);
            ops_xp_mac_learning_process_port_down(ml, event.data.intfId);
            ovs_rwlock_unlock(&ml->rwlock);
            break;

        case XP_ML_VLAN_REMOVED_EVENT:
            ovs_rwlock_wrlock(&ml->rwlock);
            ops_xp_mac_learning_process_vlan_removed(ml, event.data.vlan);
            ovs_rwlock_unlock(&ml->rwlock);
            break;

        default:
            break;
        }
    } /* while (!latch_is_set(&ml->exit_latch)) */

    VLOG_INFO("XPliant device's FDB events processing thread finished");
}

/* Learning event handler registered in XDK.
 * Sends xp_ml_event event to software FDB task. */
XP_STATUS
ops_xp_mac_learning_on_learning(xpsDevice_t devId OVS_UNUSED,
                                uint32_t ingressVif,
                                uint32_t reasonCode, uint32_t bdId,
                                void *buf, uint16_t bufSize,
                                void *userData)
{
    struct xp_mac_learning *ml = (struct xp_mac_learning *)userData;
    uint8_t *srcMacAddr = (uint8_t *)(buf + XP_MAC_ADDR_LEN);
    int retval = 0;
    struct xp_ml_event event;

    ovs_assert(ml);

    if (bufSize < sizeof(struct ethhdr)) {
        VLOG_ERR("%s, To short learning packet received on "
                 " interface %u.", __FUNCTION__, ingressVif);
        return XP_ERR_INVALID_DATA;
    }

    memset(&event, 0, sizeof(event));

    event.type = XP_ML_LEARNING_EVENT;

    ops_xp_mac_copy_and_reverse(event.data.learning_data.xps_fdb_entry.macAddr,
                                srcMacAddr);

    event.data.learning_data.xps_fdb_entry.vlanId = bdId;
    event.data.learning_data.xps_fdb_entry.intfId = ingressVif;
    event.data.learning_data.xps_fdb_entry.isStatic = 0;
    event.data.learning_data.reasonCode = reasonCode;

    VLOG_DBG_RL(&ml_rl, "Sending XP_ML_LEARNING_EVENT to learning handler");

    /* send event to FDB task */
    retval = write(ml->event_latch.fds[1], &event, sizeof(event));

    if ((retval < 0) || (retval != sizeof(event))) {
        VLOG_WARN_RL(&ml_rl, "failed to send event (%s)", 
                     (retval < 0) ? ovs_strerror(errno) :
                     "incomplete data sent");
        return XP_ERR_SOCKET_SEND;
    }

    return XP_NO_ERR;
}

/* Aging event handler registered in XDK.
 * Allocates new aging xp_ml_event and sends it to sowftware FDB task. */
void
ops_xp_mac_learning_on_aging(xpsDevice_t devId, uint32_t *index, void *userData)
{
    struct xp_mac_learning *ml = userData;
    struct xp_ml_event event;
    int retval = 0;

    ovs_assert(ml);

    event.type = XP_ML_AGING_EVENT;
    event.data.index = *index;

    /* send event to FDB task */
    retval = write(ml->event_latch.fds[1], &event, sizeof(event));

    if ((retval < 0) || (retval != sizeof(event))) {
        VLOG_WARN_RL(&ml_rl, "failed to send event (%s)", 
                     (retval < 0) ? ovs_strerror(errno) :
                     "incomplete data sent");
    }

    return;
}

/* VLAN removed event handler.
 * Sends vlan removed xp_ml_event to software FDB task.*/ 
int
ops_xp_mac_learning_on_vlan_removed(struct xp_mac_learning *ml, xpsVlan_t vlanId)
{
    int retval = 0;
    struct xp_ml_event event;

    ovs_assert(ml);

    event.type = XP_ML_VLAN_REMOVED_EVENT;
    event.data.vlan = vlanId;

    /* send event to FDB task */
    do {
        retval = write(ml->event_latch.fds[1], &event, sizeof(event));
        pthread_yield();
    } while (((retval < 0) && (errno == EAGAIN)) || ((retval != sizeof(event))));
 
    if (retval < 0) {
        VLOG_WARN("failed to send event (%s)", 
                  ovs_strerror(errno));
        return retval;
    }

    return 0;
}

/* Port down event handler.
 * Sends port down xp_ml_event to software FDB task. */
int
ops_xp_mac_learning_on_port_down(struct xp_mac_learning *ml,
                                 xpsInterfaceId_t intfId)
{
    int retval = 0;
    struct xp_ml_event event;

    ovs_assert(ml);

    event.type = XP_ML_PORT_DOWN_EVENT;
    event.data.intfId = intfId;

    /* TODO: need to make it safe to be called from signal handler if this 
     * handler is registered and called from XDK. Return status type also
     * should be changed to XP_STATUS. */

    /* send event to FDB task */
    do {
        retval = write(ml->event_latch.fds[1], &event, sizeof(event));
        pthread_yield();
    } while (((retval < 0) && (errno == EAGAIN)) || ((retval != sizeof(event))));
 
    if (retval < 0) {
        VLOG_WARN("failed to send event (%s)", 
                  ovs_strerror(errno));
        return retval;
    }

    return 0;
}

/* VNI removed event handler.
 * Removes and then adds again all FDB entries learned on the interface if it's
 * member of the vlan. */
void
ops_xp_mac_learning_on_vni_removed(struct xp_mac_learning *ml, xpsVlan_t vlan,
                                   uint32_t vni, xpsInterfaceId_t if_id)
{
    struct xp_mac_entry *e = NULL;
    struct xp_mac_entry *next = NULL;
    int ret = 0;

    ovs_assert(ml);

    ovs_rwlock_wrlock(&ml->rwlock);

    HMAP_FOR_EACH_SAFE (e, next, hmap_node, &ml->table) {
        if ((e->xps_fdb_entry.vlanId == vlan) &&
            (e->xps_fdb_entry.intfId == if_id) &&
            (e->xps_fdb_entry.serviceInstId == vni)) {

            ops_xp_mac_learning_expire(ml, e);
        }
    }

    ovs_rwlock_unlock(&ml->rwlock);
}

void
ops_xp_mac_learning_dump_table(struct xp_mac_learning *ml, struct ds *d_str)
{
    const struct xp_mac_entry *e = NULL;

    ovs_assert(ml);
    ovs_assert(d_str);

    ds_put_cstr(d_str, " port    VLAN  MAC                index\n");

    HMAP_FOR_EACH(e, hmap_node, &ml->table) {
        if (e) {
            char *iface_name;

            iface_name = ops_xp_get_iface_name(ml->ofproto,
                                               e->xps_fdb_entry.intfId,
                                               e->xps_fdb_entry.serviceInstId);
            if (!iface_name) {
                continue;
            }

            ds_put_format(d_str, "%-8s %4d  "XP_ETH_ADDR_FMT"  0x%lx\n",
                          iface_name,
                          e->xps_fdb_entry.vlanId, 
                          XP_ETH_ADDR_ARGS(e->xps_fdb_entry.macAddr),
                          e->hmap_node.hash);
            free(iface_name);
        }
    }
}
