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
 * File: ops-xp-copp.c
 *
 * Purpose: This file contains OpenSwitch CoPP related
 *          application code for the Cavium/XPliant SDK.
 */

#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include "openvswitch/vlog.h"
#include "ops-xp-util.h"
#include "ops-xp-dev.h"
#include "ops-xp-vlan.h"
#include "ops-xp-copp.h"
#include "openXpsPort.h"
#include "openXpsAcm.h"
#include "openXpsPolicer.h"
#include "openXpsQos.h"
#include "openXpsFdb.h"
#include "openXpsCopp.h"

#define XPS_POLICER_ENTRY_INITIALIZER(CIR,PIR) { \
        .cir = (CIR), \
        .pir = (PIR), \
        .cbs = 64000, \
        .pbs = 64000, \
        .colorAware = 0, \
        .dropRed = 1, \
        .dropYellow = 1, \
        .updateResultRed = 0, \
        .updateResultYellow = 0, \
        .updateResultGreen = 0, \
        .polResult = 0 \
    }

#define XPS_COPP_ENTRY_INITIALIZER(policer_id) { \
        .enPolicer = true, \
        .policerId = (policer_id), \
        .updatePktCmd = false, \
        .pktCmd = 0, \
        .updateReasonCode = false, \
        .reasonCode = 0 \
    }

/*
 * Logging module for CoPP.
 */
VLOG_DEFINE_THIS_MODULE(xp_copp);

static uint32_t
policer_rate_xlate(float rate_kbs)
{
    return (uint32_t)(((rate_kbs * 550)/(600 * 8 * 1000)) + 0.5f);
}

static int
ops_xp_copp_config(xpsDevice_t devId, uint32_t queue_number,
                   uint32_t policer_id, uint32_t packet_rate,
                   uint32_t packet_burst, uint32_t rc_list[],
                   uint16_t rc_list_size)
{
    int i, j;
    XP_STATUS status;
    xpsPolicerEntry_t policer_entry =
        XPS_POLICER_ENTRY_INITIALIZER(policer_rate_xlate(packet_rate),
                                      policer_rate_xlate(packet_burst));
    xpCoppEntryData_t copp_entry = XPS_COPP_ENTRY_INITIALIZER(policer_id);
    xpsHashIndexList_t hash_list;

    ovs_assert(rc_list != NULL);
    ovs_assert(rc_list_size != 0);

    status = xpsPolicerAddEntry(devId, XP_ACM_COPP_POLICER, policer_id,
                                &policer_entry);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Error while %s. "
                 "Error code: %d\n", __FUNCTION__, "xpsPolicerAddEntry", status);
    }

    for (j = 0; j < rc_list_size; j++) {
        status = xpsQosCpuScAddReasonCodeToCpuQueueMap(devId, XP_MCPU,
                                                       rc_list[j],
                                                       queue_number);
        if (status != XP_NO_ERR) {
            VLOG_ERR("%s: Error while %s. "
                     "Error code: %d\n", __FUNCTION__, "xpsQosCpuScAddReasonCodeToCpuQueueMap", status);
        }
    }

    for (i = 0; i < XP_MAX_TOTAL_PORTS; i++) {
        for (j = 0; j < rc_list_size; j++) {
#if 0
            status = xpsCoppRemoveEntry(devId, i, rc_list[j]);
            if (status != XP_NO_ERR) {
                VLOG_ERR("%s: Error while %s. "
                         "Error code: %d\n", __FUNCTION__, "xpsCoppRemoveEntry", status);
            }
#endif
            status = xpsCoppAddEntry(devId, i, rc_list[j], copp_entry,
                                     &hash_list);
            if (status != XP_NO_ERR) {
                VLOG_ERR("%s: Error while %s. "
                         "Error code: %d\n", __FUNCTION__, "xpsCoppAddEntry", status);
            }
        }
    }

    return 0;
}

