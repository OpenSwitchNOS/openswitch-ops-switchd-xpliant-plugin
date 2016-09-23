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
 * File: ops-xp-qos.h
 *
 * Purpose: This file provides public definitions for OpenSwitch QoS related
 *          application code for the Cavium/XPliant SDK.
 */

#ifndef OPS_XP_QOS_H
#define OPS_XP_QOS_H 1

#include <stdio.h>
#include <stdlib.h>
#include "ofproto/ofproto-provider.h"
#include "qos-asic-provider.h"

#define OPS_QOS_NUM_DEI_VALUES     2
#define OPS_QOS_MAX_QUEUE          8

int ops_xp_qos_set_port_qos_cfg(struct ofproto *ofproto_, void *aux,
                                const struct qos_port_settings *settings);

int ops_xp_qos_set_cos_map(struct ofproto *ofproto_, void *aux,
                           const struct cos_map_settings *settings);

int ops_xp_qos_set_dscp_map(struct ofproto *ofproto_, void *aux,
                            const struct dscp_map_settings *settings);

int ops_xp_qos_apply_qos_profile(struct ofproto *ofproto_, void *aux,
                                 const struct schedule_profile_settings *s_settings,
                                 const struct queue_profile_settings *q_settings);

#endif /* ops-xp-qos.h */
