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
 * File: ops-xp-ofproto-datapath.h
 *
 * Purpose: This file provides public definitions for OpenSwitch OpenFlow
 *          related application code for the Cavium/XPliant SDK.
 */

#ifndef OPS_XP_OFPROTO_DATAPATH_H
#define OPS_XP_OFPROTO_DATAPATH_H 1

#include "ofproto/ofproto-provider.h"
#include "openXpsTypes.h"

/* Flow table operations. */

struct flow_stats_xpliant {
    uint64_t n_packets; /* accumulated number of packets per flow */
    uint64_t n_bytes; /* accumulated number of bytes per flow */
    long long int used;
};

struct rule_xpliant {
    struct rule up;

    struct hmap_node hmap_node; /* Node in a 'ofproto->flows' hmap. */
    xpOfFlowId_t flow_id;

    struct ovs_mutex stats_mutex;
    struct flow_stats_xpliant stats OVS_GUARDED; /* accumulated statistic */
};

struct group_xpliant {

    struct ofgroup up;
};

/* ## ----------------------- ## */
/* ## OpenFlow Rule Functions ## */
/* ## ----------------------- ## */
struct rule *ofproto_xpliant_rule_alloc(void);
void ofproto_xpliant_rule_dealloc(struct rule *rule_);
enum ofperr ofproto_xpliant_rule_construct(struct rule *rule_ OVS_UNUSED);
void ofproto_xpliant_rule_insert(struct rule *rule, struct rule *old_rule,
                                 bool forward_stats);
void ofproto_xpliant_rule_delete(struct rule *rule_ OVS_UNUSED)
    OVS_REQUIRES(ofproto_mutex);
void ofproto_xpliant_rule_destruct(struct rule *rule_ OVS_UNUSED);
void ofproto_xpliant_rule_get_stats(struct rule *rule_ OVS_UNUSED,
                                    uint64_t * packets OVS_UNUSED,
                                    uint64_t * bytes OVS_UNUSED,
                                    long long int *used OVS_UNUSED);
enum ofperr ofproto_xpliant_rule_execute(struct rule *rule OVS_UNUSED,
                                         const struct flow *flow OVS_UNUSED,
                                         struct dp_packet *packet OVS_UNUSED);
bool ofproto_xpliant_set_frag_handling(struct ofproto *ofproto_ OVS_UNUSED,
                                       enum ofp_config_flags frag_handling OVS_UNUSED);
enum ofperr ofproto_xpliant_packet_out(struct ofproto *ofproto_ OVS_UNUSED,
                                       struct dp_packet *packet OVS_UNUSED,
                                       const struct flow *flow OVS_UNUSED,
                                       const struct ofpact *ofpacts OVS_UNUSED,
                                       size_t ofpacts_len OVS_UNUSED);

/* ## -------------------- ## */
/* ## OpenFlow 1.1+ groups ## */
/* ## -------------------- ## */
struct ofgroup *ofproto_xpliant_group_alloc(void);
void ofproto_xpliant_group_dealloc(struct ofgroup *group_);
enum ofperr ofproto_xpliant_group_construct(struct ofgroup *group_ OVS_UNUSED);
void ofproto_xpliant_group_destruct(struct ofgroup *group_ OVS_UNUSED);
enum ofperr ofproto_xpliant_group_modify(struct ofgroup *group_ OVS_UNUSED);
enum ofperr ofproto_xpliant_group_get_stats(const struct ofgroup *group_ OVS_UNUSED,
                                            struct ofputil_group_stats *ogs OVS_UNUSED);
const char *ofproto_xpliant_get_datapath_version(const struct ofproto *ofproto_ OVS_UNUSED);


static inline struct rule_xpliant *
rule_xpliant_cast(const struct rule *rule)
{
    return rule ? CONTAINER_OF(rule, struct rule_xpliant, up) : NULL;
}

static inline struct group_xpliant *
group_xpliant_cast(const struct ofgroup *group)
{
    return group ? CONTAINER_OF(group, struct group_xpliant, up) : NULL;
}

#endif /* ops-xp-ofproto-datapath.h */
