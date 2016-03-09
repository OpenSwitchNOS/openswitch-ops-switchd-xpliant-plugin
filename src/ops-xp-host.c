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

#include "ops-xp-dev.h"
#include "ops-xp-host.h"

VLOG_DEFINE_THIS_MODULE(xp_host);

extern const struct xp_host_if_api xp_host_netdev_api;
extern const struct xp_host_if_api xp_host_tap_api;

/* Initializes HOST interface. */
int
ops_xp_host_init(xpsDevice_t unit, xp_host_if_type_t type)
{
    int rc = 0;
    struct xp_host_if_info *info;
    struct xpliant_dev *xp_dev = ops_xp_dev_by_id(unit);

    ovs_assert(xp_dev);

    info = xzalloc(sizeof *info);
    xp_dev->host_if_info = info;

    switch (type) {
    case XP_HOST_IF_KNET:
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
        rc = info->exec->init(unit);
    }

    ops_xp_dev_free(xp_dev);
    return rc;
}

/* Deinitializes HOST interface. */
void
ops_xp_host_deinit(xpsDevice_t unit)
{
    struct xpliant_dev *xp_dev = ops_xp_dev_by_id(unit);
    struct xp_host_if_info *info;

    ovs_assert(xp_dev);
    ovs_assert(xp_dev->host_if_info);

    info = xp_dev->host_if_info;
    if (info->exec->deinit) {
        info->exec->deinit(unit);
    }

    xp_dev->host_if_info = NULL;
    free(info);
    ops_xp_dev_free(xp_dev);
}

/* Creates HOST virtual interface. */
int
ops_xp_host_if_create(xpsDevice_t unit, char *name,
                      xpsInterfaceId_t xps_if_id, struct ether_addr *mac,
                      int *knet_if_id)
{
    struct xpliant_dev *xp_dev = ops_xp_dev_by_id(unit);
    struct xp_host_if_info *info;
    int rc = EOPNOTSUPP;

    ovs_assert(xp_dev);
    ovs_assert(xp_dev->host_if_info);

    info = xp_dev->host_if_info;
    if (info->exec->if_create) {
        rc = info->exec->if_create(unit, name, xps_if_id, mac, knet_if_id);
    }
    ops_xp_dev_free(xp_dev);

    return rc;
}

/* Deletes HOST virtual interface. */
int
ops_xp_host_if_delete(xpsDevice_t unit, int knet_if_id)
{
    struct xpliant_dev *xp_dev = ops_xp_dev_by_id(unit);
    struct xp_host_if_info *info;
    int rc = EOPNOTSUPP;

    ovs_assert(xp_dev);
    ovs_assert(xp_dev->host_if_info);

    info = xp_dev->host_if_info;
    if (info->exec->if_delete) {
        rc = info->exec->if_delete(unit, knet_if_id);
    }
    ops_xp_dev_free(xp_dev);

    return rc;
}

void
ops_xp_host_port_filter_create(char *name, xpsDevice_t unit,
                               xpsInterfaceId_t xps_if_id,
                               int knet_if_id, int *knet_filter_id)
{
    struct xpliant_dev *xp_dev = ops_xp_dev_by_id(unit);
    struct xp_host_if_info *info;

    ovs_assert(xp_dev);
    ovs_assert(xp_dev->host_if_info);

    info = xp_dev->host_if_info;
    if (info->exec->if_filter_create) {
        info->exec->if_filter_create(name, unit, xps_if_id, knet_if_id,
                                     knet_filter_id);
    }
    ops_xp_dev_free(xp_dev);
}

void
ops_xp_host_filter_delete(char *name, xpsDevice_t unit, int knet_filter_id)
{
    struct xpliant_dev *xp_dev = ops_xp_dev_by_id(unit);
    struct xp_host_if_info *info;

    ovs_assert(xp_dev);
    ovs_assert(xp_dev->host_if_info);

    info = xp_dev->host_if_info;
    if (info->exec->if_filter_delete) {
        info->exec->if_filter_delete(unit, knet_filter_id);
    }
    ops_xp_dev_free(xp_dev);
}
