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
#include "openXpsQos.h"


VLOG_DEFINE_THIS_MODULE(xp_qos);


/* There exist two profiles, default and factory-default profiles.
 * The default profile can be modified by the customer.
 * while factory-default is never modified or deleted.
 * Modified default profile uses the same profile id.
 *
 * Once user remove the edited default-profile OpenSwitch will push the
 * factory-default values. */
#define OPS_QOS_DEFAULT_PROFILE_ID 1

static int qos_set_port_qos_trust(xpsDevice_t dev_id, xpsPort_t port,
                                  enum qos_trust qos_trust)
{
    XP_STATUS status = XP_NO_ERR;
    uint32_t profile_id = OPS_QOS_DEFAULT_PROFILE_ID;
    uint32_t enable = true;

    VLOG_DBG("%s: xp_port %d, xpdevid: %d ", __FUNCTION__, port, dev_id);

    switch (qos_trust) {
    case QOS_TRUST_NONE:
        status = xpsQosPortIngressSetL2QosProfileForPort(dev_id, port,
                                                  profile_id, enable);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Profile %d could not be set to xp_port %d",
                                               profile_id, port);
            return EFAULT;
        }

        status = xpsQosPortIngressSetL3QosProfileForPort(dev_id, port,
                                                  profile_id, enable);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Profile %d could not be set to xp_port %d",
                                               profile_id, port);
            return EFAULT;
        }

        break;

    case QOS_TRUST_COS:
        status = xpsQosPortIngressSetTrustL2ForPort(dev_id, port);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Failed to set Trust for L2, retVal:%d", status);
            return EFAULT;
        }

        break;

    case QOS_TRUST_DSCP:
        status = xpsQosPortIngressSetTrustL3ForPort(dev_id, port);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Failed to set Trust for L2, retVal:%d", status);
            return EFAULT;
        }

        break;

    default:
        VLOG_ERR("Trust: %d not supported", qos_trust);
        return EINVAL;

    }
    return 0;
}

static int
qos_set_port_dscp_override(xpsDevice_t dev_id, xpsPort_t port, uint8_t value)
{
    XP_STATUS status = XP_NO_ERR;
    uint32_t profile_id = OPS_QOS_DEFAULT_PROFILE_ID;
    uint32_t tc, dp;

    VLOG_DBG("%s: xp_port %d, DSCP OverRide Value %d", __FUNCTION__,
              port, value);

    status = xpsQosPortIngressSetPortDefaultL3QosPriority(dev_id, port, value);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to set DSCP overide value, retVal:%d", status);
        return EFAULT;
    }

    /* Get the TC and DP from the L3 QoS map table for this dscpVal and
       use same to program below.*/
    status = xpsQosPortIngressGetTrafficClassForL3QosProfile(dev_id, profile_id,
                                                                    value, &tc);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to get Qos Map DSCP to TC, retVal:%d", status);
        return EFAULT;
    }

    status = xpsQosPortIngressGetDropPrecedenceForL3QosProfile(dev_id,
                                              profile_id, value, &dp);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to get Qos Map DSCP to COLOR, retVal:%d", status);
        return EFAULT;
    }

    VLOG_DBG("%s: DP: %d and TC: %d for DSCP: %d "
              , __FUNCTION__, dp, tc, value);

    status = xpsQosPortIngressSetPortDefaultTrafficClass(dev_id, port, tc);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to set TC, retVal : %d", status);
        return EFAULT;
    }

    status = xpsQosPortIngressSetPortDefaultDropPrecedence(dev_id, port, dp);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to set COLOR, retVal:%d", status);
        return EFAULT;
    }

    return 0;
}

