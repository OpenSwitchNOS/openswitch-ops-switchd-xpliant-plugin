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
 * File: ops-xp-routing.h
 *
 * Purpose: This file provides public definitions for OpenSwitch routing
 *          related application code for the Cavium/XPliant SDK.
 */

#ifndef OPS_XP_ROUTING_H
#define OPS_XP_ROUTING_H 1

#include <net/if.h>
#include <ofproto/ofproto.h>
#include "openXpsL3.h"


typedef struct {
    struct ovs_refcount ref_cnt;
    struct ovs_mutex mutex;

    uint32_t next_host_id;              /* Next free Host entry ID */
    struct ovs_list dummy_host_list;    /* Contains dummy/empty host entries */

    struct hmap route_map;
    struct hmap nh_group_map;   /* All NextHop ECMP groups */
    struct hmap host_map;       /* Hosts by HW hash */
    struct hmap host_id_map;    /* Hosts by ID value */

    uint32_t ecmp_hash;

    void *dbg;                  /* Debugging hooks */
} xp_l3_mgr_t;

typedef struct {
    struct hmap_node hmap_node; /* Node in a xp_l3_mgr's nh_group_map. */
    struct hmap nh_map;         /* NextHops in ECMP group */
    struct ovs_refcount ref_cnt;/* How many routes reference to this NH group */
    uint32_t nh_id;
    uint32_t size;
    uint32_t hash;
} xp_nh_group_entry_t;

/* A route MAP entry.
 * Guarded by owning xp_l3_mgr's mutex */
typedef struct {
    struct hmap_node hmap_node; /* Node in a xp_l3_mgr's route_map. */
    char *prefix;
    struct ovs_refcount ref_cnt;/* Number of references to this route */
    xpsL3RouteEntry_t xp_route;
    xp_nh_group_entry_t *nh_group;
} xp_route_entry_t;

/* A host MAP entry.
 * Guarded by owning xp_l3_mgr's mutex */
typedef struct {
    struct hmap_node hmap_node;     /* Node in a xp_l3_mgr's host_map. */
    struct hmap_node hmap_id_node;  /* Node in a xp_l3_mgr's host_id_map. */
    struct ovs_list list_node;      /* Node in a xp_l3_mgr's dummy_host_list. */
    bool is_ipv6_addr;
    struct in_addr ipv4_dest_addr;
    uint8_t ipv6_dest_addr[sizeof(struct in6_addr)];
    uint8_t mac_addr[ETH_ADDR_LEN];
    xpsL3HostEntry_t xp_host;
    uint32_t id;    /* Host HW entry ID which must be constant */
} xp_host_entry_t;

/* A nh MAP entry.
 * Guarded by owning xp_l3_mgr's mutex */
typedef struct {
    struct hmap_node hmap_node; /* Node in nh_map of NH group. */
    xpsL3NextHopEntry_t xp_nh;  /* NH entry in HW */
    uint32_t xp_nh_id;          /* NH ID in the HW */
    char *id;                   /* NH ID in OPS */
    bool nh_port;
} xp_nh_entry_t;

typedef struct xp_l3_intf {
    xpsInterfaceId_t l3_intf_id;    /* L3 interface ID */
    xpsInterfaceId_t intf_id;       /* Port/LAG interface ID for non VLAN L3 */
    uint32_t l3_vrf;
    xpsVlan_t vlan_id;
} xp_l3_intf_t;

typedef struct net_address {
    struct hmap_node addr_node;
    uint32_t id;                /* ID of host entry created on HW */
    char *address;              /* IPv4/IPv6 address */
} xp_net_addr_t;

struct ofproto_xpliant;


xp_l3_mgr_t *ops_xp_l3_mgr_create(xpsDevice_t devId);
xp_l3_mgr_t *ops_xp_l3_mgr_ref(xp_l3_mgr_t *mgr);
void ops_xp_l3_mgr_unref(struct ofproto_xpliant *ofproto);
void ops_xp_l3_mgr_destroy(struct ofproto_xpliant *ofproto);

int ops_xp_routing_add_host_entry(struct ofproto_xpliant *ofproto,
                                  xpsInterfaceId_t port_intf_id,
                                  bool is_ipv6_addr,char *ip_addr,
                                  char *next_hop_mac_addr,
                                  xpsInterfaceId_t l3_intf_id,
                                  xpsVlan_t vid, bool local, int *l3_egress_id);

int ops_xp_routing_delete_host_entry(struct ofproto_xpliant *ofproto,
                                     int *l3_egress_id);

int ops_xp_routing_route_entry_action(struct ofproto_xpliant *ofproto,
                                      enum ofproto_route_action action,
                                      struct ofproto_route *routep);

int ops_xp_routing_ecmp_hash_set(struct ofproto_xpliant *ofproto,
                                 unsigned int hash, bool enable);

xp_l3_intf_t *ops_xp_routing_enable_l3_interface(
                                    struct ofproto_xpliant *ofproto,
                                    xpsInterfaceId_t if_id, char *if_name,
                                    macAddr_t mac);

xp_l3_intf_t *ops_xp_routing_enable_l3_subinterface(
                                    struct ofproto_xpliant *ofproto,
                                    xpsInterfaceId_t if_id, char *if_name,
                                    xpsVlan_t vid, macAddr_t mac);

xp_l3_intf_t *ops_xp_routing_enable_l3_vlan_interface(
                                    struct ofproto_xpliant *ofproto,
                                    xpsVlan_t vid, char *if_name,
                                    macAddr_t mac);

void ops_xp_routing_disable_l3_interface(struct ofproto_xpliant *ofproto,
                                         xp_l3_intf_t *l3_intf);

void ops_xp_routing_update_l3_interface(struct ofproto_xpliant *ofproto,
                                        xp_l3_intf_t *l3_intf);

void ops_xp_routing_unixctl_init(void);

#endif /* ops-xp-routing.h */
