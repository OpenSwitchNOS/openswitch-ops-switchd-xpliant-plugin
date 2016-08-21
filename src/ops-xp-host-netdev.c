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
 * File: ops-xp-host-netdev.c
 *
 * Purpose: This file contains OpenSwitch CPU netdev interface related
 *          application code for the Cavium/XPliant SDK.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openvswitch/vlog.h>
#include <netinet/ether.h>

#include "ops-xp-util.h"
#include "ops-xp-host.h"
#include "ops-xp-dev.h"
#include "ops-xp-dev-init.h"
#include "openXpsPacketDrv.h"
#include "openXpsPort.h"
#include "openXpsReasonCodeTable.h"

VLOG_DEFINE_THIS_MODULE(xp_host_netdev);

static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(5, 20);

typedef enum xp_host_if_trap_channel {
    XP_HOST_IF_TRAP_CHANNEL_FD,     /* Receive packets via file desriptor */
    XP_HOST_IF_TRAP_CHANNEL_CB,     /* Receive packets via callback       */
    XP_HOST_IF_TRAP_CHANNEL_NETDEV, /* Receive packets via OS net device  */
    XP_HOST_IF_TRAP_CHANNEL_CUSTOM_RANGE_BASE = 0x10000000
} xp_host_if_trap_channel_t;

static int netdev_init(struct xpliant_dev *xp_dev);
static int netdev_if_create(struct xpliant_dev *xp_dev, char *intf_name,
                            xpsInterfaceId_t xps_if_id,
                            struct ether_addr *mac, int *xpnet_if_id);
static int netdev_if_delete(struct xpliant_dev *xp_dev, int xpnet_if_id);
static int netdev_if_filter_delete(struct xpliant_dev *xp_dev,
                                   int xpnet_filter_id);
static int netdev_if_filter_create(char *filter_name, struct xpliant_dev *xp_dev,
                                   xpsInterfaceId_t xps_if_id,
                                   int xpnet_if_id, int *xpnet_filter_id);
static int netdev_if_control_id_set(struct xpliant_dev *xp_dev,
                                    xpsInterfaceId_t xps_if_id, 
                                    int xpnet_if_id, bool set);

const struct xp_host_if_api xp_host_netdev_api = {
    netdev_init,
    NULL,
    netdev_if_create,
    netdev_if_delete,
    netdev_if_filter_create,
    netdev_if_filter_delete,
    netdev_if_control_id_set,
};

static int
netdev_init(struct xpliant_dev *xp_dev)
{
    uint32_t i = 0;
    uint32_t list_of_rc[] = {
        XP_IVIF_RC_BPDU,
        XP_BRIDGE_RC_IVIF_ARPIGMPICMP_CMD,
        XP_ROUTE_RC_HOST_TABLE_HIT,
        XP_ROUTE_RC_NH_TABLE_HIT,
        XP_ROUTE_RC_ROUTE_NOT_POSSIBLE,
        XP_ROUTE_RC_TTL1_OR_IP_OPTION,
    };

    for (i = 0; i < ARRAY_SIZE(list_of_rc); i++) {
        if (XP_NO_ERR != xpsNetdevTrapSet(xp_dev->id, i, list_of_rc[i], 
                         XP_HOST_IF_TRAP_CHANNEL_NETDEV, 0, true)) {
            VLOG_ERR("%s, Unable to install a trap.", __FUNCTION__);
            return EFAULT;
        }
    }

    return 0;
}

static int
netdev_if_create(struct xpliant_dev *xp_dev, char *intf_name,
                 xpsInterfaceId_t xps_if_id,
                 struct ether_addr *mac, int *xpnet_if_id)
{
    int xpnetId = 0;
    XP_STATUS status;

    if (XP_NO_ERR != xpsNetdevXpnetIdAllocate(xp_dev->id, &xpnetId)) {
        VLOG_ERR("%s, Unable to allocate xpnetId for interface: %u, %s",
                  __FUNCTION__, xps_if_id, intf_name);
        return EPERM;
    }

