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
 * File: ops-xp-dev-init.h
 *
 * Purpose: This file provides public definitions for OpenSwitch XPliant device
 *          initialization related application code for the Cavium/XPliant SDK.
 */

#ifndef OPS_XP_DEV_INIT_H
#define OPS_XP_DEV_INIT_H 1

#include "openXpsInit.h"

int ops_xp_sdk_init(xpInitType_t initType);

XP_STATUS ops_xp_sdk_dev_add(xpsDevice_t devId, xpInitType_t initType,
                             xpsDevConfigStruct_t *devConfig);

#ifdef OPS_XP_SIM
XP_STATUS ops_xp_sdk_dev_remove(xpsDevice_t devId);
#endif

XP_STATUS ops_xp_dev_config(xpsDevice_t deviceId, void *arg);

XP_STATUS ops_xp_sdk_log_level_set(const char *level_name);

#endif /* ops-xp-dev-init.h */
