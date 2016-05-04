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

int ops_xp_copp_stats_get(const unsigned int hw_asic_id,
                          const enum copp_protocol_class class,
                          struct copp_protocol_stats *const stats);

int ops_xp_copp_hw_status_get(const unsigned int hw_asic_id,
                              const enum copp_protocol_class class,
                              struct copp_hw_status *const hw_status);

#endif /* ops-xp-copp.h */
