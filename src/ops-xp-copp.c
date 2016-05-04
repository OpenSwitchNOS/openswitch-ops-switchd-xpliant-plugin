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
#include "ops-xp-copp.h"

/*
 * Logging module for CoPP.
 */
VLOG_DEFINE_THIS_MODULE(xp_copp);

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
    VLOG_DBG("%s", __FUNCTION__);

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
    VLOG_DBG("%s", __FUNCTION__);

    return 0;
}
