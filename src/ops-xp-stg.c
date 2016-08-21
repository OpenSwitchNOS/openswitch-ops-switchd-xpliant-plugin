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
 * File: ops-xp-stg.c
 *
 * Purpose: This file contains OpenSwitch spanning tree groups related
 *          application code for the Cavium/XPliant SDK.
 */

#include <unistd.h>
#include "openvswitch/vlog.h"
#include "ofproto/ofproto-provider.h"
#include "ops-xp-port.h"
#include "ops-xp-vlan.h"
#include "ops-xp-stg.h"
#include "ops-xp-netdev.h"
#include "openXpsStp.h"
#include "openXpsVlan.h"

VLOG_DEFINE_THIS_MODULE(xp_stg);

typedef enum ops_stg_port_state {
    OPS_STG_PORT_STATE_DISABLED = 0,
    OPS_STG_PORT_STATE_BLOCKED,
    OPS_STG_PORT_STATE_LEARNING,
    OPS_STG_PORT_STATE_FORWARDING,
    OPS_STG_PORT_STATE_NOT_SET
} ops_stg_port_state_t;


static bool
ops_xp_get_hw_port_state_from_ops_state(int ops_state, xpsStpState_e *hw_state)
{
    if (!hw_state) {
        return false;
    }

    switch (ops_state) {

    case OPS_STG_PORT_STATE_BLOCKED:
        *hw_state = SPAN_STATE_BLOCK;
        return true;
    case OPS_STG_PORT_STATE_DISABLED:
        *hw_state = SPAN_STATE_DISABLE;
        return true;
    case OPS_STG_PORT_STATE_LEARNING:
        *hw_state = SPAN_STATE_LEARN;
        return true;
    case OPS_STG_PORT_STATE_FORWARDING:
        *hw_state = SPAN_STATE_FORWARD;
        return true;
    default:
        break;
    }

    return false;
}

static bool
ops_xp_get_ops_port_state_from_hw_state(xpsStpState_e hw_state, int *ops_state)
{
    if (!ops_state) {
        return false;
    }

    switch (hw_state) {

    case SPAN_STATE_BLOCK:
        *ops_state = OPS_STG_PORT_STATE_BLOCKED;
        return true;
    case SPAN_STATE_DISABLE:
        *ops_state = OPS_STG_PORT_STATE_DISABLED;
        return true;
    case SPAN_STATE_LEARN:
        *ops_state = OPS_STG_PORT_STATE_LEARNING;
        return true;
    case SPAN_STATE_FORWARD:
        *ops_state = OPS_STG_PORT_STATE_FORWARDING;
        return true;
    default:
        break;
    }

    return false;
}

int
ops_xp_create_stg(int *p_stg)
{
    XP_STATUS status;
    xpsStp_t stp_id;

    status = xpsStpCreate(&stp_id);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Could not create STG. Error: %d", __FUNCTION__, status);
        return EFAULT;
    }

    *p_stg = (int)stp_id;

    VLOG_INFO("%s: Created STG: %d", __FUNCTION__, stp_id);

    return 0;
}

int
ops_xp_delete_stg(int stg)
{
    XP_STATUS status;

    status = xpsStpDestroy((xpsStp_t)stg);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Could not destroy STG: %d. Error: %d",
                 __FUNCTION__, stg, status);
        return EFAULT;
    }

    VLOG_INFO("%s: Removed STG: %d", __FUNCTION__, stg);

    return 0;
}

int
ops_xp_add_stg_vlan(int stg, int vid)
{
    XP_STATUS status;

    if (!ops_xp_is_vlan_id_valid((xpsVlan_t)vid)) {
        VLOG_ERR("%s: Invalid VLAN ID: %d passed",
                 __FUNCTION__, vid, status);
        return EINVAL;
    }

    status = xpsVlanBindStp(0, (xpsVlan_t)vid, (xpsStp_t)stg);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Could not add VLAN: %d to STG: %d. Error: %d",
                 __FUNCTION__, vid, stg, status);
        return EFAULT;
    }

    VLOG_INFO("%s: Added VLAN: %d to STG: %d", __FUNCTION__, vid, stg);

    return 0;
}

int
ops_xp_remove_stg_vlan(int stg, int vid)
{
    XP_STATUS status;

    if (!ops_xp_is_vlan_id_valid((xpsVlan_t)vid)) {
        VLOG_ERR("%s: Invalid VLAN ID: %d passed",
                 __FUNCTION__, vid, status);
        return EINVAL;
    }

    status = xpsVlanUnbindStp(0, (xpsVlan_t)vid, (xpsStp_t)stg);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Could not remove VLAN: %d from STG: %d. Error: %d",
                 __FUNCTION__, vid, stg, status);
        return EFAULT;
    }

    VLOG_INFO("%s: Removed VLAN: %d from STG: %d", __FUNCTION__, vid, stg);

    return 0;
}

int
ops_xp_set_stg_port_state(char *port_name, int stg, int stp_state,
                          bool port_stp_set OVS_UNUSED)
{
    XP_STATUS status;
    struct netdev_xpliant *netdev;
    xpsStpState_e hw_stp_state;

    netdev = ops_xp_netdev_from_name(port_name);
    if (!netdev) {
        VLOG_ERR("%s: Could not get netdev for port: %s",
                 __FUNCTION__, port_name);
        return ENOENT;
    }

    if (!ops_xp_get_hw_port_state_from_ops_state(stp_state, &hw_stp_state)) {
        VLOG_ERR("%s: Could not OPS STP state to HW state "
                 "for STG: %d port: %s.", __FUNCTION__, stg, port_name);
        return EFAULT;
    }

    status = xpsStpSetState(netdev->xpdev->id, (xpsStp_t)stg, netdev->ifId,
                            hw_stp_state);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Could not set XP STP state: %d for STG: %d port: %s. "
                 "Error: %d",
                 __FUNCTION__, hw_stp_state, stg, port_name, status);
        return EFAULT;
    }

    VLOG_INFO("%s: Set XP STP state: %d for STG: %d port: %s.",
              __FUNCTION__, hw_stp_state, stg, port_name);

    return 0;
}

int
ops_xp_get_stg_port_state(char *port_name, int stg, int *p_stp_state)
{
    XP_STATUS status;
    struct netdev_xpliant *netdev;
    xpsStpState_e hw_stp_state;

    VLOG_DBG("%s", __FUNCTION__);

    netdev = ops_xp_netdev_from_name(port_name);
    if (!netdev) {
        VLOG_INFO("%s: Could not get netdev for port: %s",
                  __FUNCTION__, port_name);
        return ENOENT;
    }

    status = xpsStpGetState(netdev->xpdev->id, (xpsStp_t)stg, netdev->ifId,
                            &hw_stp_state);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Could not get STP state for STG: %d port: %s. Error: %d",
                 __FUNCTION__, stg, port_name, status);
        return EFAULT;
    }

    if (!ops_xp_get_ops_port_state_from_hw_state(hw_stp_state, p_stp_state)) {
        VLOG_ERR("%s: Could not convert HW STP state to OPS state "
                 "for STG: %d port: %s.", __FUNCTION__, stg, port_name);
        return EFAULT;
    }

    return 0;
}

int
ops_xp_get_stg_default(int *p_stg)
{
    XP_STATUS status;
    xpsStp_t stp_id;

    status = xpsStpGetDefault(0, &stp_id);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Could not get default STG. Error: %d",
                 __FUNCTION__, status);
        return EFAULT;
    }

    *p_stg = (int)stp_id;

    VLOG_DBG("%s Got default STG: %d", __FUNCTION__, stp_id);

    return 0;
}