int
ops_xp_qos_set_port_qos_cfg(struct ofproto *ofproto_, void *aux,
                            const struct qos_port_settings *settings)
{
    XP_STATUS status = XP_NO_ERR;
    const struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);
    struct bundle_xpliant *bundle;
    struct ofport_xpliant *port = NULL;
    struct ofport_xpliant *next_port = NULL;
    int ret = 0;

    VLOG_DBG("%s", __FUNCTION__);

    bundle = bundle_lookup(ofproto, aux);
    if (bundle) {
        VLOG_DBG("%s: port %s, settings->qos_trust %d, cfg@ %p",
                 __FUNCTION__, bundle->name, settings->qos_trust, settings->other_config);

        /* If port_trust config */
        if (!settings->dscp_override_enable) {
            LIST_FOR_EACH_SAFE (port, next_port, bundle_node, &bundle->ports) {
                ret = qos_set_port_qos_trust(ofproto->xpdev->id,
                                             ops_xp_get_ofport_number(port),
                                             settings->qos_trust);
            }
        /* If dscp_override config */
        } else {
            LIST_FOR_EACH_SAFE (port, next_port, bundle_node, &bundle->ports) {
                ret = qos_set_port_dscp_override(ofproto->xpdev->id,
                                           ops_xp_get_ofport_number(port),
                                           settings->dscp_override_value);
            }
        }
    } else {
        VLOG_DBG("%s: NO BUNDLE aux@%p, settings->qos_trust %d, cfg@ %p",
                 __FUNCTION__, aux, settings->qos_trust, settings->other_config);
    }

    return ret;
}

int
ops_xp_qos_set_cos_map(struct ofproto *ofproto_, void *aux,
                       const struct cos_map_settings *settings)
{
    XP_STATUS status = XP_NO_ERR;
    const struct ofproto_xpliant *ofproto;
    struct cos_map_entry *entry;
    int i;
    uint32_t profile_id = OPS_QOS_DEFAULT_PROFILE_ID;
    uint8_t deiVal;

    VLOG_DBG("%s", __FUNCTION__);

    /* TODO
     * When the ofproto and/or aux pointers are NULL, the configuration
     * is for the system default CoS Map table.  Currently,
     * setting the system default CoS Map table is supported.
     * Hence the device Id is hardCoded.
     */

    for (i = 0; i < settings->n_entries; i++) {
        entry = &settings->entries[i];

        VLOG_DBG("%s: ofproto@ %p index=%d color=%d cp=%d lp=%d",
                 __FUNCTION__, ofproto_, i,
                 entry->color, entry->codepoint, entry->local_priority);
        /* Program for DEI-0 and DEI-1.*/
        for (deiVal = 0; deiVal < OPS_QOS_NUM_DEI_VALUES; deiVal++) {
            status = xpsQosPortIngressSetTrafficClassForL2QosProfile(0,
                                                    profile_id,
                                                    entry->codepoint,
                                                    deiVal,
                                                    entry->local_priority);
            if (status != XP_NO_ERR) {
                VLOG_ERR("Failed to set TC for L2 profile, retVal:%d", status);
            }
            status = xpsQosPortIngressSetDropPrecedenceForL2QosProfile(0,
                                                    profile_id,
                                                    entry->codepoint,
                                                    deiVal,
                                                    entry->color);
            if (status != XP_NO_ERR) {
                VLOG_ERR("Failed to set DP for L2 profile, retVal:%d", status);
            }
            status = xpsQosPortIngressRemapPriorityForL2QosProfile(0,
                                                    profile_id,
                                                    entry->codepoint,
                                                    deiVal,
                                                    entry->codepoint,
                                                    deiVal, 0, 0);
            if (status != XP_NO_ERR) {
                VLOG_ERR("Failed to set Remap Priority for L2 profile,"
                         " retVal:%d", status);
            }
        }

        /*  TODO
         *  Don't enable DOT1P remark globally. This needs to be handled
         *  from port QoS config, based on trust mode.
         **/
    }

    return 0;
}

