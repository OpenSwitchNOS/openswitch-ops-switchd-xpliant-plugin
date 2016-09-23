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
 * File: ops-xp-vlan.c
 *
 * Purpose: This file contains OpenSwitch VLAN related application code for the
 *          Cavium/XPliant SDK.
 */

#include <errno.h>

#include "ops-xp-vlan.h"
#include "ops-xp-mac-learning.h"
#include "bitmap.h"
#include <openvswitch/vlog.h>
#include "ops-xp-ofproto-provider.h"
#include "ops-xp-util.h"
#include "openXpsStp.h"

VLOG_DEFINE_THIS_MODULE(xp_vlan);

static struct xp_vlan_member_entry*
vlan_member_lookup(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id,
                   xpsInterfaceId_t intfId);

/* Creates VLAN manager with table of all the possible VLANs in the system.
 * Also initializes all required deafult VLAN data - members bitmaps
 * and hash table are created here for each VLAN and are destroyed 
 * only during VLAN manager removal. */ 
struct xp_vlan_mgr *
ops_xp_vlan_mgr_create(struct xpliant_dev *xp_dev)
{
    struct xp_vlan_mgr *mgr = NULL;
    int i = 0;

    ovs_assert(xp_dev);

    mgr = xmalloc(sizeof *mgr);

    mgr->xp_dev = xp_dev;

    for (i = XP_VLAN_MIN_ID; i <= XP_VLAN_MAX_ID; i++) {
        mgr->table[i].is_existing = false;
        hmap_init(&mgr->table[i].members_table);
        hmap_init(&mgr->table[i].vxlan_vnis);
        hmap_init(&mgr->table[i].geneve_vnis);
        hmap_init(&mgr->table[i].nvgre_vnis);
    }

    ovs_refcount_init(&mgr->ref_cnt);
    ovs_rwlock_init(&mgr->rwlock);

    return mgr;
}

struct xp_vlan_mgr *
ops_xp_vlan_mgr_ref(struct xp_vlan_mgr* mgr)
{
    if (mgr) {
        ovs_refcount_ref(&mgr->ref_cnt);
    }

    return mgr;
}

/* Unreferences (and possibly destroys) VLAN manager 'mgr'. */
void
ops_xp_vlan_mgr_unref(struct xp_vlan_mgr *mgr)
{
    if (mgr && ovs_refcount_unref(&mgr->ref_cnt) == 1) {
        int i = 0;
        for (i = XP_VLAN_MIN_ID; i <= XP_VLAN_MAX_ID; i++) {
            ops_xp_vlan_remove(mgr, i);
            hmap_destroy(&mgr->table[i].members_table);
            hmap_destroy(&mgr->table[i].vxlan_vnis);
            hmap_destroy(&mgr->table[i].geneve_vnis);
            hmap_destroy(&mgr->table[i].nvgre_vnis);
        }

        ovs_rwlock_destroy(&mgr->rwlock);
        ops_xp_dev_free(mgr->xp_dev);

        free(mgr);
    }
}

/* Creates VLAN in both software and hardware */
int
ops_xp_vlan_create(struct xp_vlan_mgr* mgr, xpsVlan_t vlan_id)
{
    XP_STATUS status = XP_NO_ERR;
    xpsVlanConfig_t vlanConfig;

    ovs_assert(mgr);

    if (!ops_xp_is_vlan_id_valid(vlan_id)) {
        VLOG_ERR("%s, Invalid vlan_id: %d. Acceptable values are from %d to %d",
                 __FUNCTION__, vlan_id, XP_VLAN_MIN_ID, XP_VLAN_MAX_ID);
        return ENXIO;
    }

    if (mgr->table[vlan_id].is_existing) {
        VLOG_ERR("%s, Could not create. VLAN: %d alredy exists",
                 __FUNCTION__, vlan_id);
        return EEXIST;
    }

    status = xpsVlanCreate(mgr->xp_dev->id, vlan_id);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Could not create vlan: %d. Error code: %d\n",
                 __FUNCTION__, vlan_id, status);
        return EPERM;
    }

    status = xpsVlanGetConfig(mgr->xp_dev->id, vlan_id, &vlanConfig);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to get VLAN config for devId %d and vlanId %d \n",
                 mgr->xp_dev->id, vlan_id);
        return EPERM;
    }

    xpsStpGetDefault(mgr->xp_dev->id, &vlanConfig.stpId);
    vlanConfig.mirrorAnalyzerId = 0;
    vlanConfig.saMissCmd = XP_PKTCMD_FWD_MIRROR;
    vlanConfig.bcCmd = XP_PKTCMD_FWD;
    vlanConfig.unknownUcCmd = XP_PKTCMD_FWD;
    vlanConfig.arpBcCmd = XP_PKTCMD_FWD;

    status = xpsVlanSetConfig(mgr->xp_dev->id, vlan_id, &vlanConfig);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to set VLAN config for devId %d and vlanId %d \n",
                 mgr->xp_dev->id, vlan_id);
        return EPERM;
    }

    mgr->table[vlan_id].is_existing = true;

    return 0;
}

