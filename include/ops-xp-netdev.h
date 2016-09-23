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
 * File: ops-xp-netdev.h
 *
 * Purpose: This file provides public definitions for OpenSwitch network device
 *          related application code for the Cavium/XPliant SDK.
 */

#ifndef OPS_XP_NETDEV_H
#define OPS_XP_NETDEV_H 1

#include <netdev-provider.h>
#include <linux/ethtool.h>
#include <netinet/ether.h>
#include <ovs/ovs-thread.h>
#include <ovs/packets.h>

#include "ops-xp-dev.h"
#include "ops-xp-util.h"
#include "openXpsInterface.h"

struct xpdev_queue {
    struct hmap_node hmap_node; /* In struct netdev's "queues" hmap. */
    unsigned int queue_id;      /* OpenFlow queue ID. */
    uint32_t priority;
    uint32_t rate_kbps;
    uint32_t max_burst;
    long long int created;      /* Time queue was created, in msecs. */
};

struct netdev_rxq_xpliant {
    struct netdev_rxq up;

    /* NOTE: The implementation-specific members go here. */
    int rxq_fd[2];              /* RXQ UDS socket pair */
};

struct netdev_xpliant {
    struct netdev up;

    /* In xp_netdev_list. */
    struct ovs_list list_node OVS_GUARDED_BY(xp_netdev_list_mutex);

    /* Protects all members below. */
    struct ovs_mutex mutex OVS_ACQ_AFTER(xp_netdev_list_mutex);

    struct xpliant_dev *xpdev;  /* Xpliant device which netdev belongs to. */
    xpsPort_t port_num;
    xpVif_t vif;
    xpsInterfaceId_t ifId;

    /* Port Configuration. */
    struct port_cfg pcfg;

    bool link_status; /* true - UP, false - DOWN */
    struct netdev_stats stats;
    enum netdev_flags flags OVS_GUARDED;
    enum netdev_features features;
    /* Contains "struct queue_xpliant"s. */
    struct hmap queues;
    const char *qos_type;
    unsigned int n_queues;

    /* By default, one rx queue per netdev. See netdev_open() */
    struct netdev_rxq_xpliant *rxq;

    uint32_t kbits_rate;        /* Policing data. */
    uint32_t kbits_burst;

    /* OPS */
    bool intf_initialized;
    uint8_t hwaddr[ETH_ADDR_LEN] OVS_GUARDED;
    long long int link_resets OVS_GUARDED;
    int xpnet_if_id;
    int xpnet_port_filter_id;

    /* Port info structure. */
    struct xp_port_info *port_info;

    /* ----- Subport/lane split config (e.g. QSFP+) ----- */

     /* Boolean indicating if this is a split parent or subport:
     *  - Parent port refers to the base port that is not split.
     *  - Subports refers to all individual ports after the
     *    parent port is split.
     * Note that these two booleans can never both be true at the
     * same time, and the parent port and the first subport are
     * mutually exclusive since they map to the same h/w port.
     */
    bool is_split_parent;
    bool is_split_subport;

    /* Pointer to parent port port_info data.
     * Valid for split children ports only. */
    struct xp_port_info *parent_port_info;

    char *subintf_parent_name;
    xpsVlan_t subintf_vlan_id;

    /* For devices of class netdev_xpliant_internal_class only. */
    int tap_fd;
};

struct netdev_xpliant *ops_xp_netdev_from_name(const char *name);
struct netdev_xpliant *ops_xp_netdev_from_port_num(uint32_t dev_id,
                                                   uint32_t port_id);
struct netdev_xpliant *netdev_xpliant_cast(const struct netdev *netdev);
bool is_xpliant_class(const struct netdev_class *class);
void ops_xp_netdev_register(void);
void ops_xp_netdev_link_state_callback(struct netdev_xpliant *netdev,
                                       int link_status);
void ops_xp_netdev_get_subintf_vlan(struct netdev *netdev, xpsVlan_t *vlan);

#endif /* ops-xp-netdev.h */