int
ops_xp_qos_set_dscp_map(struct ofproto *ofproto_, void *aux,
                        const struct dscp_map_settings *settings)
{
    XP_STATUS status = XP_NO_ERR;
    struct dscp_map_entry *entry;
    uint32_t profile_id = OPS_QOS_DEFAULT_PROFILE_ID;
    int i;

    VLOG_DBG("%s", __FUNCTION__);

    /* TODO
     * When the ofproto and/or aux pointers are NULL, the configuration
     * is for the system default CoS Map table.  Currently,
     * setting the system default CoS Map table is supported.
     * Hence the device Id is hardCoded.
     */

    for (i = 0; i < settings->n_entries; i++) {
        entry = &settings->entries[i];

        VLOG_DBG("%s: ofproto@ %p index=%d color=%d cp=%d lp=%d cos=%d",
                 __FUNCTION__, ofproto_, i, entry->color, entry->codepoint,
                 entry->local_priority, entry->cos);
        status = xpsQosPortIngressSetTrafficClassForL3QosProfile(0,
                                                        profile_id,
                                                        entry->codepoint,
                                                        entry->local_priority);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Failed to set TC for L3 profile, retVal:%d", status);
        }
        status = xpsQosPortIngressSetDropPrecedenceForL3QosProfile(0,
                                                        profile_id,
                                                        entry->codepoint,
                                                        entry->color);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Failed to set DP for L2 profile, retVal:%d", status);
        }
        status = xpsQosPortIngressRemapPriorityForL3QosProfile(0,
                                                      profile_id,
                                                      entry->codepoint,
                                                      0, 0,
                                                      entry->codepoint, 0);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Failed to set Remap Priority for L2 profile, "
                     "retVal : %d", status);
        }
        /* TODO
         * Don't enable DSCP remark globally. This needs to be handled
         * from port QoS config, based on trust mode.
         * */
    }

    return 0;
}

static int
qos_apply_port_schedule_profile(xpsDevice_t dev_id, xpsPort_t port,
                           const struct schedule_profile_settings *s_settings)
{
    XP_STATUS status = XP_NO_ERR;
    struct schedule_profile_entry *sp_entry;
    int i, j;

    VLOG_DBG("%s, ... For xp_port: %d", __FUNCTION__, port);

    /* TODO
     * Validation for 'queue_id' is greater than or equal to
     * the number of supported queues. */

    for (i = 0; i < s_settings->n_entries; i++) {
        sp_entry = s_settings->entries[i];

        VLOG_DBG("... %d q=%d alg=%d wt=%d", i,
                 sp_entry->queue, sp_entry->algorithm, sp_entry->weight);

        switch(sp_entry->algorithm) {
        case ALGORITHM_STRICT:
            status = xpsQosSetQueueSchedulerSP(dev_id, port,
                                               sp_entry->queue, 1);
            if (status != XP_NO_ERR) {
                VLOG_ERR("Failed to set Queue Scheduler type to SP, RC: %d",
                          status);
                return EFAULT;
            }
            break;
        case ALGORITHM_DWRR:
            status = xpsQosSetQueueSchedulerDWRRWeight(dev_id, port,
                                                       sp_entry->queue,
                                                       sp_entry->weight);
            if (status != XP_NO_ERR) {
                VLOG_ERR("Failed to set Queue Scheduler DWRR Weight, RC: %d",
                          status);
                return EFAULT;
            }

            status = xpsQosSetQueueSchedulerDWRR(dev_id, port,
                                             sp_entry->queue, 1);
            if (status != XP_NO_ERR) {
                VLOG_ERR("Failed to set Queue Scheduler type to DWRR, RC: %d",
                          status);
                return EFAULT;
            }
            break;
        default:
            VLOG_ERR("Queue Scheduler Algorithm not supported");
            return EOPNOTSUPP;
        }
    }

    return 0;
}