/* Removes VLAN in both software and hardware. */
int
ops_xp_vlan_remove(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id)
{
    XP_STATUS status = XP_NO_ERR;
    struct xp_vlan_member_entry *e = NULL;
    struct xp_vlan_member_entry *next = NULL;
    struct xp_tunnel_vni_entry *vni_e = NULL;
    struct xp_tunnel_vni_entry *vni_e_next = NULL;

    ovs_assert(mgr);

    if (!ops_xp_vlan_is_existing(mgr, vlan_id)) {
        return 0;
    }

    mgr->table[vlan_id].is_existing = false;

    /* Clear members_table */
    HMAP_FOR_EACH_SAFE(e, next, hmap_node,
                       &mgr->table[vlan_id].members_table) {
        hmap_remove(&mgr->table[vlan_id].members_table, &e->hmap_node);
        free(e);
    }

    hmap_shrink(&mgr->table[vlan_id].members_table);

    /* Clear vnis tables. */
    HMAP_FOR_EACH_SAFE(vni_e, vni_e_next, hmap_node,
                       &mgr->table[vlan_id].vxlan_vnis) {
        hmap_remove(&mgr->table[vlan_id].vxlan_vnis, &vni_e->hmap_node);
        free(vni_e);
    }

    hmap_shrink(&mgr->table[vlan_id].vxlan_vnis);

    vni_e = vni_e_next = NULL;
    HMAP_FOR_EACH_SAFE(vni_e, vni_e_next, hmap_node,
                       &mgr->table[vlan_id].geneve_vnis) {
        hmap_remove(&mgr->table[vlan_id].geneve_vnis, &vni_e->hmap_node);
        free(vni_e);
    }

    hmap_shrink(&mgr->table[vlan_id].nvgre_vnis);

    vni_e = vni_e_next = NULL;
    HMAP_FOR_EACH_SAFE(vni_e, vni_e_next, hmap_node,
                       &mgr->table[vlan_id].nvgre_vnis) {
        hmap_remove(&mgr->table[vlan_id].nvgre_vnis, &vni_e->hmap_node);
        free(vni_e);
    }

    hmap_shrink(&mgr->table[vlan_id].nvgre_vnis);

    /* Remove from hardware. */
    status = xpsVlanDestroy(mgr->xp_dev->id, vlan_id);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Could not destroy vlan %d. Error code: %d\n",
                 __FUNCTION__, vlan_id, status);
        return EPERM;
    }

    return 0;
}

/* Enables/disables flooding on a VLAN.
 * Updates corresponding flag and sets SA learning command
 * to flooding(if flooding flag is true) in the hardware 
 * i.e. learning packets won't be trapped to CPU for this VLAN. */
