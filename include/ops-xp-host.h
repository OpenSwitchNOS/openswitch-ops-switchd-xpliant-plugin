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

typedef enum {
    XP_HOST_IF_KNET,
    XP_HOST_IF_TAP,
    XP_HOST_IF_DEFAULT = XP_HOST_IF_TAP
} xp_host_if_type_t;

struct xp_host_if_api {
    int (*init)(xpsDevice_t unit);
    void (*deinit)(xpsDevice_t unit);
    int (*if_create)(xpsDevice_t unit, char *name,
                     xpsInterfaceId_t xps_if_id,
                     struct ether_addr *mac, int *knet_if_id);
    int (*if_delete)(xpsDevice_t unit, int knet_if_id);
    int (*if_filter_create)(char *name, xpsDevice_t unit,
                            xpsInterfaceId_t xps_if_id,
                            int knet_if_id, int *knet_filter_id);
    int (*if_filter_delete)(xpsDevice_t unit,
                            int knet_filter_id);
};

struct xp_host_if_info {
    const struct xp_host_if_api *exec;
    void *data;
};

int ops_xp_host_init(xpsDevice_t unit, xp_host_if_type_t type);
void ops_xp_host_deinit(xpsDevice_t unit);
int ops_xp_host_if_create(xpsDevice_t unit, char *name,
                          xpsInterfaceId_t xps_if_id,
                          struct ether_addr *mac, int *knet_if_id);
int ops_xp_host_if_delete(xpsDevice_t unit, int knet_if_id);
void ops_xp_host_port_filter_create(char *name, xpsDevice_t unit,
                                    xpsInterfaceId_t xps_if_id,
                                    int knet_if_id, int *knet_filter_id);
void ops_xp_host_filter_delete(char *name, xpsDevice_t unit,
                               int knet_filter_id);

#endif /* ops-xp-host.h */
