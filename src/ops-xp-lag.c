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
 * File: ops-xp-lag.c
 *
 * Purpose: This file contains OpenSwitch LAG related application code for the
 *          Cavium/XPliant SDK.
 */

#include <stdint.h>
#include <stdlib.h>
#include <errno.h>

#include <openvswitch/vlog.h>
#include <ofproto/bond.h>
#include <netdev.h>

#include "ops-xp-lag.h"
#include "ops-xp-host.h"
#include "ops-xp-netdev.h"
#include "openXpsLag.h"
#include "openXpsInterface.h"
#include "openXpsL3.h"

VLOG_DEFINE_THIS_MODULE(xp_lag);

static int lag_attach_port_on_hw(xpsDevice_t dev_id,
                                 xpsInterfaceId_t lag_id,
                                 xpsInterfaceId_t if_id);
static int lag_detach_port_on_hw(xpsDevice_t dev_id,
                                 xpsInterfaceId_t lag_id,
                                 xpsInterfaceId_t if_id);
static bool lag_is_port_attached(xpsInterfaceId_t lag_id,
                                 xpsInterfaceId_t if_id);


/* Creates a lag on hardware. */
int
ops_xp_lag_create(const char *lag_name, xpsInterfaceId_t *lag_id)
{
    XP_STATUS status;

    ovs_assert(lag_name);
    ovs_assert(lag_id);

    status = xpsLagCreate(lag_id);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Could not create LAG %s on hardwrae. Error code: %d\n",
                 __FUNCTION__, lag_name, status);
        return EFAULT;
    }

    VLOG_INFO("%s: Created LAG %s(IF ID: %u) on hardwrae.",
              __FUNCTION__, lag_name, *lag_id);

    return 0;
}

/* Destroys LAG on hardware. */
void
ops_xp_lag_destroy(xpsDevice_t dev_id, const char* lag_name,
                   xpsInterfaceId_t lag_id)
{
    XP_STATUS status;
    xpsLagPortIntfList_t if_list;
    int i;

    ovs_assert(lag_name);

    status = xpsLagGetPortIntfList(lag_id, &if_list);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Could not get list of LAG %s interfaces. "
                 "Error code: %d\n", __FUNCTION__, lag_name, status);
        return;
    }

    /* Detach all members from LAG. */
    for (i = 0; i < if_list.size; i++) {
        lag_detach_port_on_hw(dev_id, lag_id, if_list.portIntf[i]);
    }

    status = xpsLagDeploy(dev_id, lag_id, AUTO_DIST_ENABLE);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Could not deploy LAG %s changes to hardware. "
                 "Error code: %d\n", __FUNCTION__, lag_name, status);
    }

    status = xpsLagDestroy(lag_id);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Could not destroy LAG %s on hardware. Error code: %d\n",
                 __FUNCTION__, lag_name, status);
        return;
    }

    VLOG_INFO("%s: Removed LAG %s(IF ID: %u) from hardware.",
              __FUNCTION__, lag_name, lag_id);
}

/* Attaches ports to LAG on HW. */
int
ops_xp_lag_attach_ports(xpsDevice_t dev_id, const char* lag_name,
                        xpsInterfaceId_t lag_id, xpsLagPortIntfList_t *if_list)
{
    XP_STATUS status;
    xpsLagPortIntfList_t hw_if_list;
    int i, j;

    ovs_assert(lag_name);
    ovs_assert(if_list);

    status = xpsLagGetPortIntfList(lag_id, &hw_if_list);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Could not get list of LAG %s interfaces. "
                 "Error code: %d\n", __FUNCTION__, lag_name, status);
        return EFAULT;
    }

    /* Detach removed member ports. */
    for (i = 0; i < hw_if_list.size; i++) {
        for (j = 0; j < if_list->size; j++) {
            if (if_list->portIntf[j] == hw_if_list.portIntf[i]) {
                break;
            }
        }

        if (j == if_list->size) {
            lag_detach_port_on_hw(dev_id, lag_id, hw_if_list.portIntf[i]);
        }
    }

    /* Attach current list of member ports. */
    for (i = 0; i < if_list->size; i++) {
        if (!lag_is_port_attached(lag_id, if_list->portIntf[i])) {
            lag_attach_port_on_hw(dev_id, lag_id, if_list->portIntf[i]);
        }
    }

    return 0;
}

/* Sets LAG hash mode on HW. */
int
ops_xp_lag_set_balance_mode(xpsDevice_t dev_id, enum bond_mode mode)
{
    const uint8_t HASH_FIELDS_COUNT = 2;
    xpHashField fields[HASH_FIELDS_COUNT];
    XP_STATUS status;

    switch (mode) {
    case BM_L2_SRC_DST_HASH:
        fields[0] = XP_ETHERNET_MAC_DA;
        fields[1] = XP_ETHERNET_MAC_SA;
        break;
    case BM_L3_SRC_DST_HASH:
        fields[0] = XP_IPV4_SOURCE_IP_ADDRESS;
        fields[1] = XP_IPV4_DESTINATION_IP_ADDRESS;
        break;
    default:
        VLOG_ERR("%s: Wrong hash option %u specified. ",
                 __FUNCTION__, mode);
        return EPERM;
    }

    status = xpsLagSetHashFields(dev_id, fields, HASH_FIELDS_COUNT);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Could not set LAG hash fields. "
                 "Error code: %d\n", __FUNCTION__, status);
        return EFAULT;
    }

    return 0;
}