int
ops_xp_copp_default_init(xpsDevice_t devId, uint32_t queue_number,
                         uint32_t policer_id, uint32_t packet_rate,
                         uint32_t packet_burst)
{
    int i, j;
    XP_STATUS status;
    xpsPolicerEntry_t policer_entry =
        XPS_POLICER_ENTRY_INITIALIZER(policer_rate_xlate(packet_rate),
                                      policer_rate_xlate(packet_burst));
    xpCoppEntryData_t copp_entry = XPS_COPP_ENTRY_INITIALIZER(policer_id);
    xpsHashIndexList_t hash_list;

    status = xpsPolicerAddEntry(devId, XP_ACM_COPP_POLICER, policer_id,
                                &policer_entry);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Error while %s. "
                 "Error code: %d\n", __FUNCTION__, "xpsPolicerAddEntry", status);
    }

    /* By default all reason codes are mapped to a single CPU Queue */
    for (j = 0; j < OPS_XP_RC_MAX; j++) {
        status = xpsQosCpuScAddReasonCodeToCpuQueueMap(devId, XP_MCPU, j,
                                                       queue_number);
        if (status != XP_NO_ERR) {
            VLOG_ERR("%s: Error while %s. "
                     "Error code: %d\n", __FUNCTION__, "xpsQosCpuScAddReasonCodeToCpuQueueMap", status);
        }
    }

    /* By default add all possible CoPP entries to apply default policer */
    for (i = 0; i < XP_MAX_TOTAL_PORTS; i++) {
        for (j = 0; j < OPS_XP_RC_MAX; j++) {
#if 0
            status = xpsCoppAddEntry(devId, i, j, copp_entry, &hash_list);
            if (status != XP_NO_ERR) {
                VLOG_ERR("%s: Error while %s(%d,%d). "
                         "Error code: %d\n", __FUNCTION__, "xpsCoppAddEntry", i, j, status);
            }
#endif
        }
    }

    return 0;
}

int
ops_xp_copp_bcarp_init(xpsDevice_t devId, uint32_t queue_number,
                       uint32_t policer_id, uint32_t packet_rate,
                       uint32_t packet_burst)
{
    uint32_t rc_list[] = {
        XP_BRIDGE_RC_IVIF_ARPIGMPICMP_CMD
    };

    return ops_xp_copp_config(devId, queue_number, policer_id, packet_rate,
                              packet_burst, rc_list, ARRAY_SIZE(rc_list));
}

int
ops_xp_copp_ucarp_init(xpsDevice_t devId, uint32_t queue_number,
                       uint32_t policer_id, uint32_t packet_rate,
                       uint32_t packet_burst)
{
    uint32_t rc_list[] = {
        XP_ROUTE_RC_ARP_MY_UNICAST
    };

    return ops_xp_copp_config(devId, queue_number, policer_id, packet_rate,
                              packet_burst, rc_list, ARRAY_SIZE(rc_list));
}

int
ops_xp_copp_myip_init(xpsDevice_t devId, uint32_t queue_number,
                      uint32_t policer_id, uint32_t packet_rate,
                      uint32_t packet_burst)
{
    uint32_t rc_list[] = {
        XP_ROUTE_RC_HOST_TABLE_HIT,
        XP_ROUTE_RC_TTL1_OR_IP_OPTION
    };

    return ops_xp_copp_config(devId, queue_number, policer_id, packet_rate,
                              packet_burst, rc_list, ARRAY_SIZE(rc_list));
}

int
ops_xp_copp_lacp_init(xpsDevice_t devId, uint32_t queue_number,
                      uint32_t policer_id, uint32_t packet_rate,
                      uint32_t packet_burst)
{
    XP_STATUS status = XP_NO_ERR;
    macAddr_t keyMAC;
    xpsHashIndexList_t hash_list;
    uint32_t rc_list[] = {
        XP_IVIF_RC_LACP
    };

    /* XDK APIs use MAC in reverse order. */
    ops_xp_mac_copy_and_reverse(keyMAC, eth_addr_lacp.ea);

    status = xpsFdbAddControlMacEntry(devId, XPS_VLANID_MIN, keyMAC,
                                      XP_IVIF_RC_LACP, &hash_list);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Error in inserting control MAC entry for LACP. "
                 "Error code: %d\n", __FUNCTION__, status);
    }

    return ops_xp_copp_config(devId, queue_number, policer_id, packet_rate,
                              packet_burst, rc_list, ARRAY_SIZE(rc_list));
}