int
ops_xp_vlan_enable_flooding(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id,
                            bool flooding)
{
    XP_STATUS status = XP_NO_ERR;
    xpPktCmd_e sa_miss_cmd = flooding ? XP_PKTCMD_FWD : XP_PKTCMD_TRAP;
    xpPktCmd_e current_sa_miss_Cmd = XP_PKTCMD_MAX;

    ovs_assert(mgr);

    if (!ops_xp_vlan_is_existing(mgr, vlan_id)) {
         VLOG_ERR("%s, Could not set flooding mode on a VLAN."
                  "VLAN: %d does not exist", __FUNCTION__, vlan_id);
         return ENXIO;
    }

    status = xpsVlanGetUnknownSaCmd(mgr->xp_dev->id, vlan_id, &current_sa_miss_Cmd);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Could not get SA learning for the vlan %u, error code: %d",
                 __FUNCTION__, vlan_id, status);
        return EPERM;
    }

    if (sa_miss_cmd == current_sa_miss_Cmd) {
        return 0;
    }

    status = xpsVlanSetUnknownSaCmd(mgr->xp_dev->id, vlan_id, sa_miss_cmd);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Could not set SA learning for the vlan %u, error code: %d",
                 __FUNCTION__, vlan_id, status);
        return EPERM;
    }

    return 0;
}

/* Adds member to a VLAN.
 * After corresponding validity checks tries to add member to a VLAN on 
 * hardware. If this succeeds then updates tagged/untagged members bitmaps 
 * and stores index of member entry in hardware table to a members_table. */
int
ops_xp_vlan_member_add(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id,
                       xpsInterfaceId_t intfId, xpsL2EncapType_e encapType,
                       uint32_t vni)
{
    XP_STATUS status = XP_NO_ERR;
    struct xp_vlan_member_entry *e = NULL;

    ovs_assert(mgr);

    if (!ops_xp_vlan_is_existing(mgr, vlan_id)) {
        VLOG_ERR("%s, Could not add a member to VLAN. VLAN: %d does not exist", 
                 __FUNCTION__, vlan_id);
        return ENXIO;
    }

    if (ops_xp_vlan_port_is_member(mgr, vlan_id, intfId)) {

        VLOG_WARN("%s, Interface %d is already a member of VLAN %d",
                  __FUNCTION__, intfId, vlan_id);
        return 0;
    }

    if (!ops_xp_is_tunnel_intf(intfId)) {

        status = xpsVlanAddInterface(mgr->xp_dev->id, vlan_id, intfId, encapType);
        if (status != XP_NO_ERR) {
            VLOG_ERR("%s: Could not add interface %d to VLAN %d. "
                     "Error code: %d\n", 
                     __FUNCTION__, intfId, vlan_id, status);
            return EPERM;
        }
    } else {

        status = xpsVlanAddEndpoint(mgr->xp_dev->id, vlan_id, intfId,
                                    encapType, vni);
        if (status != XP_NO_ERR) {
            VLOG_ERR("%s: Could not add interface %d to VLAN %d. "
                     "Error code: %d\n", 
                     __FUNCTION__, intfId, vlan_id, status);
            return EPERM;
        }

        VLOG_DBG("%s: Added tunnel interface %d to VLAN %d, VNI: %d. ", 
                 __FUNCTION__, intfId, vlan_id, vni);
    }

    e = xmalloc(sizeof(*e));
    e->encapType = encapType;
    hmap_insert(&mgr->table[vlan_id].members_table, &e->hmap_node, intfId);

    return 0;
}

/* Removes member from a VLAN.
 * Removes member entry from a hardware table as well as from the software
 * one and updates tagged/untagged members bitmap. */
