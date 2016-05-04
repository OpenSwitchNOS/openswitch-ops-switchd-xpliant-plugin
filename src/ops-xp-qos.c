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
 * File: ops-xp-qos.c
 *
 * Purpose: This file contains OpenSwitch QoS related application code for the
 *          Cavium/XPliant SDK.
 */

#include <unistd.h>
#include "openvswitch/vlog.h"
#include "ofproto/ofproto-provider.h"
#include "qos-asic-provider.h"
#include "ops-xp-ofproto-provider.h"
#include "ops-xp-qos.h"


VLOG_DEFINE_THIS_MODULE(xp_qos);

int
ops_xp_qos_set_port_qos_cfg(struct ofproto *ofproto_, void *aux,
                            const struct qos_port_settings *settings)
{
    const struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);
    struct bundle_xpliant *bundle;

    VLOG_DBG("%s", __FUNCTION__);

    bundle = bundle_lookup(ofproto, aux);
    if (bundle) {
        VLOG_DBG("%s: port %s, settings->qos_trust %d, cfg@ %p",
                 __FUNCTION__, bundle->name, settings->qos_trust, settings->other_config);
    } else {
        VLOG_DBG("%s: NO BUNDLE aux@%p, settings->qos_trust %d, cfg@ %p",
                 __FUNCTION__, aux, settings->qos_trust, settings->other_config);
    }

    return 0;
}

int
ops_xp_qos_set_cos_map(struct ofproto *ofproto_, void *aux,
                       const struct cos_map_settings *settings)
{
    struct cos_map_entry *entry;
    int i;

    VLOG_DBG("%s", __FUNCTION__);

    for (i = 0; i < settings->n_entries; i++) {
        entry = &settings->entries[i];
        VLOG_DBG("%s: ofproto@ %p index=%d color=%d cp=%d lp=%d",
                 __FUNCTION__, ofproto_, i,
                 entry->color, entry->codepoint, entry->local_priority);
    }

    return 0;
}

int
ops_xp_qos_set_dscp_map(struct ofproto *ofproto_, void *aux,
                        const struct dscp_map_settings *settings)
{
    struct dscp_map_entry *entry;
    int i;

    VLOG_DBG("%s", __FUNCTION__);

    for (i = 0; i < settings->n_entries; i++) {
        entry = &settings->entries[i];
        VLOG_DBG("%s: ofproto@ %p index=%d color=%d cp=%d lp=%d cos=%d",
                 __FUNCTION__, ofproto_, i, entry->color, entry->codepoint,
                 entry->local_priority, entry->cos);
    }

    return 0;
}

int
ops_xp_qos_apply_qos_profile(struct ofproto *ofproto_, void *aux,
                             const struct schedule_profile_settings *s_settings,
                             const struct queue_profile_settings *q_settings)
{
    struct queue_profile_entry *qp_entry;
    struct schedule_profile_entry *sp_entry;
    int i;

    VLOG_DBG("%s ofproto@ %p aux=%p q_settings=%p s_settings=%p", __FUNCTION__,
             aux, ofproto_, s_settings, q_settings);

    for (i = 0; i < q_settings->n_entries; i++) {
        qp_entry = q_settings->entries[i];
        VLOG_DBG("... %d q=%d #lp=%d", i,
                 qp_entry->queue, qp_entry->n_local_priorities);
    }

    for (i = 0; i < s_settings->n_entries; i++) {
        sp_entry = s_settings->entries[i];
        VLOG_DBG("... %d q=%d alg=%d wt=%d", i,
                 sp_entry->queue, sp_entry->algorithm, sp_entry->weight);
    }

    return 0;
}