int
ops_xp_copp_lldp_init(xpsDevice_t devId, uint32_t queue_number,
                      uint32_t policer_id, uint32_t packet_rate,
                      uint32_t packet_burst)
{
    XP_STATUS status = XP_NO_ERR;
    macAddr_t keyMAC;
    xpsHashIndexList_t hash_list;
    static const struct eth_addr eth_addr_lldp[] OVS_UNUSED = {
        { { 0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E } },
        { { 0x01, 0x80, 0xC2, 0x00, 0x00, 0x03 } },
//        { { 0x01, 0x80, 0xC2, 0x00, 0x00, 0x00 } },
    };
    int i;
    uint32_t rc_list[] = {
        XP_IVIF_RC_LLDP
    };

    for (i = 0; i < ARRAY_SIZE(eth_addr_lldp); i++) {
        /* XDK APIs use MAC in reverse order. */
        ops_xp_mac_copy_and_reverse(keyMAC, eth_addr_lldp[i].ea);

        status = xpsFdbAddControlMacEntry(devId, XPS_VLANID_MIN, keyMAC,
                                          XP_IVIF_RC_LLDP, &hash_list);
        if (status != XP_NO_ERR) {
            VLOG_ERR("%s: Error in inserting control MAC entry for LLDP. "
                     "Error code: %d\n", __FUNCTION__, status);
        }
    }

    return ops_xp_copp_config(devId, queue_number, policer_id, packet_rate,
                              packet_burst, rc_list, ARRAY_SIZE(rc_list));
}

int
ops_xp_copp_stp_init(xpsDevice_t devId, uint32_t queue_number,
                     uint32_t policer_id, uint32_t packet_rate,
                     uint32_t packet_burst)
{
    XP_STATUS status = XP_NO_ERR;
    macAddr_t keyMAC;
    xpsHashIndexList_t hash_list;
    uint32_t rc_list[] = {
        XP_IVIF_RC_STP_BPDU
    };

    /* XDK APIs use MAC in reverse order. */
    ops_xp_mac_copy_and_reverse(keyMAC, eth_addr_stp.ea);

    status = xpsFdbAddControlMacEntry(devId, XPS_VLANID_MIN, keyMAC,
                                      XP_IVIF_RC_STP_BPDU, &hash_list);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Error in inserting control MAC entry for STP. "
                 "Error code: %d\n", __FUNCTION__, status);
    }

    return ops_xp_copp_config(devId, queue_number, policer_id, packet_rate,
                              packet_burst, rc_list, ARRAY_SIZE(rc_list));
}

int
ops_xp_copp_unknownip_init(xpsDevice_t devId, uint32_t queue_number,
                           uint32_t policer_id, uint32_t packet_rate,
                           uint32_t packet_burst)
{
    uint32_t rc_list[] = {
        XP_ROUTE_RC_NH_TABLE_HIT,
        XP_ROUTE_RC_UC_TABLE_MISS,
        XP_ROUTE_RC_ROUTE_NOT_POSSIBLE
    };
    return ops_xp_copp_config(devId, queue_number, policer_id, packet_rate,
                              packet_burst, rc_list, ARRAY_SIZE(rc_list));
}

int
ops_xp_copp_fdb_init(xpsDevice_t devId, uint32_t queue_number,
                     uint32_t policer_id, uint32_t packet_rate,
                     uint32_t packet_burst)
{
    uint32_t rc_list[] = {
        XP_BRIDGE_MAC_SA_NEW,
        XP_BRIDGE_MAC_SA_MOVE,
        XP_BRIDGE_RC_IVIF_SA_MISS
    };
    return ops_xp_copp_config(devId, queue_number, policer_id, packet_rate,
                              packet_burst, rc_list, ARRAY_SIZE(rc_list));
}