    if (XP_NO_ERR != xpsNetdevIfCreate(xp_dev->id, xpnetId, intf_name)) {
        VLOG_ERR("%s, Unable to create interface: %u, %s",
                 __FUNCTION__ ,xps_if_id, intf_name);
        return EPERM;
    }

    status = xpsNetdevIfTxHeaderSet(xp_dev->id, xpnetId, xps_if_id, true);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s, Unable to set tx header for interface: %u",
                 __FUNCTION__, xps_if_id);
        xpsNetdevIfDelete(xp_dev->id, xpnetId);
        xpsNetdevXpnetIdFree(xp_dev->id, xpnetId);
        return EPERM;
    }

    if (ops_xp_net_if_setup(intf_name, mac)) {
        VLOG_ERR("%s, Unable to setup %s interface.",
                 __FUNCTION__, intf_name);
        xpsNetdevIfDelete(xp_dev->id, xpnetId);
        xpsNetdevXpnetIdFree(xp_dev->id, xpnetId);
        return EFAULT;
    }

    *xpnet_if_id = xpnetId;
    return 0;
}

static int
netdev_if_delete(struct xpliant_dev *xp_dev, int xpnet_if_id)
{
    if (XP_NO_ERR != xpsNetdevIfDelete(xp_dev->id, xpnet_if_id)) {
        VLOG_ERR("%s, Unable to delete xpnet_if_id: %u",
                 __FUNCTION__, xpnet_if_id);
        return EPERM;
    }

    return 0;
}

static int
netdev_if_filter_create(char *filter_name, struct xpliant_dev *xp_dev,
                        xpsInterfaceId_t xps_if_id,
                        int xpnet_if_id, int *xpnet_filter_id)
{
    if (XP_NO_ERR != xpsNetdevIfLinkSet(xp_dev->id, xpnet_if_id, xps_if_id,
                                        true)) {
        xpsNetdevIfTxHeaderSet(xp_dev->id, xpnet_if_id, xps_if_id, false);
        VLOG_ERR("%s, Unable to link %u(%u) interface.",
                 __FUNCTION__, xps_if_id, xpnet_if_id);
        return EFAULT;
    }

    *xpnet_filter_id = xpnet_if_id;
    return 0;
}

static int
netdev_if_filter_delete(struct xpliant_dev *xp_dev, int xpnet_filter_id)
{
    uint32_t xpnet_if_id = xpnet_filter_id;

    xpsNetdevIfLinkSet(xp_dev->id, xpnet_if_id, 0, false);

    return 0;
}

static int
netdev_if_control_id_set(struct xpliant_dev *xp_dev,
                         xpsInterfaceId_t xps_if_id, 
                         int xpnet_if_id, bool set)
{
    xpsInterfaceId_t send_if_id = xps_if_id;
    xpsInterfaceType_e if_type;
    XP_STATUS status = XP_NO_ERR;

    if (set) {
        status = xpsInterfaceGetType(xps_if_id, &if_type);
        if (status != XP_NO_ERR) {
            VLOG_ERR("%s, Failed to get interface type. Error: %d", 
                     __FUNCTION__, status);
            return EPERM;
        }

        if (if_type != XPS_PORT) {
            return 0;
        }

        status = xpsPortGetPortControlIntfId(xp_dev->id, xps_if_id,
                                             &send_if_id);
        if (status) {
            VLOG_ERR("%s, Unable to get port control interface ID for "
                     "interface: %u. Error: %d",
                     __FUNCTION__, xps_if_id, status);
            return EPERM;
        }
    }

    status = xpsNetdevIfTxHeaderSet(xp_dev->id, xpnet_if_id, send_if_id, true);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s, Unable to set tx header for interface: %u",
                 __FUNCTION__, xps_if_id);
        return EPERM;
    }

    return 0;
}