int
ops_xp_vlan_member_remove(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id,
                          xpsInterfaceId_t intfId)
{
    XP_STATUS status = XP_NO_ERR;
    struct xp_vlan_member_entry *e = NULL;
    xpL2EncapType_e encapType = XP_L2_ENCAP_INVALID;

    ovs_assert(mgr);

    if (!ops_xp_vlan_is_existing(mgr, vlan_id)) {
        VLOG_ERR("%s, Could not remove member from VLAN. "
                 "VLAN: %d does not exist", __FUNCTION__, vlan_id);
        return ENXIO;
    }

    if (ops_xp_vlan_port_is_member(mgr, vlan_id, intfId)) {

        e = vlan_member_lookup(mgr, vlan_id, intfId);
        if (!e) {
            VLOG_ERR("%s, Could not locate entry for member %x of VLAN %u.",
                     __FUNCTION__, intfId, vlan_id);
            return ENOENT;
        }
    } else {
        /* Nothing to remove. */
        return 0;
    }

    if (!ops_xp_is_tunnel_intf(intfId)) {
        status = xpsVlanSetOpenFlowEnable(mgr->xp_dev->id, vlan_id, intfId, 0);
        if (status != XP_NO_ERR) {
            VLOG_ERR("%s: Failed to remove interface/vlan pair %d:%d "
                     "from OpenFlow pipeline. Error code: %d\n",
                     __FUNCTION__, intfId, vlan_id, status);
            return EPERM;
        }
    }

    status = xpsVlanRemoveInterface(mgr->xp_dev->id, vlan_id, intfId);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Could not remove interface %u from vlan %d. "
                 "Error code: %d\n",
                 __FUNCTION__, intfId, vlan_id, status);
        return EPERM;
    }

    /* Update softare members table */
    hmap_remove(&mgr->table[vlan_id].members_table, &e->hmap_node);
    free(e);

    hmap_shrink(&mgr->table[vlan_id].members_table);

    ovs_rwlock_wrlock(&mgr->xp_dev->ml->rwlock);
    ops_xp_mac_learning_flush_vlan_intf(mgr->xp_dev->ml, vlan_id, intfId);
    ovs_rwlock_unlock(&mgr->xp_dev->ml->rwlock);

    return ((status == XP_NO_ERR) ? 0 : EPERM);
}

/* Removes all VLAN members */
int
ops_xp_vlan_members_remove_all(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id)
{
    XP_STATUS status = XP_NO_ERR;
    struct xp_vlan_member_entry *e = NULL;
    struct xp_vlan_member_entry *next = NULL;

    ovs_assert(mgr);

    if (!ops_xp_vlan_is_existing(mgr, vlan_id)) {
        VLOG_ERR("%s, Could not remove members from VLAN. "
                 "VLAN: %d does not exist", __FUNCTION__, vlan_id);
        return ENXIO;
    }

    HMAP_FOR_EACH_SAFE(e, next, hmap_node,
                       &mgr->table[vlan_id].members_table) {

        status = ops_xp_vlan_member_remove(mgr, vlan_id, e->hmap_node.hash);
        if (status != 0) {
            return status;
        }
    }

    return 0;
}

/* Sets tagging on a VLAN member.
 * If tagging flag is different than the one already set for a member
 * the member is removed from a VLAN and then added again but with new
 * tagging value. */ 