static int
qos_apply_port_queue_profile(xpsDevice_t dev_id, xpsPort_t port,
                           const struct queue_profile_settings *q_settings)
{
    XP_STATUS status = XP_NO_ERR;
    struct queue_profile_entry *qp_entry;
    uint32_t qmap_idx = 0;
    uint32_t queue_loc = 0;
    uint32_t abs_q_num = 0;
    int i, j;

    VLOG_DBG("%s, ... For xp_port: %d", __FUNCTION__, port);

    /* TODO
     * Validation for 'queue_id' is greater than or equal to
     * the number of supported queues. */

    for (i = 0; i < q_settings->n_entries; i++) {
        qp_entry = q_settings->entries[i];

        VLOG_DBG("... %d q=%d #numof_lp=%d", i,
                 qp_entry->queue, qp_entry->n_local_priorities);

        for (j = 0; j < qp_entry->n_local_priorities; j++) {

            VLOG_DBG("...index_local_prio: %d #lp=%d ", j,
                      qp_entry->local_priorities[j]->local_priority);
            status = xpsQosAqmGetQmapTableIndex(dev_id, port, 0, 0, 0,
                        qp_entry->local_priorities[j]->local_priority,
                                                &qmap_idx, &queue_loc);
            if (status != XP_NO_ERR) {
                VLOG_ERR("Failed to get Qmap table index, retVal:%d", status);
                return EFAULT;
            }

            status = xpsQosAqmGetQueueAbsoluteNumber(dev_id, port, qp_entry->queue,
                                                                       &abs_q_num);
            if (status != XP_NO_ERR) {
                VLOG_ERR("Failed to get Quese absolute number, retVal:%d", status);
                return EFAULT;
            }

            status = xpsQosAqmSetQueueAtQmapIndex(dev_id, qmap_idx, queue_loc,
                                                                  abs_q_num);
            if (status != XP_NO_ERR) {
                VLOG_ERR("Faild to set Queue At QmapIndex, retVal:%d", status);
                return EFAULT;
            }
            status = xpsQosSetQueueSchedulerSPPriority(dev_id,
                               port, qp_entry->queue,
                               qp_entry->local_priorities[j]->local_priority);
            if (status != XP_NO_ERR) {
                VLOG_ERR("Failed to set Queue Scheduler Priority, RC:%d",
                          status);
                return EFAULT;
            }
        }
    }

    return 0;
}

int
ops_xp_qos_apply_qos_profile(struct ofproto *ofproto_, void *aux,
                             const struct schedule_profile_settings *s_settings,
                             const struct queue_profile_settings *q_settings)
{
    const struct ofproto_xpliant *ofproto;
    struct bundle_xpliant *bundle;
    struct ofport_xpliant *port = NULL;
    struct ofport_xpliant *next_port = NULL;
    int ret = 0;


    VLOG_DBG("%s ofproto@ %p aux=%p q_settings=%p s_settings=%p",
               __FUNCTION__, ofproto_, aux, s_settings, q_settings);

    if (ofproto_ == NULL || aux  == NULL) {
        /* When the ofproto and/or aux pointers are NULL, the configuration is
         * for the system default. The both are valid, the configuration is for
         * all the interface members of the bundle identified
         * by the ofproto's struct port pointer passed by the aux parameter.
         * Asic support per port configuration. */
        VLOG_DBG("system default configuration is not supported");
        return EOPNOTSUPP;

    } else {
        ofproto = ops_xp_ofproto_cast(ofproto_);
        bundle = bundle_lookup(ofproto, aux);
        if (bundle) {
            LIST_FOR_EACH_SAFE (port, next_port, bundle_node, &bundle->ports) {

                VLOG_DBG("%s: interface %d, xpdevid %d inside loop ",
                           __FUNCTION__, bundle->name, ofproto->xpdev->id);

                if (s_settings == NULL) {
                    /* Configure bundle without schedule profile with existing
                       global queue profile is not currently supported. */
                    VLOG_DBG("schedule settings is NULL");
                    return EOPNOTSUPP;
                } else {
                    ret = qos_apply_port_schedule_profile(ofproto->xpdev->id,
                                              ops_xp_get_ofport_number(port),
                                              s_settings);
                }

                if (q_settings == NULL) {
                    /* Configure bundle with new schedule profile with existing
                       global default queue profile. No error is returned. */
                    VLOG_DBG("queue settings is NULL");
                } else {
                    ret = qos_apply_port_queue_profile(ofproto->xpdev->id,
                                           ops_xp_get_ofport_number(port),
                                           q_settings);
                }
            }
        } else {
            VLOG_DBG("%s: NO BUNDLE ofproto@ %p aux@%p",
                     __FUNCTION__, ofproto_, aux);
        }
    }

    return ret;
}
