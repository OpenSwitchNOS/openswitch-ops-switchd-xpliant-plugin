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
 * File: ops-xp-copp.h
 *
 * Purpose: This file provides public definitions for OpenSwitch CoPP
 *          related application code for the Cavium/XPliant SDK.
 */

#ifndef OPS_XP_COPP_H
#define OPS_XP_COPP_H 1

#include <stdint.h>
#include <ofproto/ofproto.h>
#include "copp-asic-provider.h"
#include "openXpsTypes.h"
#include "openXpsReasonCodeTable.h"

#define XP_IVIF_RC_LLDP                         20
#define XP_IVIF_RC_LACP                         21
#define XP_IVIF_RC_STP_BPDU                     22

/*
 * Queue numbers as per the control packet classes.
 */
#define OPS_XP_COPP_QUEUE_CRITICAL              8  /* Q8 */
#define OPS_XP_COPP_QUEUE_IMPORTANT             7  /* Q7 */
#define OPS_XP_COPP_QUEUE_BPDU                  6  /* Q6 */
#define OPS_XP_COPP_QUEUE_MY_IP                 5  /* Q5 */
#define OPS_XP_COPP_QUEUE_FDB                   4  /* Q4 */
#define OPS_XP_COPP_QUEUE_UNKNOWN_IP            3  /* Q3 */
#define OPS_XP_COPP_QUEUE_SFLOW                 2  /* Q2 */
#define OPS_XP_COPP_QUEUE_DEFAULT               1  /* Q1 */
#define OPS_XP_COPP_QUEUE_EXCEPTION             0  /* Q0 */
#define OPS_XP_COPP_QUEUE_MAX                   OPS_XP_COPP_QUEUE_CRITICAL
#define OPS_XP_COPP_QUEUE_MIN                   OPS_XP_COPP_QUEUE_EXCEPTION

#define OPS_XP_RC_MAX                           1024

/*
 * Macro for generating the enum names for different control plane
 * packets.
 */
#define OPS_XP_COPP_CLASS(class,name,queue,rate,burst,init_func) XP_##class,

/*
 * Enum for storing the indexing id for different control place packets. The
 * following points need to be considered while adding entries to this
 * enum:-
 * 1. The sequence of rules is important. If a packet matches two sets of
 *    rules within the CoPP group, then the rule appearing earlier in the
 *    enum will get precedence with respect to the action chosen for that
 *    packet. The sequence of the control plane packets must be correctly
 *    specified in the file ops-copp-defaults.h.
 */
typedef enum {
#include "ops-xp-copp-defaults.h"
    XP_COPP_MAX
} xp_copp_packet_class_t;

/*
 * If OPS_XP_COPP_CLASS is already defined, then undefine it.
 */
#undef OPS_XP_COPP_CLASS

/*
 * Function pointer structure for global control packet class rules.
 */
typedef int (*copp_packet_class_init_t)(xpsDevice_t devId, uint32_t queue_number,
             uint32_t policer_id,uint32_t packet_rate, uint32_t packet_burst);

struct ops_xp_copp_class_config_t {
    uint32_t policer_id; /* Policer id for packet class */
    const char *packet_name; /* Name of the control plane packet class */
    uint32_t queue_number; /* Value of the current CPU QoS queue for this control plane packet */
    uint32_t packet_rate; /* Value of the current rate for this control plane packet class */
    uint32_t packet_burst; /* Value of the current burst for this control plane packet. */
    copp_packet_class_init_t packet_init_func; /* Function pointer to the control packet class global pipeline rules. */
};

void ops_xp_copp_init(xpsDevice_t devId);

int ops_xp_copp_stats_get(const unsigned int hw_asic_id,
                          const enum copp_protocol_class class,
                          struct copp_protocol_stats *const stats);

int ops_xp_copp_hw_status_get(const unsigned int hw_asic_id,
                              const enum copp_protocol_class class,
                              struct copp_hw_status *const hw_status);

#endif /* ops-xp-copp.h */