int
ops_xp_vlan_member_set_tagging(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id,
                               xpsInterfaceId_t intfId, bool tagging,
                               uint32_t vni)
{
    int rc = 0;

    ovs_assert(mgr);

    if (!ops_xp_vlan_is_existing(mgr, vlan_id)) {
        VLOG_ERR("%s, Could not remove member from VLAN. "
                 "VLAN: %d does not exist", __FUNCTION__, vlan_id);
        return ENXIO;
    }

    /* TODO: add check port number validity here when such code exists */

    if (ops_xp_vlan_port_is_tagged_member(mgr, vlan_id, intfId)) {

        if (tagging) {
            /* Tagging is already set on this member. */
            return 0;
        }

    } else if (ops_xp_vlan_port_is_untagged_member(mgr, vlan_id, intfId)) {

        if (!tagging) {
            /* Untagging is already set on this member. */
            return 0;
        }

    } else {
        VLOG_ERR("%s, Could not set tagging on port %u since it is not"
                 "a member of VLAN %u", __FUNCTION__, intfId, vlan_id);
        return EOPNOTSUPP;
    }

    rc = ops_xp_vlan_member_remove(mgr, vlan_id, intfId);
    if (rc != 0) {
        return rc;
    }

    rc = ops_xp_vlan_member_add(mgr, vlan_id, intfId, tagging, vni);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/* Removes port from all VLANs it particpates in. */
int
ops_xp_vlan_port_clear_all_membership(struct xp_vlan_mgr *mgr,
                                      xpsInterfaceId_t intfId)
{
    int i = 0;
    int rc = 0;

    ovs_assert(mgr);

    for (i = XP_VLAN_MIN_ID; i <= XP_VLAN_MAX_ID; i++) {

        if (!ops_xp_vlan_is_existing(mgr, i)) {
            continue;
        }

        rc = ops_xp_vlan_member_remove(mgr, i, intfId);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}

bool
ops_xp_is_vlan_id_valid(xpsVlan_t vlan_id)
{
    return ((vlan_id >= XP_VLAN_MIN_ID) && (vlan_id <= XP_VLAN_MAX_ID));
}

bool
ops_xp_vlan_is_existing(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id)
{
    ovs_assert(mgr);

    return (ops_xp_is_vlan_id_valid(vlan_id) &&
            mgr->table[vlan_id].is_existing);
}

bool
ops_xp_vlan_is_flooding(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id)
{
    XP_STATUS status = XP_NO_ERR;
    xpPktCmd_e sa_miss_cmd;

    ovs_assert(mgr);

    if (!ops_xp_vlan_is_existing(mgr, vlan_id)) {
         VLOG_ERR("%s, Could not get flooding mode on a VLAN."
                  "VLAN: %d does not exist", __FUNCTION__, vlan_id);
         return false;
    }

    status = xpsVlanGetUnknownSaCmd(mgr->xp_dev->id, vlan_id, &sa_miss_cmd);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Could not get SA learning for the vlan %u, error code: %d",
                 __FUNCTION__, vlan_id, status);
        return false;
    }

    return (sa_miss_cmd == XP_PKTCMD_FWD);
}

bool
ops_xp_vlan_is_learning(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id)
{
    return (ops_xp_vlan_is_existing(mgr, vlan_id) &&
            !ops_xp_vlan_is_flooding(mgr, vlan_id));
}

bool
ops_xp_vlan_port_is_tagged_member(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id,
                                  xpsInterfaceId_t intfId)
{
    struct xp_vlan_member_entry *e = NULL;

    ovs_assert(mgr);

    e = vlan_member_lookup(mgr, vlan_id, intfId);

    return (e && (e->encapType == XP_L2_ENCAP_DOT1Q_TAGGED));
}

bool
ops_xp_vlan_port_is_untagged_member(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id,
                                    xpsInterfaceId_t intfId)
{
    struct xp_vlan_member_entry *e = NULL;

    ovs_assert(mgr);

    e = vlan_member_lookup(mgr, vlan_id, intfId);

    return (e && (e->encapType == XP_L2_ENCAP_DOT1Q_UNTAGGED));
}

/* Returns true if port is memeber of a vlan. */
bool
ops_xp_vlan_port_is_member(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id,
                           xpsInterfaceId_t intfId)
{
    ovs_assert(mgr);

    return (vlan_member_lookup(mgr, vlan_id, intfId) != NULL);
}

/* Gets encapsulation type for a VLAN member. */
int
ops_xp_vlan_member_get_encap_type(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id,
                                  xpsInterfaceId_t intfId,
                                  xpL2EncapType_e *encapType)
{
    struct xp_vlan_member_entry *e = NULL;

    ovs_assert(mgr);

    e = vlan_member_lookup(mgr, vlan_id, intfId);

    if (e) {
        *encapType = e->encapType;
        return 0;
    }

    return ENODATA;
}

static struct xp_vlan_member_entry*
vlan_member_lookup(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id,
                   xpsInterfaceId_t intfId)
{
    struct hmap_node* e = NULL;

    ovs_assert(mgr);

    if (ops_xp_vlan_is_existing(mgr, vlan_id)) {
        e = hmap_first_with_hash(&mgr->table[vlan_id].members_table, intfId);

        if (e) {
            return CONTAINER_OF(e, struct xp_vlan_member_entry, hmap_node);
        }
    }

    return NULL;
}

bool
ops_xp_vlan_is_membership_empty(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id)
{
    if (ops_xp_vlan_is_existing(mgr, vlan_id)) {
        return hmap_is_empty(&mgr->table[vlan_id].members_table);
    }

    return true;
}