/* Adds a member to LAG on hardware. */
static int
lag_attach_port_on_hw(xpsDevice_t dev_id, xpsInterfaceId_t lag_id,
                      xpsInterfaceId_t if_id)
{
    XP_STATUS status;
    struct xpliant_dev *dev;
    xpsInterfaceId_t found_lag_id;
    struct netdev_xpliant *netdev;
    int xpnet_if_id;
    int ret;

    status = xpsLagGetFirst(&found_lag_id);

    while (status == XP_NO_ERR) {
        /* If port is already attached to trunk, take appropriate action. 
         * XP API doesn't check for duplicate ports in trunk. */
        if (lag_is_port_attached(found_lag_id, if_id)) {

            if (lag_id == found_lag_id) {
                /* Nothing to do more. */
                return 0;
            }
            /* Detach port from the found LAG so that it can be added to LAG
             * identified by 'lag_id'. */
            lag_detach_port_on_hw(dev_id, found_lag_id, if_id);

            break;
        }

        status = xpsLagGetNext(found_lag_id, &found_lag_id);
    }

    netdev = ops_xp_netdev_from_port_num(dev_id, if_id);
    if (!netdev) {
        VLOG_ERR("%s: Could not get netdev for port: %u", __FUNCTION__, if_id);
        return ENOENT;
    }

    ovs_mutex_lock(&netdev->mutex);
    dev = ops_xp_dev_ref(netdev->xpdev);
    xpnet_if_id = netdev->xpnet_if_id;
    ovs_mutex_unlock(&netdev->mutex);

    ret = ops_xp_host_port_control_if_id_set(dev, if_id, xpnet_if_id, true);
    if (ret) {
        VLOG_ERR("%s: Could not set port control interface ID for port: %u",
                 __FUNCTION__, if_id);
        return ret;
    }

    status = xpsLagAddPort(lag_id, if_id);
    if (status != XP_NO_ERR) {
        ops_xp_host_port_control_if_id_set(dev, if_id, xpnet_if_id, false);
        VLOG_ERR("%s: Could not add port: %u to LAG: %u. Error code: %d",
                 __FUNCTION__, lag_id, if_id, status);
        return EFAULT;
    }

    status = xpsLagDeploy(dev_id, lag_id, AUTO_DIST_ENABLE);
    if (status != XP_NO_ERR) {
        ops_xp_host_port_control_if_id_set(dev, if_id, xpnet_if_id, false);
        VLOG_ERR("%s: Could not deploy LAG %u changes to hardware. "
                 "Error code: %d\n", __FUNCTION__, lag_id, status);
        return EFAULT;
    }

    ops_xp_dev_free(dev);

    VLOG_INFO("%s: Added port: %u to LAG: %u on hardware.",
              __FUNCTION__, if_id, lag_id);

    return 0;
}

/* Removes a member from a LAG on hardware. */
static int
lag_detach_port_on_hw(xpsDevice_t dev_id, xpsInterfaceId_t lag_id,
                      xpsInterfaceId_t if_id)
{
    XP_STATUS status;
    struct netdev_xpliant *netdev;
    int xpnet_if_id;

    if (!lag_is_port_attached(lag_id, if_id)) {
        return 0;
    }

    status = xpsLagRemovePort(lag_id, if_id);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Could not remove port: %u from LAG: %u."
                 "Error code: %d", __FUNCTION__, if_id, lag_id, status);
        return EFAULT;
    }

    status = xpsLagDeploy(dev_id, lag_id, AUTO_DIST_ENABLE);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Could not deploy LAG %u changes to hardware. "
                 "Error code: %d\n", __FUNCTION__, lag_id, status);
        return EFAULT;
    }

    /* Unbind port from L3 config. Just in case L3 was configured on a LAG */
    status = xpsL3UnBindPortIntf(if_id);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Could not unbind port: %u from L3 config."
                 "Error code: %d", __FUNCTION__, if_id, status);
        return EFAULT;
    }

    netdev = ops_xp_netdev_from_port_num(dev_id, if_id);
    if (netdev) {
        struct xpliant_dev *dev;

        ovs_mutex_lock(&netdev->mutex);
        dev = ops_xp_dev_ref(netdev->xpdev);
        xpnet_if_id = netdev->xpnet_if_id;
        ovs_mutex_unlock(&netdev->mutex);

        if (ops_xp_host_port_control_if_id_set(dev, if_id, xpnet_if_id, false)) {
            VLOG_ERR("%s: Could not unset port control interface ID "
                     "for port: %u", __FUNCTION__, if_id);
        }

        ops_xp_dev_free(dev);
    } else {
         VLOG_ERR("%s: Could not get netdev for port: %u", __FUNCTION__, if_id);
    }

    VLOG_INFO("%s: Removed port: %u from LAG: %u on hardware.", 
              __FUNCTION__, if_id, lag_id);

    return 0;
}

/* Checks if port is a LAG member. */
static bool
lag_is_port_attached(xpsInterfaceId_t lag_id, xpsInterfaceId_t if_id)
{
    XP_STATUS status;
    uint32_t isMember;

    status = xpsLagIsPortIntfMember(if_id, lag_id, &isMember);
    if ((status == XP_NO_ERR) && isMember) {
        return true;
    }

    return false;
}
