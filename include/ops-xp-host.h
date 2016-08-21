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
 * File: ops-xp-host.h
 *
 * Purpose: This file provides public definitions for OpenSwitch CPU host
 *          interface related generic application code for the
 *          Cavium/XPliant SDK.
 */

#ifndef OPS_XP_HOST_H
#define OPS_XP_HOST_H 1

#include <sys/select.h>
#include <netinet/ether.h>
#include <ovs/dynamic-string.h>
#include <hmap.h>
#include "openXpsInterface.h"

struct xpliant_dev;

typedef enum {
    XP_HOST_IF_XPNET,
    XP_HOST_IF_TAP,
    XP_HOST_IF_DEFAULT = XP_HOST_IF_TAP
} xp_host_if_type_t;

struct xp_host_if_api {
    int (*init)(struct xpliant_dev *xp_dev);
    void (*deinit)(struct xpliant_dev *xp_dev);
    int (*if_create)(struct xpliant_dev *xp_dev, char *name,
                     xpsInterfaceId_t xps_if_id,
                     struct ether_addr *mac, int *xpnet_if_id);
    int (*if_delete)(struct xpliant_dev *xp_dev, int xpnet_if_id);
    int (*if_filter_create)(char *name, struct xpliant_dev *xp_dev,
                            xpsInterfaceId_t xps_if_id,
                            int xpnet_if_id, int *xpnet_filter_id);
    int (*if_filter_delete)(struct xpliant_dev *xp_dev,
                            int xpnet_filter_id);
    int (*if_control_id_set)(struct xpliant_dev *xp_dev,
                             xpsInterfaceId_t xps_if_id, 
                             int xpnet_if_id, bool set);
};

struct xp_host_if_info {
    const struct xp_host_if_api *exec;
    void *data;
};

int ops_xp_host_init(struct xpliant_dev *xp_dev, xp_host_if_type_t type);
void ops_xp_host_deinit(struct xpliant_dev *xp_dev);
int ops_xp_host_if_create(struct xpliant_dev *xp_dev, char *name,
                          xpsInterfaceId_t xps_if_id,
                          struct ether_addr *mac, int *xpnet_if_id);
int ops_xp_host_if_delete(struct xpliant_dev *xp_dev, int xpnet_if_id);
void ops_xp_host_port_filter_create(char *name, struct xpliant_dev *xp_dev,
                                    xpsInterfaceId_t xps_if_id,
                                    int xpnet_if_id, int *xpnet_filter_id);
void ops_xp_host_filter_delete(char *name, struct xpliant_dev *xp_dev,
                               int xpnet_filter_id);
int ops_xp_host_port_control_if_id_set(struct xpliant_dev *xp_dev,
                                       xpsInterfaceId_t xps_if_id,
                                       int xpnet_if_id, bool set);

#endif /* ops-xp-host.h */
