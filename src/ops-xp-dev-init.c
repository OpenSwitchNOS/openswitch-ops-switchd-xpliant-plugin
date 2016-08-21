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
 * File: ops-xp-dev-init.c
 *
 * Purpose: This file contains OpenSwitch XPliant device initialization related
 *          application code for the Cavium/XPliant SDK.
 */

#if !defined(OPS_AS7512) && !defined(OPS_XP_SIM)
#define OPS_XP_SIM
#endif

#include <errno.h>

#include "ops-xp-classifier.h"
#include "ops-xp-dev.h"
#include "ops-xp-dev-init.h"
#include "ops-xp-util.h"
#include <openvswitch/vlog.h>
#include "dirs.h"

#include "openXpsSalInterface.h"
#include "openXpsInit.h"
#include "openXpsPlatformInterface.h"

#include "openXpsVlan.h"
#include "openXpsPort.h"

VLOG_DEFINE_THIS_MODULE(xp_dev_init);

#if defined(OPS_AS7512)
#define OPS_XPPLATFORM_NAME         "as7512"
#elif defined(OPS_XP_SIM)
#define OPS_XPPLATFORM_NAME         "xp-sim"
#endif

const char **moduleNames = NULL;

static xpsDevConfigStruct_t defaultConfig = {
    XP_ROUTE_CENTRIC_SINGLE_PIPE_PROFILE,     // SELECT PROFILE
    SKU_128X10,                               // default speed
#ifdef OPS_XP_SIM
    XPS_DAL_WHITEMODEL,
    1,
#else
    XPS_DAL_HARDWARE,
    0
#endif
};

int
ops_xp_sdk_init(xpInitType_t initType)
{
    int status = XP_NO_ERR;

    moduleNames = xpsSdkLoggerInit();
    if (moduleNames == NULL) {
        VLOG_ERR("xpsLoggerInit Failed");
        return XP_ERR_NULL_POINTER;
    }

    status = xpsSdkLogVersion();
    if (status) {
        VLOG_ERR("xpsSdkLogVersion Failed");
        return status;
    }

    status = xpsSdkLogToFile(xasprintf("/var/log/%s.log", "xdk"));
    if (status) {
        VLOG_ERR("xpsSdkLogFile Failed");
        return status;
    }

#ifdef OPS_XP_SIM
    xpsSetSalType(XP_SAL_WM_TYPE);
#else
    xpsSetSalType(XP_SAL_HW_TYPE);
#endif

    xpsSalDefaultInit();

#ifndef OPS_XP_SIM
    status = xpPlatformInit(OPS_XPPLATFORM_NAME, initType, false, NULL);
    if (status) {
        VLOG_ERR("xpPlatformInit() Failed with err: %d", status);
    }
#endif

    status = xpsSdkInit(RANGE_ROUTE_CENTRIC, initType);
    if (status) {
        VLOG_ERR("xpsSdkInit Failed");
    }

    return status;
}

XP_STATUS
ops_xp_sdk_dev_add(xpsDevice_t devId, xpInitType_t initType,
                   xpsDevConfigStruct_t *devConfig)
{
    XP_STATUS status;
    VLOG_INFO("%s (devId = 0x%x)\n", __FUNCTION__, devId);

    status = xpsSdkDefaultDevInit(devId, initType, devConfig,
                                  ops_xp_packet_if_type_get());
    if (status) {
        VLOG_ERR("xpsSdkDefaultDevInit(devId = 0x%x) failed.\n", devId);
        return status;
    }

    VLOG_INFO("%s (devId = 0x%x) done.\n", __FUNCTION__, devId);


    return XP_NO_ERR;
}

#ifdef OPS_XP_SIM
XP_STATUS
ops_xp_sdk_dev_remove(xpsDevice_t devId)
{
    XP_STATUS status = XP_NO_ERR;

    status = xpsSdkDevDeInit(devId);
    if (status) {
        VLOG_ERR("xpsSdkDevDeInit failed.\n");
    }

    VLOG_INFO("%s (devId = 0x%x) done.\n", __FUNCTION__, devId);

    return status;
}
#endif

XP_STATUS
ops_xp_dev_config(xpsDevice_t deviceId, void *arg)
{
    XP_STATUS status = XP_NO_ERR;
    struct xpliant_dev *dev = NULL;
    xpInitType_t initType = *((xpInitType_t *)arg);

    /* XDK init for specific device and type */
    status = ops_xp_sdk_dev_add(deviceId, initType, &defaultConfig);
    if (status != XP_NO_ERR) {
        VLOG_ERR("xpliant_sdk_dev_add Failed.. Error #%1d\n", status);
        return status;
    }
    VLOG_INFO("XP SDK device added!\n");

    /* Allocate XP device instance */
    dev = ops_xp_dev_alloc(deviceId);
    if (dev == NULL) {
        VLOG_ERR("Unable to allocate XP device instance\n");
        return XP_ERR_INIT;
    }
    VLOG_INFO("XP device instance allocated!\n");

    /*acl related init*/
    ops_xp_cls_init(deviceId);
    return status;
}

XP_STATUS
ops_xp_sdk_log_level_set(const char *level_name)
{
    int id;
    for (id = 0; moduleNames[id] != NULL; id++) {
        xpsSdkSetLoggerOptions(id, CONST_CAST(char*, level_name));
    }
    return XP_NO_ERR;
}
