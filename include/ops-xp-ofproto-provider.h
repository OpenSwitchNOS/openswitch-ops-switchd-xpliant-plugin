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
 * File: ops-xp-ofproto-provider.h
 *
 * Purpose: This file provides public definitions for OpenSwitch ofproto provider
 *          related application code for the Cavium/XPliant SDK.
 */

#ifndef OPS_XP_OFPROTO_PROVIDER_H
#define OPS_XP_OFPROTO_PROVIDER_H 1

#include <stdint.h>

#include "ofp-util.h"
#include <ofproto/ofproto-provider.h>
#include "ops-xp-dev.h"
#include "ops-xp-host.h"
#include "ops-xp-routing.h"
#include "sset.h"
#include "openXpsInterface.h"
#include "openXpsPort.h"
#include "openXpsL3.h"

struct ofport_xpliant;
struct group_xpliant;
struct xp_vlan_mgr;
struct xp_bond_mgr;
struct xp_mac_learning;
struct xp_l3_intf;


struct ofproto_xpliant {

    struct hmap_node all_ofproto_xpliant_node; /* inside 'all_ofproto_xpliant'  */
    struct ofproto up;
    struct xpliant_dev *xpdev;

    /* Bridging. */
    struct hmap bundles;        /* Contains "struct bundle_xpliant"s. */
    struct xp_vlan_mgr* vlan_mgr;
    struct xp_bond_mgr* bond_mgr;
    struct xp_mac_learning *ml;
    bool has_bonded_bundles;
    bool lacp_enabled;
    struct eth_addr sys_mac;

    /* Ports */
    struct sset ports;             /* Set of standard port names added to the datapath. */
    struct sset ghost_ports;       /* Ports with no datapath port. */
    uint64_t change_seq;           /* Connectivity status changes. */

    /* Datapath Ports.
     *
     * Any lookup into 'dp_ports' requires taking 'port_rwlock'. */
    struct ovs_rwlock dp_port_rwlock;
    struct hmap dp_ports OVS_GUARDED;   /* 'xp_odp_port' nodes map */
    struct seq *dp_port_seq;            /* Incremented whenever a port changes. */

    /* OPS specific */
    xp_l3_mgr_t *l3_mgr;

    bool vrf;                   /* Specifies whether specific ofproto instance
                                 * is backing up VRF and not bridge */
    size_t vrf_id;              /* If vrf is true, then specifies hw vrf_id
                                 * for the specific ofproto instance */
};

struct ofport_xpliant {
    struct hmap_node odp_port_node; /* In xpdev's "odp_to_ofport_map". */
    struct ofport up;

    odp_port_t odp_port;        /* Datapath port number */
    struct bundle_xpliant *bundle;    /* Bundle that contains this port, if any. */
    struct ovs_list bundle_node;    /* In struct ofbundle's "ports" list. */

    /* Spanning tree. */
    struct stp_port *stp_port;  /* Spanning Tree Protocol, if any. */
    enum stp_state stp_state;   /* Always STP_DISABLED if STP not in use. */
    long long int stp_state_entered;

    bool may_enable;            /* May be enabled in bonds. */
    bool is_tunnel;             /* This port is a tunnel. */
};

struct bundle_xpliant {
    struct hmap_node hmap_node;     /* In struct ofproto's "bundles" hmap. */
    struct ofproto_xpliant *ofproto;/* Owning ofproto. */
    void *aux;      /* Key supplied by ofproto's client. */
    char *name;     /* Identifier for log messages. */
    xpsInterfaceId_t intfId; /* Interface ID of the bundle (port, bond, etc) */
    bool is_lag;

    /* Configuration. */
    struct ovs_list ports;          /* Contains "struct ofport"s. */
    bool ports_updated;
    enum port_vlan_mode vlan_mode; /* VLAN mode */
    int vlan;                   /* -1=trunk port, else a 12-bit VLAN ID. */
    unsigned long *trunks;      /* Bitmap of trunked VLANs, if 'vlan' == -1.
                                 * NULL if all VLANs are trunked. */
    bool use_priority_tags;     /* Use 802.1p tag for frames in VLAN 0? */

    /* Status. */
    bool floodable;         /* True if no port has OFPUTIL_PC_NO_FLOOD set. */

    /* L3 Routing */
    xp_l3_intf_t *l3_intf;  /* L3 interface pointer. NULL if not L3 */

    /* L3 port ip's */
    xp_net_addr_t *ip4addr;
    xp_net_addr_t *ip6addr;
    struct hmap secondary_ip4addr; /* List of secondary IP address */
    struct hmap secondary_ip6addr; /* List of secondary IPv6 address */
};

enum { N_TABLES = 2 };
enum { TBL_INTERNAL = N_TABLES - 1 };   /* Used for internal hidden rules. */

extern const struct ofproto_class ofproto_xpliant_class;

static inline struct ofproto_xpliant *
ops_xp_ofproto_cast(const struct ofproto *ofproto)
{
    ovs_assert(ofproto && ofproto->ofproto_class == &ofproto_xpliant_class);
    return ofproto ? CONTAINER_OF(ofproto, struct ofproto_xpliant, up) : NULL;
}

struct bundle_xpliant *bundle_lookup(const struct ofproto_xpliant *ofproto,
                                     void *aux);

xpsInterfaceId_t ops_xp_get_ofport_intf_id(const struct ofport_xpliant *port);
xpsPort_t ops_xp_get_ofport_number(const struct ofport_xpliant *port);
struct ofproto_xpliant *ops_xp_ofproto_lookup(const char *name);

#endif /* ops-xp-ofproto-provider.h */
