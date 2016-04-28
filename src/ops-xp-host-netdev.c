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

static int netdev_init(xpsDevice_t unit);
static int netdev_if_create(xpsDevice_t unit, char *intf_name,
                            xpsInterfaceId_t xps_if_id,
                            struct ether_addr *mac, int *knet_if_id);
static int netdev_if_delete(xpsDevice_t unit, int knet_if_id);
static int netdev_if_filter_delete(xpsDevice_t unit, int knet_filter_id);
static int netdev_if_filter_create(char *filter_name, xpsDevice_t unit,
                                   xpsInterfaceId_t xps_if_id,
                                   int knet_if_id, int *knet_filter_id);

const struct xp_host_if_api xp_host_netdev_api = {
    netdev_init,
    NULL,
    netdev_if_create,
    netdev_if_delete,
    netdev_if_filter_create,
    netdev_if_filter_delete,
};

static int
netdev_init(xpsDevice_t unit)
{
    uint32_t i = 0;
    uint32_t list_of_rc[] = {
        XP_IVIF_RC_BPDU,
        XP_BRIDGE_RC_IVIF_ARPIGMPICMP_CMD,
        XP_ROUTE_RC_HOST_TABLE_HIT,
        XP_ROUTE_RC_ROUTE_NOT_POSSIBLE,
        XP_ROUTE_RC_TTL1_OR_IP_OPTION,
    };

    for (i = 0; i < ARRAY_SIZE(list_of_rc); i++) {
        if (XP_NO_ERR != xpsNetdevTrapSet(unit, i, list_of_rc[i],
                         XP_HOST_IF_TRAP_CHANNEL_NETDEV, 0, true)) {
            VLOG_ERR("%s, Unable to install a trap.", __FUNCTION__);
            return EFAULT;
        }
    }

    return 0;
}

static int
netdev_if_create(xpsDevice_t unit, char *intf_name,
                 xpsInterfaceId_t xps_if_id,
                 struct ether_addr *mac, int *knet_if_id)
{
    int knetId = 0;
    struct xpliant_dev *xp_dev = ops_xp_dev_by_id(unit);

    if (XP_NO_ERR != xpsNetdevKnetIdAllocate(unit, &knetId)) {
        VLOG_ERR("%s, Unable to allocate knetId for interface: %u, %s",
                  __FUNCTION__, xps_if_id, intf_name);
        ops_xp_dev_free(xp_dev);
        return EPERM;
    }

    if (XP_NO_ERR != xpsNetdevIfCreate(unit, knetId, intf_name)) {
        VLOG_ERR("%s, Unable to create interface: %u, %s",
                 __FUNCTION__ ,xps_if_id, intf_name);
        ops_xp_dev_free(xp_dev);
        return EPERM;
    }

    if (ops_xp_net_if_setup(intf_name, mac)) {
        VLOG_ERR("%s, Unable to setup %s interface.",
                 __FUNCTION__, intf_name);
        xpsNetdevIfDelete(unit, knetId);
        xpsNetdevKnetIdFree(unit, knetId);
        ops_xp_dev_free(xp_dev);
        return EFAULT;
    }

    ops_xp_dev_free(xp_dev);
    *knet_if_id = knetId;
    return 0;
}

static int
netdev_if_delete(xpsDevice_t unit, int knet_if_id)
{
    struct xpliant_dev *xp_dev = ops_xp_dev_by_id(unit);

    if (XP_NO_ERR != xpsNetdevIfDelete(unit, knet_if_id)) {
        VLOG_ERR("%s, Unable to delete knet_if_id: %u", __FUNCTION__, knet_if_id);
        ops_xp_dev_free(xp_dev);
        return EPERM;
    }

    ops_xp_dev_free(xp_dev);
    return 0;
}

static int
netdev_if_filter_create(char *filter_name, xpsDevice_t unit,
                        xpsInterfaceId_t xps_if_id,
                        int knet_if_id, int *knet_filter_id)
{
    if (XP_NO_ERR != xpsNetdevIfTxHeaderSet(unit, knet_if_id, xps_if_id, true)) {
        VLOG_ERR("%s, Unable to set tx header for interface: %u",
                 __FUNCTION__, xps_if_id);
        return EPERM;
    }

    if (XP_NO_ERR != xpsNetdevIfLinkSet(unit, knet_if_id, xps_if_id, true)) {
        xpsNetdevIfTxHeaderSet(unit, knet_if_id, xps_if_id, false);
        VLOG_ERR("%s, Unable to link %u(%u) interface.",
                 __FUNCTION__, xps_if_id, knet_if_id);
        return EFAULT;
    }

    *knet_filter_id = knet_if_id;
    return 0;
}

static int
netdev_if_filter_delete(xpsDevice_t unit, int knet_filter_id)
{
    uint32_t knet_if_id = knet_filter_id;

    xpsNetdevIfTxHeaderSet(unit, knet_if_id, 0, false);
    xpsNetdevIfLinkSet(unit, knet_if_id, 0, false);

    return 0;
}
