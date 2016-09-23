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
 * File: ops-xp-vlan.h
 *
 * Purpose: This file provides public definitions for OpenSwitch VLAN
 *          related application code for the Cavium/XPliant SDK.
 */

#ifndef OPS_XP_VLAN_H
#define OPS_XP_VLAN_H 1

#include "hmap.h"
#include "ovs-thread.h"
#include "openXpsVlan.h"

struct ofproto_xpliant;

#define XP_VLAN_MIN_ID              XPS_VLANID_MIN
#define XP_VLAN_MAX_ID              XPS_VLANID_MAX
#define XP_VLAN_MAX_COUNT           XP_MAX_VLAN_NUM
#define XP_DEFAULT_VLAN_ID          1/*XP_VLAN_MIN_ID*/

/* In XDK tunnel VNIs are configured per VLAN whereas in OVS - per tunnel.
 * So created once VNI can serve for many tunnels. This entry is designed 
 * to track how many tunnel interfaces use the same VNI in a VLAN.
 * This will prevent a VNI from removal from XDK when a tunnel interface
 * using it is removed from OVS. The index of the entry is VNI itself. */
struct xp_tunnel_vni_entry {
    struct hmap_node hmap_node;    /* Node in a hmap. */
    struct ovs_refcount ref_cnt;  /* Reference counter for this VNI. */
};

/* A members_table entry. Entry index is a port VIF. */
struct xp_vlan_member_entry {
    struct hmap_node hmap_node; /* Node in a hmap. */
    xpL2EncapType_e encapType;  /* Encapsulation type of a member. */
};

/* VLAN entry which stores all necessary data to
 * to configure VLAN in software as well as in the hardware. */
struct xp_vlan {
    bool is_existing;           /* Flag signalizing VLAN is existing. */
    struct hmap members_table;  /* Table of members of this vlan. */
    struct hmap vxlan_vnis;     /* Table of VxLAN VNIs */
    struct hmap geneve_vnis;    /* Table of Geneve VNIs */
    struct hmap nvgre_vnis;     /* Table of NVGRE VNIs */
};

/* Vlan manager - holds table of the VLANs*/
struct xp_vlan_mgr {
    struct xp_vlan table[XP_VLAN_MAX_COUNT];
    struct xpliant_dev *xp_dev;
    xpsDevice_t dev_id;
    struct ovs_refcount ref_cnt;
    struct ovs_rwlock rwlock;
};

/* Basics. */
struct xp_vlan_mgr *ops_xp_vlan_mgr_create(struct xpliant_dev *xp_dev);
struct xp_vlan_mgr *ops_xp_vlan_mgr_ref(struct xp_vlan_mgr *mgr);
void ops_xp_vlan_mgr_unref(struct xp_vlan_mgr* mgr);

/* Configuration */

int ops_xp_vlan_create(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id)
    OVS_REQ_WRLOCK(mgr->rwlock);

int ops_xp_vlan_remove(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id)
    OVS_REQ_WRLOCK(mgr->rwlock);

int ops_xp_vlan_enable_flooding(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id,
                                bool flooding)
    OVS_REQ_WRLOCK(mgr->rwlock);

int ops_xp_vlan_member_add(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id,
                           xpsInterfaceId_t intfId, xpsL2EncapType_e encapType,
                           uint32_t vni)
    OVS_REQ_WRLOCK(mgr->rwlock);

int ops_xp_vlan_member_remove(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id,
                              xpsInterfaceId_t intfId)
    OVS_REQ_WRLOCK(mgr->rwlock);

int ops_xp_vlan_members_remove_all(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id)
    OVS_REQ_WRLOCK(mgr->rwlock);

int ops_xp_vlan_member_set_tagging(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id,
                                   xpsInterfaceId_t intfId, bool tagging,
                                   uint32_t vni)
    OVS_REQ_WRLOCK(mgr->rwlock);

int ops_xp_vlan_port_clear_all_membership(struct xp_vlan_mgr *mgr,
                                          xpsInterfaceId_t intfId)
    OVS_REQ_WRLOCK(mgr->rwlock);

/* Helpers */

bool ops_xp_is_vlan_id_valid(xpsVlan_t vlan_id);

bool ops_xp_vlan_is_existing(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id)
     OVS_REQ_RDLOCK(mgr->rwlock);

bool ops_xp_vlan_is_flooding(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id)
     OVS_REQ_RDLOCK(mgr->rwlock);

bool ops_xp_vlan_is_learning(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id)
     OVS_REQ_RDLOCK(mgr->rwlock);

bool ops_xp_vlan_port_is_tagged_member(struct xp_vlan_mgr *mgr,
                                       xpsVlan_t vlan_id,
                                       xpsInterfaceId_t intfId)
     OVS_REQ_RDLOCK(mgr->rwlock);

bool ops_xp_vlan_port_is_untagged_member(struct xp_vlan_mgr *mgr,
                                         xpsVlan_t vlan_id,
                                         xpsInterfaceId_t intfId)
     OVS_REQ_RDLOCK(mgr->rwlock);

bool ops_xp_vlan_port_is_member(struct xp_vlan_mgr *mgr, xpsVlan_t vlan_id,
                                xpsInterfaceId_t intfId)
     OVS_REQ_RDLOCK(mgr->rwlock);

int ops_xp_vlan_member_get_encap_type(struct xp_vlan_mgr *mgr,
                                      xpsVlan_t vlan_id,
                                      xpsInterfaceId_t intfId,
                                      xpL2EncapType_e *encapType)
     OVS_REQ_RDLOCK(mgr->rwlock);

bool ops_xp_vlan_is_membership_empty(struct xp_vlan_mgr *mgr,
                                     xpsVlan_t vlan_id)
    OVS_REQ_WRLOCK(mgr->rwlock);

#endif /* ops-xp-vlan.h */
