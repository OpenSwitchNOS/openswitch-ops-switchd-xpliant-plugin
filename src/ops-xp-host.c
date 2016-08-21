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
 * File: ops-xp-host.c
 *
 * Purpose: This file contains OpenSwitch CPU host interface related
 *          generic application code for the Cavium/XPliant SDK.
 */

#include <openvswitch/vlog.h>

#include "ops-xp-host.h"
#include "ops-xp-dev.h"

VLOG_DEFINE_THIS_MODULE(xp_host);

extern const struct xp_host_if_api xp_host_netdev_api;
extern const struct xp_host_if_api xp_host_tap_api;

/* Initializes HOST interface. */
int
ops_xp_host_init(struct xpliant_dev *xp_dev, xp_host_if_type_t type)
{
    int rc = 0;
    struct xp_host_if_info *info;

    ovs_assert(xp_dev);

    info = xzalloc(sizeof *info);
    xp_dev->host_if_info = info;

    switch (type) {
    case XP_HOST_IF_XPNET:
        info->exec = &xp_host_netdev_api;
        info->data = NULL;
        break;
    case XP_HOST_IF_TAP:
        info->exec = &xp_host_tap_api;
        info->data = NULL;
        break;
    default:
        return EINVAL;
    }

    if (info->exec->init) {
        rc = info->exec->init(xp_dev);
    }

    return rc;
}

/* Deinitializes HOST interface. */
void
ops_xp_host_deinit(struct xpliant_dev *xp_dev)
{
    struct xp_host_if_info *info;

    ovs_assert(xp_dev);
    ovs_assert(xp_dev->host_if_info);

    info = xp_dev->host_if_info;
    if (info->exec->deinit) {
        info->exec->deinit(xp_dev);
    }

    xp_dev->host_if_info = NULL;
    free(info);
}

/* Creates HOST virtual interface. */
int
ops_xp_host_if_create(struct xpliant_dev *xp_dev, char *name,
                      xpsInterfaceId_t xps_if_id, struct ether_addr *mac,
                      int *xpnet_if_id)
{
    struct xp_host_if_info *info;
    int rc = EOPNOTSUPP;

    ovs_assert(xp_dev);
    ovs_assert(xp_dev->host_if_info);

    info = xp_dev->host_if_info;
    if (info->exec->if_create) {
        rc = info->exec->if_create(xp_dev, name, xps_if_id, mac, xpnet_if_id);
    }

    return rc;
}

/* Deletes HOST virtual interface. */
int
ops_xp_host_if_delete(struct xpliant_dev *xp_dev, int xpnet_if_id)
{
    struct xp_host_if_info *info;
    int rc = EOPNOTSUPP;

    ovs_assert(xp_dev);
    ovs_assert(xp_dev->host_if_info);

    info = xp_dev->host_if_info;
    if (info->exec->if_delete) {
        rc = info->exec->if_delete(xp_dev, xpnet_if_id);
    }

    return rc;
}

void
ops_xp_host_port_filter_create(char *name, struct xpliant_dev *xp_dev,
                               xpsInterfaceId_t xps_if_id,
                               int xpnet_if_id, int *xpnet_filter_id)
{
    struct xp_host_if_info *info;

    ovs_assert(xp_dev);
    ovs_assert(xp_dev->host_if_info);

    info = xp_dev->host_if_info;
    if (info->exec->if_filter_create) {
        info->exec->if_filter_create(name, xp_dev, xps_if_id, xpnet_if_id,
                                     xpnet_filter_id);
    }
}

void
ops_xp_host_filter_delete(char *name, struct xpliant_dev *xp_dev,
                          int xpnet_filter_id)
{
    struct xp_host_if_info *info;

    ovs_assert(xp_dev);
    ovs_assert(xp_dev->host_if_info);

    info = xp_dev->host_if_info;
    if (info->exec->if_filter_delete) {
        info->exec->if_filter_delete(xp_dev, xpnet_filter_id);
    }
}

int
ops_xp_host_port_control_if_id_set(struct xpliant_dev *xp_dev,
                                   xpsInterfaceId_t xps_if_id,
                                   int xpnet_if_id, bool set)
{
    struct xp_host_if_info *info;
    int rc = EOPNOTSUPP;

    ovs_assert(xp_dev);
    ovs_assert(xp_dev->host_if_info);

    info = xp_dev->host_if_info;
    if (info->exec->if_control_id_set) {
        rc = info->exec->if_control_id_set(xp_dev, xps_if_id, xpnet_if_id, set);
    }

    return rc;
}