/*
 * Macro for populating the structure ops_xp_copp_class_config_t with the
 * default values specified in the file ops-xp-copp-defaults.h.
 */
#define OPS_XP_COPP_CLASS(class,name,queue,rate,burst,init_func) {\
                          .packet_name=(name),\
                          .queue_number=(queue), \
                          .policer_id=(XP_##class) + 1, \
                          .packet_rate=(rate), \
                          .packet_burst=(burst), \
                          .packet_init_func=&(init_func)},

/*
 * Static array for CoPP packet class configurations. The number of entries in
 * this array is equal to the number of control plane packet classes given
 * in file ops-xp-copp-defaults.h.
 */
static struct ops_xp_copp_class_config_t xp_copp_packet_class_rules[] =
{
#include "ops-xp-copp-defaults.h"
};

/*
 * If OPS_XP_COPP_CLASS is already defined, then undefine it.
 */
#undef OPS_XP_COPP_CLASS


void
ops_xp_copp_init(xpsDevice_t devId)
{
    XP_STATUS status;
    int rc, i;

    status = xpsSetCountMode(devId, XP_ACM_COPP_POLICER, XP_ACM_POLICING, 8, 0,
                             0, 15);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Error while %s. "
                 "Error code: %d\n", __FUNCTION__, "xpsSetCountMode", status);
    }

    for (i = 0; i < ARRAY_SIZE(xp_copp_packet_class_rules); i++) {
        if (xp_copp_packet_class_rules[i].packet_init_func) {
            rc = (xp_copp_packet_class_rules[i].packet_init_func)(devId,
                    xp_copp_packet_class_rules[i].queue_number,
                    xp_copp_packet_class_rules[i].policer_id,
                    xp_copp_packet_class_rules[i].packet_rate,
                    xp_copp_packet_class_rules[i].packet_burst);
            if (rc) {
                /* TODO: Add error message here */
            }
        } else {
            VLOG_ERR("%s: Error!!! no packet_init_func assigned.",
                     __FUNCTION__);
        }
    }

    /* Enable BUM policer on all ports */
    for (i = 0; i < XP_MAX_TOTAL_PORTS; i++) {
        status = xpsPortSetField(devId, i, XPS_PORT_BUM_POLICER_EN, 1);
        if (status != XP_NO_ERR) {
            VLOG_ERR("%s: Error while %s. "
                     "Error code: %d\n", __FUNCTION__, "xpsPortSetField", status);
        }
    }
}

/*
 * copp_packet_class_mapper
 *
 * This function takes in the ops packet class as the input and converts it to
 * the copp packet class enum
 */
static xp_copp_packet_class_t
xp_copp_packet_class_mapper(enum copp_protocol_class ops_class)
{
    switch(ops_class) {
        case COPP_ARP_BROADCAST:
            return XP_COPP_ARP_BROADCAST;
        case COPP_DEFAULT_UNKNOWN:
            return XP_COPP_DEFAULT_UNKNOWN;
        case COPP_LACP:
            return XP_COPP_LACP;
        case COPP_LLDP:
            return XP_COPP_LLDP;
        case COPP_STP_BPDU:
            return XP_COPP_STP_BPDU;
        case COPP_UNKNOWN_IP_UNICAST:
            return XP_COPP_UNKNOWN_IP_UNICAST;
        default:
            return XP_COPP_MAX;
    }
}

/*
 * ops_xp_copp_stats_get
 *
 * This is the implementaion of the function interfacing with switchd,
 * which is polled every 5000 ms by switchd. This function returns the
 * statistics corresponding to a particular protocol.
 */
int
ops_xp_copp_stats_get(const unsigned int hw_asic_id,
                      const enum copp_protocol_class class,
                      struct copp_protocol_stats *const stats)
{
    xp_copp_packet_class_t xp_copp_class;
    struct xpliant_dev *xpdev;
    uint32_t queue, wrap;
    XP_STATUS status;
    int rc;

