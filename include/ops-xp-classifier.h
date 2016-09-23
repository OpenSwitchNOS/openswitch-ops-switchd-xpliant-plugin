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
 * File: ops-xp-classifier.h
 *
 * Purpose: This file provides public definitions for OpenSwitch classifier
 *          related application code for the Cavium/XPliant SDK.
 */

#ifndef OPS_XP_CLASSIFIER_H
#define OPS_XP_CLASSIFIER_H 1

#include <stdio.h>
#include <stdlib.h>
#include "ofproto/ofproto-provider.h"
#include "ops-cls-asic-plugin.h"
#include "uuid.h"
#include "hmap.h"
#include "ovs/list.h"
#include "openXpsTypes.h"


struct xp_acl_rule {
    struct ovs_list list_node;
    uint32_t        rule_id;
    uint8_t         counter_en;
    uint64_t        count;
};

struct xp_acl_interface {
    struct ovs_list     list_node;
    xpsInterfaceId_t    intf_num;
};

struct xp_acl_entry {
    struct hmap_node        hnode;      /* Node in a xp_acl hmap. */
    struct uuid             list_uid;   /* uuid of classifier list in OVSDB */
    uint8_t                 acl_id;     /* Identifies this list node inside XDK */

    /* Rule index list, maintained through a linked list */
    uint16_t                num_rules;  /* number of rules in this classifier list */
    struct ovs_list         rule_list;  /* list that holds rule ids in TCAM for the corresponding classifier rules in order */

    /* Interface list, maintained through a dynamic array (intial size 4) */
    uint16_t                num_intfs;          /* number of interfaces on which ACL is applied */
    struct ovs_list         intf_list;          /* list of interfaces on which ACL is applied */
    struct ops_cls_interface_info   intf_info;  /* store interface type PORT or VLAN or TUNNEL*/
    enum ops_cls_direction  cls_direction;      /* indicates whether the classifier is applied on INGRESS or EGRESS*/
};

typedef struct iacl_v4_key {
    xpIaclV4KeyFlds v4_field;
    uint8_t         size;
} iacl_v4_key_t;

int ops_xp_cls_apply(struct ops_cls_list *list, struct ofproto *ofproto,
                     void *aux, struct ops_cls_interface_info *interface_info,
                     enum ops_cls_direction direction,
                     struct ops_cls_pd_status *pd_status);

int ops_xp_cls_remove(const struct uuid *list_id, const char *list_name,
                      enum ops_cls_type list_type, struct ofproto *ofproto,
                      void *aux, struct ops_cls_interface_info *interface_info,
                      enum ops_cls_direction direction,
                      struct ops_cls_pd_status *pd_status);

int ops_xp_cls_replace(const struct uuid *list_id_orig, const char *list_name_orig,
                       struct ops_cls_list *list_new, struct ofproto *ofproto,
                       void *aux, struct ops_cls_interface_info *interface_info,
                       enum ops_cls_direction direction,
                       struct ops_cls_pd_status *pd_status);

int ops_xp_cls_list_update(struct ops_cls_list *list,
                           struct ops_cls_pd_list_status *status);

int ops_xp_cls_stats_get(const struct uuid *list_id, const char *list_name,
                         enum ops_cls_type list_type, struct ofproto *ofproto,
                         void *aux, struct ops_cls_interface_info *interface_info,
                         enum ops_cls_direction direction,
                         struct ops_cls_statistics *statistics,
                         int num_entries, struct ops_cls_pd_list_status *status);

int ops_xp_cls_stats_clear(const struct uuid *list_id, const char *list_name,
                           enum ops_cls_type list_type, struct ofproto *ofproto,
                           void *aux, struct ops_cls_interface_info *interface_info,
                           enum ops_cls_direction direction,
                           struct ops_cls_pd_list_status *status);

int ops_xp_cls_stats_clear_all(struct ops_cls_pd_list_status *status);

int ops_xp_cls_acl_log_pkt_register_cb(void (*callback_handler)(struct acl_log_info *));
int ops_xp_cls_init(xpsDevice_t devId);

#endif /* ops-xp-classifier.h */