    VLOG_DBG("%s", __FUNCTION__);

    /* Check for stats pointer passed not being NULL */
    if (stats == NULL) {
        VLOG_ERR("%s: Stats pointer passed to function is NULL", __FUNCTION__);
        return EINVAL;
    }

    /* Map the incoming enum to our copp class enum */
    xp_copp_class = xp_copp_packet_class_mapper(class);
    if (xp_copp_class == XP_COPP_MAX) {
        VLOG_DBG("CoPP packet class %d not supported.", class);
        return EOPNOTSUPP;
    }

    /* Get pointer to XPliant device based on ASIC ID */
    xpdev = ops_xp_dev_by_id((xpsDevice_t)hw_asic_id);
    if (xpdev == NULL) {
        VLOG_ERR("%s: ASIC with ID %u not initialized",
                 __FUNCTION__, hw_asic_id);
        return EINVAL;
    }

    /* Get CPU queue number for CoPP traffic class */
    queue = xp_copp_packet_class_rules[xp_copp_class].queue_number;

    XP_LOCK();
    status = xpsQosQcGetQueueFwdPacketCountForPort(xpdev->id, CPU_PORT, queue,
                                                   &stats->packets_passed,
                                                   &wrap);
    if (status != XP_NO_ERR) {
        VLOG_WARN("%s: Failed to get packets passed count for %d",
                 __FUNCTION__, xp_copp_class);
    }

    status = xpsQosQcGetQueueFwdByteCountForPort(xpdev->id, CPU_PORT, queue,
                                                 &stats->bytes_passed,
                                                 &wrap);
    if (status != XP_NO_ERR) {
        VLOG_WARN("%s: Failed to get bytes passed count for %d",
                 __FUNCTION__, xp_copp_class);
    }

    status = xpsQosQcGetQueueDropPacketCountForPort(xpdev->id, CPU_PORT, queue,
                                                    &stats->packets_dropped,
                                                    &wrap);
    if (status != XP_NO_ERR) {
        VLOG_WARN("%s: Failed to get packets dropped count for %d",
                 __FUNCTION__, xp_copp_class);
    }

    status = xpsQosQcGetQueueDropByteCountForPort(xpdev->id, CPU_PORT, queue,
                                                  &stats->bytes_dropped,
                                                  &wrap);
    if (status != XP_NO_ERR) {
        VLOG_WARN("%s: Failed to get bytes dropped count for %d",
                 __FUNCTION__, xp_copp_class);
    }
    XP_UNLOCK();

    ops_xp_dev_free(xpdev);

    return 0;
}

/*
 * ops_xp_copp_hw_status_get
 *
 * This is the implementaion of the function interfacing with switchd,
 * which is polled every 5000 ms by switchd. This function returns the
 * hw_status info like rate, burst, local_priority corresponding to a particular
 * protocol.
 */
int
ops_xp_copp_hw_status_get(const unsigned int hw_asic_id,
                          const enum copp_protocol_class class,
                          struct copp_hw_status *const hw_status)
{
    xp_copp_packet_class_t xp_copp_class;

    VLOG_DBG("%s", __FUNCTION__);

    /* Check for stats pointer passed not being NULL */
    if (hw_status == NULL) {
        VLOG_ERR("%s: Hardware status pointer passed to function is NULL",
                 __FUNCTION__);
        return EINVAL;
    }

    /* Map the incoming enum to our copp class enum */
    xp_copp_class = xp_copp_packet_class_mapper(class);
    if (xp_copp_class == XP_COPP_MAX) {
        VLOG_ERR("%s: CoPP packet class %d not supported.",
                 __FUNCTION__, class);
        return EOPNOTSUPP;
    }

    hw_status->rate = xp_copp_packet_class_rules[xp_copp_class].packet_rate / 12;
    hw_status->burst = xp_copp_packet_class_rules[xp_copp_class].packet_burst / 12;
    hw_status->local_priority = xp_copp_packet_class_rules[xp_copp_class].queue_number;

    return 0;
}
