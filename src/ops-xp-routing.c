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
 * File: ops-xp-routing.c
 *
 * Purpose: This file contains OpenSwitch routing related application code
 *          for the Cavium/XPliant SDK.
 */

#include <string.h>
#include <errno.h>
#include <assert.h>
#include <util.h>
#include <sset.h>
#include <netinet/ether.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <openvswitch/vlog.h>
#include "ops-xp-ofproto-provider.h"
#include "ops-xp-util.h"
#include "ops-xp-routing.h"
#include "ops-xp-vlan.h"
#include "unixctl.h"
#include "openXpsReasonCodeTable.h"

VLOG_DEFINE_THIS_MODULE(xp_routing);

typedef struct {
    struct ofproto_xpliant *ofproto;
    char *prefix;
    uint32_t count;
    uint32_t ignore_err;
} xp_l3_test_params_t;

typedef struct {
    uint32_t exec_sec;          /* Last test execution time (sec) */
    uint32_t exec_usec;         /* Last test execution time (usec) */
    uint32_t active_routes;     /* Routes in HW */
    uint32_t updated_routes;    /* Routes added/deleted during the last test */
    uint32_t errors;            /* Routes add/delete failures */
    bool started;               /* Test has been started */
    bool add_routes;            /* Add/delete routes */
    bool kickout;               /* Force test to stop */
} xp_l3_dbg_t;

static void
route_delete(struct ofproto_xpliant *ofproto, xp_route_entry_t *route);

static void
nh_group_delete(struct ofproto_xpliant *ofproto, xp_nh_group_entry_t *nh_group);


/* Creates and returns a new L3 manager. */
xp_l3_mgr_t *
ops_xp_l3_mgr_create(xpsDevice_t devId)
{
    xp_l3_mgr_t *mgr = NULL;
    XP_STATUS status;

    mgr = xzalloc(sizeof *mgr);

    ovs_refcount_init(&mgr->ref_cnt);
    ovs_mutex_init_recursive(&mgr->mutex);

    list_init(&mgr->dummy_host_list);

    hmap_init(&mgr->route_map);
    hmap_init(&mgr->nh_group_map);
    hmap_init(&mgr->host_map);
    hmap_init(&mgr->host_id_map);

    mgr->ecmp_hash = (OFPROTO_ECMP_HASH_SRCPORT | OFPROTO_ECMP_HASH_DSTPORT |
                        OFPROTO_ECMP_HASH_SRCIP | OFPROTO_ECMP_HASH_DSTIP);

    mgr->dbg = xzalloc(sizeof(xp_l3_dbg_t));

    return mgr;
}

/* Destroys L3 manager 'mgr'.
 * Should be called directly only on program exit where immediate destruction is
 * required, otherwise xp_l3_mgr_unref() should be used. */
void
ops_xp_l3_mgr_destroy(struct ofproto_xpliant *ofproto)
{
    xp_l3_mgr_t *mgr;
    xp_l3_dbg_t *dbg;
    uint32_t i;

    ovs_assert(ofproto);

    if (ofproto->l3_mgr == NULL) {
        return;
    }

    mgr = ofproto->l3_mgr;
    dbg = mgr->dbg;

    /* Remove L3 debugging info */
    i = 3;
    dbg->kickout = true;
    while (dbg->started && (i > 0)) {
        sleep(1);
        --i;
    }
    free(dbg);

    /* Remove dummy/empty host entries */
    {
        xp_host_entry_t *e = NULL;

        LIST_FOR_EACH_POP (e, list_node, &mgr->dummy_host_list) {
            free(e);
        }
    }

    /* Clear and destroy routes map. */
    {
        xp_route_entry_t *e = NULL;
        xp_route_entry_t *next = NULL;

        HMAP_FOR_EACH_SAFE (e, next, hmap_node, &mgr->route_map) {
            route_delete(ofproto, e);
        }
        hmap_destroy(&mgr->route_map);
    }

    /* Clear and destroy NH group map. */
    {
        xp_nh_group_entry_t *e = NULL;
        xp_nh_group_entry_t *next = NULL;

        HMAP_FOR_EACH_SAFE (e, next, hmap_node, &mgr->nh_group_map) {
            nh_group_delete(ofproto, e);
        }
        hmap_destroy(&mgr->nh_group_map);
    }

    /* Clear and destroy hosts map. */
    {
        xp_host_entry_t *e = NULL;
        xp_host_entry_t *next = NULL;

        HMAP_FOR_EACH_SAFE (e, next, hmap_node, &mgr->host_map) {
            ops_xp_routing_delete_host_entry(ofproto, &e->id);
        }
        hmap_destroy(&mgr->host_map);
        hmap_destroy(&mgr->host_id_map);
    }

    ovs_mutex_destroy(&mgr->mutex);

    free(mgr);

    VLOG_INFO("L3 manager instance deallocated.");
}

xp_l3_mgr_t *
ops_xp_l3_mgr_ref(xp_l3_mgr_t *mgr)
{
    if (mgr) {
        ovs_refcount_ref(&mgr->ref_cnt);
    }
    return mgr;
}

/* Unreferences (and possibly destroys) L3 manager */
void
ops_xp_l3_mgr_unref(struct ofproto_xpliant *ofproto)
{
    int ret = 0;

    if (ofproto->l3_mgr && ovs_refcount_unref(&ofproto->l3_mgr->ref_cnt) == 1) {
        ops_xp_l3_mgr_destroy(ofproto);
    }
}

static xp_host_entry_t *
host_entry_alloc(xp_l3_mgr_t *mgr)
{
    xp_host_entry_t *host;
    struct ovs_list *node;

    ovs_assert(mgr);
    if (list_is_empty(&mgr->dummy_host_list)) {
        host = xzalloc(sizeof *host);
        host->id = mgr->next_host_id++;
    } else {
        node = list_pop_front(&mgr->dummy_host_list);
        host = CONTAINER_OF(node, xp_host_entry_t, list_node);
    }
    return host;
}

static void
host_entry_free(xp_l3_mgr_t *mgr, xp_host_entry_t *host)
{
    ovs_assert(mgr);

    if (host) {
        uint32_t id = host->id;
        memset(host, 0, sizeof(*host));
        host->id = id;
        list_push_front(&mgr->dummy_host_list, &host->list_node);
    }
}

/* Function to add l3 host entry via ofproto */
int
ops_xp_routing_add_host_entry(struct ofproto_xpliant *ofproto,
                              xpsInterfaceId_t port_intf_id,
                              bool is_ipv6_addr, char *ip_addr,
                              char *next_hop_mac_addr,
                              xpsInterfaceId_t l3_intf_id,
                              xpsVlan_t vid, bool local, int *l3_egress_id)
{
    XP_STATUS status;
    uint32_t hash, rehash;
    uint8_t prefix_len;
    xp_host_entry_t *e;
    xp_l3_mgr_t *l3_mgr;
    int rc;

    ovs_assert(ofproto);
    ovs_assert(ofproto->xpdev);
    ovs_assert(ofproto->l3_mgr);

    VLOG_DBG("%s: %s ip %s, mac %s, port %u, l3intf %u",
             __FUNCTION__, local ? "local" : "remote",
             ip_addr, next_hop_mac_addr,
             port_intf_id, l3_intf_id);

    l3_mgr = ofproto->l3_mgr;

    ovs_mutex_lock(&l3_mgr->mutex);

    e = host_entry_alloc(l3_mgr);

    if (local) {
        e->xp_host.nhEntry.reasonCode = XP_ROUTE_RC_HOST_TABLE_HIT;
    } else {
        struct ether_addr ether_mac;

        e->xp_host.nhEntry.nextHop.l3InterfaceId = l3_intf_id;
        e->xp_host.nhEntry.nextHop.egressIntfId = port_intf_id;
        e->xp_host.nhEntry.serviceInstId = vid;
        e->xp_host.nhEntry.pktCmd = XP_PKTCMD_FWD;

        if (ether_aton_r(next_hop_mac_addr, &ether_mac) != NULL) {
            ops_xp_mac_copy_and_reverse(e->xp_host.nhEntry.nextHop.macDa,
                                        (uint8_t *)&ether_mac);
            memcpy(e->mac_addr, &ether_mac, ETH_ADDR_LEN);
        }
    }
    e->xp_host.nhEntry.propTTL = false;
    e->xp_host.vrfId = ofproto->vrf_id;

    if (is_ipv6_addr) {
        struct in6_addr ipv6_addr;

        rc = ops_xp_string_to_prefix(AF_INET6, ip_addr, &ipv6_addr,
                                     &prefix_len);
        if (rc) {
            VLOG_ERR("Failed to create L3 host entry. Invalid ipv6 address %s",
                     ip_addr);
            host_entry_free(l3_mgr, e);
            ovs_mutex_unlock(&l3_mgr->mutex);
            return EPFNOSUPPORT;
        }
        e->xp_host.type = XP_PREFIX_TYPE_IPV6;
        memcpy(e->xp_host.ipv6Addr, &ipv6_addr, sizeof(struct in6_addr));
        memcpy(e->ipv6_dest_addr, &ipv6_addr, sizeof(struct in6_addr));
    } else {
        struct in_addr ipv4_addr;

        rc = ops_xp_string_to_prefix(AF_INET, ip_addr, &ipv4_addr, &prefix_len);
        if (rc) {
            VLOG_ERR("Failed to create L3 host entry. Invalid ipv4 address %s",
                     ip_addr);
            host_entry_free(l3_mgr, e);
            ovs_mutex_unlock(&l3_mgr->mutex);
            return EPFNOSUPPORT;
        }

        e->xp_host.type = XP_PREFIX_TYPE_IPV4;
        memcpy(e->xp_host.ipv4Addr, &ipv4_addr, 4);
        e->ipv4_dest_addr = ipv4_addr;
    }

    e->is_ipv6_addr = is_ipv6_addr;

    if (local) {
        status = xpsL3AddIpHostControlEntry(ofproto->xpdev->id, &e->xp_host,
                                            &hash, &rehash);
    } else {
        status = xpsL3AddIpHostEntry(ofproto->xpdev->id, &e->xp_host,
                                     &hash, &rehash);
    }

    if (status != XP_NO_ERR) {
        VLOG_ERR("%s, Could not add L3 host entry on hardware. Error: %d",
                 __FUNCTION__, status);
        host_entry_free(l3_mgr, e);
        ovs_mutex_unlock(&l3_mgr->mutex);
        return EAGAIN;
    }

    /* Assign unique immutable Host Entry ID */
    *l3_egress_id = (int)e->id;

    VLOG_DBG("%s: Entry Id %u: "IP_FMT"  "ETH_ADDR_FMT,
             __FUNCTION__, *l3_egress_id,
             IP_ARGS(e->ipv4_dest_addr.s_addr),
             ETH_ADDR_BYTES_ARGS(e->xp_host.nhEntry.nextHop.macDa));

    if (hash != rehash) {
        struct hmap_node *node;

        /* Update entry index in the host table. */
        node = hmap_first_with_hash(&l3_mgr->host_map, hash);
        if (node != NULL) {
            hmap_remove(&l3_mgr->host_map, node);
            hmap_insert(&l3_mgr->host_map, node, rehash);
        }
    }
    hmap_insert(&l3_mgr->host_map, &e->hmap_node, hash);
    hmap_insert(&l3_mgr->host_id_map, &e->hmap_id_node, e->id);

    ovs_mutex_unlock(&l3_mgr->mutex);

    return 0;
}

int
ops_xp_routing_delete_host_entry(struct ofproto_xpliant *ofproto,
                                 int *l3_egress_id)
{
    XP_STATUS status;
    xpIpPrefixType_t host_entry_type;
    struct hmap_node *node;
    xp_l3_mgr_t *l3_mgr;
    xp_host_entry_t *e;
    int index;

    ovs_assert(ofproto);
    ovs_assert(ofproto->xpdev);
    ovs_assert(ofproto->l3_mgr);

    l3_mgr = ofproto->l3_mgr;

    ovs_mutex_lock(&l3_mgr->mutex);

    node = hmap_first_with_hash(&l3_mgr->host_id_map, (size_t)*l3_egress_id);
    if (node == NULL) {
        VLOG_WARN("Invalid L3 host entry ID %d", *l3_egress_id);
        ovs_mutex_unlock(&l3_mgr->mutex);
        return 0;
    }
    hmap_remove(&l3_mgr->host_id_map, node);

    /* Retrieve Host Entry's HW hash value */
    e = CONTAINER_OF(node, xp_host_entry_t, hmap_id_node);
    index = (int)hmap_node_hash(&e->hmap_node);
    if (hmap_contains(&l3_mgr->host_map, &e->hmap_node)) {
        hmap_remove(&l3_mgr->host_map, &e->hmap_node);
    }

    host_entry_type = e->is_ipv6_addr ? XP_PREFIX_TYPE_IPV6 : XP_PREFIX_TYPE_IPV4;

    host_entry_free(l3_mgr, e);

    /* Remove Host Entry from the HW */
    status = xpsL3RemoveIpHostEntryByIndex(ofproto->xpdev->id, (uint32_t)index,
                                           host_entry_type);
    ovs_mutex_unlock(&l3_mgr->mutex);

    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to delete L3 host entry");
        return EHOSTDOWN;
    }

    return 0;
}

static xp_nh_group_entry_t *
nh_group_alloc(uint32_t size)
{
    xp_nh_group_entry_t *nh_group;
    XP_STATUS status;
    uint32_t nh_id;

    if (size > OFPROTO_MAX_NH_PER_ROUTE) {
        return NULL;
    }

    /* Allocate new NH group ID */
    status = xpsL3CreateRouteNextHop(size, &nh_id);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Could not allocate NH on hardware. Err %d", status);
        return NULL;
    }

    /* Allocate and init NH group entry */
    nh_group = xzalloc(sizeof(*nh_group));
    hmap_init(&nh_group->nh_map);
    ovs_refcount_init(&nh_group->ref_cnt);
    nh_group->size = size;
    nh_group->nh_id = nh_id;

    if (size > 0) {
        hmap_reserve(&nh_group->nh_map, size);
    }

    return nh_group;
}

static void
nh_group_free(xp_nh_group_entry_t *nh_group)
{
    XP_STATUS status;
    xp_nh_entry_t *e = NULL;
    xp_nh_entry_t *next = NULL;

    ovs_assert(nh_group);

    VLOG_DBG("%s: nexthop id %u, size %u",
             __FUNCTION__, nh_group->nh_id, nh_group->size);

    status = xpsL3DestroyRouteNextHop(nh_group->size, nh_group->nh_id);
    if (status != XP_NO_ERR) {
        VLOG_WARN("Could not remove next hop on hardware. Err %d", status);
    }

    HMAP_FOR_EACH_SAFE(e, next, hmap_node, &nh_group->nh_map) {
        VLOG_DBG("%s: nexthop %s", __FUNCTION__, e->id);
        free(e->id);
        free(e);
    }
    hmap_destroy(&nh_group->nh_map);
    free(nh_group);
}

static void
nh_group_delete(struct ofproto_xpliant *ofproto, xp_nh_group_entry_t *nh_group)
{
    XP_STATUS status;
    xp_nh_entry_t *e;

    ovs_assert(ofproto);
    ovs_assert(ofproto->l3_mgr);
    ovs_assert(nh_group);

    VLOG_DBG("%s: nexthop id %u, size %u",
             __FUNCTION__, nh_group->nh_id, nh_group->size);

    if (hmap_contains(&ofproto->l3_mgr->nh_group_map,
                      &nh_group->hmap_node)) {
        hmap_remove(&ofproto->l3_mgr->nh_group_map, &nh_group->hmap_node);
    }

    HMAP_FOR_EACH(e, hmap_node, &nh_group->nh_map) {
        status = xpsL3ClearRouteNextHop(ofproto->xpdev->id, e->xp_nh_id);
        if (status != XP_NO_ERR) {
            VLOG_WARN("Could not clear NH on hardware. Status: %d", status);
        }
    }

    nh_group_free(nh_group);
}

static void
nh_group_unref(struct ofproto_xpliant *ofproto, xp_nh_group_entry_t *nh_group)
{
    if (nh_group) {
        if (ovs_refcount_unref(&nh_group->ref_cnt) == 1) {
            nh_group_delete(ofproto, nh_group);
        }
    }
}

/* Creates NH group on the HW */
static int
nh_group_add(struct ofproto_xpliant *ofproto, xp_nh_group_entry_t *nh_group)
{
    xp_nh_entry_t *e;
    XP_STATUS status;

    ovs_assert(ofproto);
    ovs_assert(ofproto->l3_mgr);
    ovs_assert(nh_group);

    HMAP_FOR_EACH(e, hmap_node, &nh_group->nh_map) {
        status = xpsL3SetRouteNextHop(ofproto->xpdev->id, e->xp_nh_id, &e->xp_nh);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Could not set next hop on hardware. Status: %d", status);
            return EHOSTUNREACH;
        }
    }

    hmap_insert(&ofproto->l3_mgr->nh_group_map,
                &nh_group->hmap_node, nh_group->hash);
    return 0;
}

static xp_nh_entry_t *
nh_dup(const xp_nh_entry_t *nh)
{
    xp_nh_entry_t *new_nh;

    ovs_assert(nh);
    new_nh = xzalloc(sizeof(*new_nh));
    memcpy(new_nh, nh, sizeof(*new_nh));
    new_nh->id = xstrdup(nh->id);
    return new_nh;
}

static void
nh_insert(xp_nh_group_entry_t *nh_group, xp_nh_entry_t *nh)
{
    ovs_assert(nh_group);
    ovs_assert(nh);

    /* Update NH group's hash and insert NH into the group */
    nh->xp_nh_id = nh_group->nh_id + hmap_count(&nh_group->nh_map);
    nh_group->hash = hash_string(nh->id, nh_group->hash);
    hmap_insert(&nh_group->nh_map, &nh->hmap_node, hash_string(nh->id, 0));

    /* Update NH group size in case it has been resized */
    if (hmap_count(&nh_group->nh_map) > nh_group->size) {
        nh_group->size = hmap_count(&nh_group->nh_map);
    }

    VLOG_DBG("%s: nexthop group id %u, size %u, hash %08X, nh %s, nh id %u",
             __FUNCTION__, nh_group->nh_id, nh_group->size,
             nh_group->hash, nh->id, nh->xp_nh_id);
}

static int
nh_update(xp_l3_mgr_t *mgr, xp_nh_entry_t *xp_nh,
          struct ofproto_route_nexthop *nh)
{
    struct hmap_node *node;

    ovs_assert(mgr);
    ovs_assert(xp_nh);
    ovs_assert(nh);

    if (nh->state == OFPROTO_NH_RESOLVED) {
        xp_host_entry_t *host_entry;

        /* Retrieve L3 Host Entry */
        node = hmap_first_with_hash(&mgr->host_id_map, (size_t)nh->l3_egress_id);
        if (node == NULL) {
            VLOG_WARN("Unknown L3 host entry ID %d", nh->l3_egress_id);
            return EHOSTUNREACH;
        }
        host_entry = CONTAINER_OF(node, xp_host_entry_t, hmap_id_node);

        memcpy(xp_nh->xp_nh.nextHop.macDa,
               host_entry->xp_host.nhEntry.nextHop.macDa,
               ETH_ADDR_LEN);

        xp_nh->xp_nh.nextHop.egressIntfId = host_entry->xp_host.nhEntry.nextHop.egressIntfId;
        xp_nh->xp_nh.nextHop.l3InterfaceId = host_entry->xp_host.nhEntry.nextHop.l3InterfaceId;
        xp_nh->xp_nh.serviceInstId = host_entry->xp_host.nhEntry.serviceInstId;
        xp_nh->xp_nh.pktCmd = XP_PKTCMD_FWD;

    } else if (xp_nh->xp_nh.pktCmd != XP_PKTCMD_FWD) {
        /* Entry is not resolved. Trap packet to CPU for resolution. */
        xp_nh->xp_nh.pktCmd = XP_PKTCMD_TRAP;
    }

    xp_nh->xp_nh.propTTL = false;

    return 0;
}

static xp_nh_entry_t *
nh_create(xp_l3_mgr_t *mgr, struct ofproto_route_nexthop *nh)
{
    xp_nh_entry_t *nh_entry;
    struct hmap_node *node;
    int rc;

    ovs_assert(mgr);
    ovs_assert(nh);

    nh_entry = xzalloc(sizeof(*nh_entry));

    rc = nh_update(mgr, nh_entry, nh);
    if (rc) {
        free(nh_entry);
        return NULL;
    }
    nh_entry->id = xstrdup(nh->id);
    nh_entry->nh_port = (nh->type == OFPROTO_NH_PORT);

    return nh_entry;
}

static xp_nh_entry_t *
nh_lookup(xp_nh_group_entry_t *nh_group, const char *nh_id)
{
    xp_nh_entry_t *e;

    ovs_assert(nh_id);

    if (nh_group) {
        HMAP_FOR_EACH(e, hmap_node, &nh_group->nh_map) {
            if (strcmp(e->id, nh_id) == 0) {
                return e;
            }
        }
    }

    return NULL;
}

static xp_nh_group_entry_t *
nh_group_lookup_by_route(xp_l3_mgr_t *mgr, struct ofproto_route *route)
{
    xp_nh_group_entry_t *e;
    uint32_t hash;
    bool found;
    uint32_t i;

    ovs_assert(mgr);
    ovs_assert(route);

    for (i = 0, hash = 0; i < route->n_nexthops; i++) {
        hash = hash_string(route->nexthops[i].id, hash);
        VLOG_DBG("%s: nexthop %s, type %u, state %u",
                 __FUNCTION__, route->nexthops[i].id,
                 route->nexthops[i].type, route->nexthops[i].state);
    }

    HMAP_FOR_EACH_WITH_HASH(e, hmap_node, hash, &mgr->nh_group_map) {
        if (e->size == route->n_nexthops) {
            VLOG_DBG("%s: nexthop group id %u, size %u",
                     __FUNCTION__, e->nh_id, e->size);

            found = true;
            for (i = 0; i < route->n_nexthops; i++) {
                if (nh_lookup(e, route->nexthops[i].id) == NULL) {
                    found = false;
                    break;
                }
            }

            if (found) {
                ovs_refcount_ref(&e->ref_cnt);
                return e;
            }
        }
    }

    return NULL;
}

static xp_nh_group_entry_t *
nh_group_lookup_by_group(xp_l3_mgr_t *mgr, xp_nh_group_entry_t *nh_group)
{
    xp_nh_group_entry_t *e;
    xp_nh_entry_t *nh_e;
    bool found;

    ovs_assert(mgr);
    ovs_assert(nh_group);

    HMAP_FOR_EACH_WITH_HASH(e, hmap_node, nh_group->hash, &mgr->nh_group_map) {
        if (e->size == nh_group->size) {
            found = true;
            HMAP_FOR_EACH(nh_e, hmap_node, &nh_group->nh_map) {
                if (nh_lookup(e, nh_e->id) == NULL) {
                    found = false;
                    break;
                }
            }

            if (found) {
                ovs_refcount_ref(&e->ref_cnt);
                return e;
            }
        }
    }

    return NULL;
}

static int
route_update(struct ofproto_xpliant *ofproto,
             struct ofproto_route *route,
             xp_route_entry_t *xp_route)
{
    xp_nh_entry_t *nh;
    bool nh_add;
    uint32_t i;
    XP_STATUS status;
    int rc;
    xp_nh_group_entry_t *nh_group;
    uint32_t n_nexthops;

    VLOG_DBG("%s: %s", __FUNCTION__, route->prefix);

    for (i = 0; i < route->n_nexthops; i++) {
        VLOG_DBG("%s: nexthop %s, type %u, state %u",
                 __FUNCTION__, route->nexthops[i].id,
                 route->nexthops[i].type, route->nexthops[i].state);
    }

    /* Check whether new NH entries have to be added */
    nh_add = false;
    for (i = 0; i < route->n_nexthops; i++) {
        nh = nh_lookup(xp_route->nh_group, route->nexthops[i].id);
        if (nh == NULL) {
            nh_add = true;
            break;
        }
    }

    if (nh_add) {
        /* New NH entries have to be added to the NH group of the route.
         * Since NH group can already be referenced by a few other routes
         * then new NH group has to be created for this route. */

        /* Calculate NH group size */
        n_nexthops = route->n_nexthops;
        if (xp_route->nh_group) {
            n_nexthops += xp_route->nh_group->size;
        }

        /* Allocate new NH group */
        nh_group = nh_group_alloc(n_nexthops);
        if (nh_group == NULL) {
            VLOG_ERR("Failed to allocate NH group");
            return ENOMEM;
        }

        /* Copy existing NH into the new NH group */
        if (xp_route->nh_group) {
            HMAP_FOR_EACH(nh, hmap_node, &xp_route->nh_group->nh_map) {
                nh_insert(nh_group, nh_dup(nh));
            }
        }
    } else {
        /* Existing NH group should be updated.
         * NOTE: the update will be visible for all routes that
         * refer to this NH group. */
        nh_group = xp_route->nh_group;
    }

    /* Update NH group */
    for (i = 0; i < route->n_nexthops; i++) {
        nh = nh_lookup(nh_group, route->nexthops[i].id);
        if (nh == NULL) {
            VLOG_DBG("%s: create nexthop %s, type %u, state %u",
                     __FUNCTION__, route->nexthops[i].id,
                     route->nexthops[i].type, route->nexthops[i].state);

            /* Add new NH entry */
            nh = nh_create(ofproto->l3_mgr, &route->nexthops[i]);
            if (nh == NULL) {
                VLOG_ERR("Failed to create NH entry %s",
                         route->nexthops[i].id);
                /* The NH group has not been created on HW yet.
                 * So, just free resources. */
                nh_group_free(nh_group);
                return EHOSTUNREACH;
            }
            nh_insert(nh_group, nh);
        } else {
            VLOG_DBG("%s: update nexthop %s, type %u, state %u",
                     __FUNCTION__, route->nexthops[i].id,
                     route->nexthops[i].type, route->nexthops[i].state);

            /* Update existing NH entry */
            rc = nh_update(ofproto->l3_mgr, nh, &route->nexthops[i]);
            if (rc) {
                VLOG_ERR("Failed to update NH entry %s", route->nexthops[i].id);
                if (nh_add) {
                    /* In case NH group has not been created on HW yet,
                     * just free it. */
                    nh_group_free(nh_group);
                }
                return rc;
            }
        }
    }

    if (nh_add) {
        xp_nh_group_entry_t *old_nh_group;

        old_nh_group = nh_group_lookup_by_group(ofproto->l3_mgr, nh_group);
        if (old_nh_group) {
            /* Reuse existing NH group */
            nh_group_free(nh_group);
            nh_group = old_nh_group;
            VLOG_DBG("%s: reuse existing NH group %u",
                     __FUNCTION__, nh_group->nh_id);
        } else {
            /* Create NH group on hardware */
            rc = nh_group_add(ofproto, nh_group);
            if (rc) {
                VLOG_ERR("Failed to add next hop group");
                nh_group_delete(ofproto, nh_group);
                return rc;
            }
            VLOG_DBG("%s: create new NH group %u",
                     __FUNCTION__, nh_group->nh_id);
        }

        /* Update route entry */
        xp_route->xp_route.nhEcmpSize = nh_group->size;
        xp_route->xp_route.nhId = nh_group->nh_id;

        status = xpsL3UpdateIpRouteEntry(ofproto->xpdev->id,
                                         &xp_route->xp_route);
        if (status) {
            VLOG_ERR("Could not update route on hardware. Err %d", status);
            nh_group_delete(ofproto, nh_group);
            return EACCES;
        }
        nh_group_unref(ofproto, xp_route->nh_group);
        xp_route->nh_group = nh_group;

    } else {
        /* Update NH entries on HW */
        HMAP_FOR_EACH(nh, hmap_node, &nh_group->nh_map) {
            status = xpsL3SetRouteNextHop(ofproto->xpdev->id,
                                          nh->xp_nh_id, &nh->xp_nh);
            if (status != XP_NO_ERR) {
                VLOG_ERR("Could not set next hop on hardware. Err %d", status);
                return EACCES;
            }
        }
    }

    return 0;
}

static int
route_add(struct ofproto_xpliant *ofproto, struct ofproto_route *route)
{
    uint8_t prefix_len;
    uint32_t route_index;
    uint32_t hash;
    xp_nh_group_entry_t *nh_group;
    xp_l3_mgr_t *mgr;
    xp_route_entry_t *e;
    xp_nh_entry_t *nh;
    XP_STATUS status;
    uint32_t i;
    int rc;

    mgr = ofproto->l3_mgr;

    VLOG_DBG("%s: %s", __FUNCTION__, route->prefix);

    /* Create new NH group or get reference to the existing NH group */
    nh_group = nh_group_lookup_by_route(mgr, route);
    if (nh_group == NULL) {
        VLOG_DBG("%s: allocate new nexthop group for route %s",
                 __FUNCTION__, route->prefix);

        /* Allocate NH group */
        nh_group = nh_group_alloc(route->n_nexthops);
        if (nh_group == NULL) {
            VLOG_ERR("Failed to allocate NH group");
            return ENOMEM;
        }

        /* Initialize NH group with NH entries */
        for (i = 0; i < route->n_nexthops; i++) {
            nh = nh_create(mgr, &route->nexthops[i]);
            if (nh == NULL) {
                VLOG_ERR("Failed to create NH entry %s", route->nexthops[i].id);
                /* The NH group has not been created on HW yet.
                 * So, just free resources. */
                nh_group_free(nh_group);
                return EHOSTUNREACH;
            }
            nh_insert(nh_group, nh);
        }

        /* Create NH group on hardware */
        rc = nh_group_add(ofproto, nh_group);
        if (rc) {
            VLOG_ERR("Failed to add next hop group");
            nh_group_delete(ofproto, nh_group);
            return rc;
        }
    }

    /* Allocate and initialize new route entry */
    e = xzalloc(sizeof *e);
    ovs_refcount_init(&e->ref_cnt);

    if (route->family == OFPROTO_ROUTE_IPV4) {
        in_addr_t ipv4_addr;

        rc = ops_xp_string_to_prefix(AF_INET, route->prefix, &ipv4_addr,
                                     &prefix_len);
        if (rc) {
            VLOG_ERR("Invalid IPv4/Prefix");
            nh_group_unref(ofproto, nh_group);
            free(e);
            return EPFNOSUPPORT;
        }

        e->xp_route.type = XP_PREFIX_TYPE_IPV4;
        memcpy(e->xp_route.ipv4Addr, &ipv4_addr, 4);
        e->xp_route.ipMaskLen = prefix_len;
    } else {
        struct in6_addr ipv6_addr;

        rc = ops_xp_string_to_prefix(AF_INET6, route->prefix, &ipv6_addr,
                                     &prefix_len);
        if (rc) {
            VLOG_ERR("Invalid IPv6/Prefix");
            nh_group_unref(ofproto, nh_group);
            free(e);
            return EPFNOSUPPORT;
        }
        e->xp_route.type = XP_PREFIX_TYPE_IPV6;
        memcpy(e->xp_route.ipv6Addr, &ipv6_addr, 16);
        e->xp_route.ipMaskLen = prefix_len;
    }

    e->nh_group = nh_group;
    e->xp_route.vrfId = ofproto->vrf_id;
    e->xp_route.nhEcmpSize = nh_group->size;
    e->xp_route.nhId = nh_group->nh_id;

    status = xpsL3AddIpRouteEntry(ofproto->xpdev->id, &e->xp_route,
                                  &route_index);
    if (status) {
        VLOG_ERR("Could not add route to hardware. Error: %d", status);
        nh_group_unref(ofproto, e->nh_group);
        free(e);
        return EAGAIN;
    }

    e->prefix = xstrdup(route->prefix);
    hash = hash_string(route->prefix, 0);
    hmap_insert(&mgr->route_map, &e->hmap_node, hash);

    VLOG_DBG("%s: RIB size %u. Added route %s",
             __FUNCTION__, hmap_count(&mgr->route_map), route->prefix);
    return 0;
}

static xp_route_entry_t *
route_lookup(xp_l3_mgr_t *mgr, const char *prefix)
{
    xp_route_entry_t *e;
    uint32_t hash;

    ovs_assert(mgr);
    ovs_assert(prefix);

    hash = hash_string(prefix, 0);
    VLOG_DBG("%s: Search for route %s with hash %08X",
             __FUNCTION__, prefix, hash);

    HMAP_FOR_EACH_WITH_HASH(e, hmap_node, hash, &mgr->route_map) {
        if (strcmp(e->prefix, prefix) == 0) {
            ovs_refcount_ref(&e->ref_cnt);
            return e;
        }
    }

    return NULL;
}

static void
route_delete(struct ofproto_xpliant *ofproto, xp_route_entry_t *route)
{
    xp_l3_mgr_t *mgr;
    XP_STATUS status;

    ovs_assert(ofproto);
    ovs_assert(ofproto->l3_mgr);
    ovs_assert(route);

    mgr = ofproto->l3_mgr;

    hmap_remove(&mgr->route_map, &route->hmap_node);

    VLOG_DBG("%s: RIB size %u. Removed route %s",
             __FUNCTION__, hmap_count(&mgr->route_map), route->prefix);

    status = xpsL3RemoveIpRouteEntry(ofproto->xpdev->id, &route->xp_route);
    if (status != XP_NO_ERR) {
        VLOG_WARN("Failed to remove route from hardware. Err %d", status);
    }

    nh_group_unref(ofproto, route->nh_group);
    free(route->prefix);
    free(route);
}

static void
route_unref(struct ofproto_xpliant *ofproto, xp_route_entry_t *route)
{
    ovs_assert(route);
    if (ovs_refcount_unref(&route->ref_cnt) == 1) {
        route_delete(ofproto, route);
    }
}

static int
add_route_entry(struct ofproto_xpliant *ofproto, struct ofproto_route *route)
{
    xp_route_entry_t *e;
    int rc;

    ovs_assert(ofproto);
    ovs_assert(ofproto->l3_mgr);
    ovs_assert(route);

    VLOG_DBG("%s: ofproto %s, route %s, n_nexthops %u",
             __FUNCTION__, ofproto->up.name, route->prefix, route->n_nexthops);

    e = route_lookup(ofproto->l3_mgr, route->prefix);
    if (e != NULL) {
        rc = route_update(ofproto, route, e);
        route_unref(ofproto, e);
        return rc;
    }

    rc = route_add(ofproto, route);
    return rc;
}

static int
delete_route_entry(struct ofproto_xpliant *ofproto,
                   struct ofproto_route *route)
{
    xp_route_entry_t *e;
    int rc = 0;

    ovs_assert(ofproto);
    ovs_assert(route);

    VLOG_DBG("%s: ofproto %s, route %s",
             __FUNCTION__, ofproto->up.name, route->prefix);

    e = route_lookup(ofproto->l3_mgr, route->prefix);
    if (e != NULL) {
        /* Un-reference the route since route_lookup() took a reference */
        route_unref(ofproto, e);
        /* Un-reference the route to trigger route removal */
        route_unref(ofproto, e);
    } else {
        VLOG_WARN("Unknown route %s", route->prefix);
        rc = EFAULT;
    }

    return rc;
}

static int
delete_nh_entry(struct ofproto_xpliant *ofproto, struct ofproto_route *route)
{
    xp_route_entry_t *e;
    xp_nh_entry_t *nh;
    uint32_t n_nexthops;
    XP_STATUS status;
    xp_nh_group_entry_t *nh_group;
    xp_nh_group_entry_t *old_nh_group;
    int rc;
    uint32_t i;

    ovs_assert(ofproto);
    ovs_assert(route);

    VLOG_DBG("%s: ofproto %s, route %s, n_nexthops %u",
             __FUNCTION__, ofproto->up.name, route->prefix, route->n_nexthops);

    e = route_lookup(ofproto->l3_mgr, route->prefix);
    if (e == NULL) {
        VLOG_WARN("%s: ofproto %s, route %s not found",
                  __FUNCTION__, ofproto->up.name, route->prefix);
        return 0;
    }

    if (e->nh_group == NULL) {
        VLOG_WARN("%s: ofproto %s, route %s does not have nexthops",
                  __FUNCTION__, ofproto->up.name, route->prefix);
        route_unref(ofproto, e);
        return 0;
    }

    /* Get the number of NHs that have to be removed */
    n_nexthops = 0;
    HMAP_FOR_EACH(nh, hmap_node, &e->nh_group->nh_map) {
        for (i = 0; i < route->n_nexthops; i++) {
            if (strcmp(nh->id, route->nexthops[i].id) == 0) {
                ++n_nexthops;
                break;
            }
        }
    }

    if (e->nh_group->size > n_nexthops) {
        n_nexthops = e->nh_group->size - n_nexthops;

        /* Allocate NH group */
        nh_group = nh_group_alloc(n_nexthops);
        if (nh_group == NULL) {
            VLOG_ERR("Failed to allocate NH group");
            route_unref(ofproto, e);
            return ENOMEM;
        }

        /* Copy valid NHs into the new NH group */
        HMAP_FOR_EACH(nh, hmap_node, &e->nh_group->nh_map) {
            bool valid = true;
            for (i = 0; i < route->n_nexthops; i++) {
                if (strcmp(nh->id, route->nexthops[i].id) == 0) {
                    valid = false;
                    break;
                }
            }

            if (valid) {
                nh_insert(nh_group, nh_dup(nh));
            }
        }

        old_nh_group = nh_group_lookup_by_group(ofproto->l3_mgr, nh_group);
        if (old_nh_group) {
            /* Reuse existing NH group */
            nh_group_free(nh_group);
            nh_group = old_nh_group;
            VLOG_DBG("%s: reuse existing NH group %u",
                     __FUNCTION__, nh_group->nh_id);
        } else {
            /* Create NH group on hardware */
            rc = nh_group_add(ofproto, nh_group);
            if (rc) {
                VLOG_ERR("Failed to add NH group");
                route_unref(ofproto, e);
                nh_group_delete(ofproto, nh_group);
                return rc;
            }
            VLOG_DBG("%s: create new NH group %u",
                     __FUNCTION__, nh_group->nh_id);
        }

        e->xp_route.nhEcmpSize = nh_group->size;
        e->xp_route.nhId = nh_group->nh_id;
    } else {
        VLOG_DBG("%s: ofproto %s, route %s without nexthops",
                 __FUNCTION__, ofproto->up.name, e->prefix);
        nh_group = NULL;
        e->xp_route.nhEcmpSize = 0;
        e->xp_route.nhId = 0;
    }

    status = xpsL3UpdateIpRouteEntry(ofproto->xpdev->id, &e->xp_route);
    if (status) {
        VLOG_ERR("Could not update route on hardware. Err %d", status);
        if (nh_group) {
            /* Delete NH group that just has been created */
            nh_group_delete(ofproto, nh_group);
        }
        route_unref(ofproto, e);
        return EPERM;
    }

    nh_group_unref(ofproto, e->nh_group);
    e->nh_group = nh_group;
    route_unref(ofproto, e);

    return 0;
}

static void
update_nexthop_error(int ret_code, struct ofproto_route *route)
{
    char *error_str = NULL;
    struct ofproto_route_nexthop *nh;

    error_str = strerror(ret_code);

    for (int i = 0;  i < route->n_nexthops; i++) {
        nh = &route->nexthops[i];
        nh->err_str = error_str;
        nh->rc = ret_code;
    }
}

int
ops_xp_routing_route_entry_action(struct ofproto_xpliant *ofproto,
                                  enum ofproto_route_action action,
                                  struct ofproto_route *route)
{
    int rc = 0;

    ovs_assert(ofproto);
    ovs_assert(ofproto->l3_mgr);
    ovs_assert(route);

    VLOG_DBG("%s: vrfid: %d, action: %d", __FUNCTION__, ofproto->vrf_id, action);

    if (!route->n_nexthops) {
        VLOG_ERR("route/nexthop entry null");
        return EINVAL; /* Return error */
    }

    VLOG_DBG("action: %d, vrf: %d, prefix: %s, nexthops: %d",
              action, ofproto->vrf_id, route->prefix, route->n_nexthops);

    ovs_mutex_lock(&ofproto->l3_mgr->mutex);

    switch (action) {
    case OFPROTO_ROUTE_ADD:
        rc = add_route_entry(ofproto, route);
        break;
    case OFPROTO_ROUTE_DELETE:
        rc = delete_route_entry(ofproto, route);
        break;
    case OFPROTO_ROUTE_DELETE_NH:
        rc = delete_nh_entry(ofproto, route);
        break;
    default:
        VLOG_ERR("Unknown route action %d", action);
        rc = EINVAL;
        break;
    }

    ovs_mutex_unlock(&ofproto->l3_mgr->mutex);

    if ((action == OFPROTO_ROUTE_ADD) && (rc != 0)) {
        update_nexthop_error(rc, route);
    }

    return rc;
} /* xp_routing_route_entry_action */

int
ops_xp_routing_ecmp_hash_set(struct ofproto_xpliant *ofproto,
                             unsigned int hash, bool enable)
{
    XP_STATUS status = XP_NO_ERR;
    xpHashField fields[XP_NUM_HASH_FIELDS];
    int size;

    VLOG_INFO("%s: %s: src-ip(%d), dst-ip(%d), src-port(%d), dst-port(%d)",
              __FUNCTION__, enable ? "Enable" : "Disable",
              !!(hash & OFPROTO_ECMP_HASH_SRCIP),
              !!(hash & OFPROTO_ECMP_HASH_DSTIP),
              !!(hash & OFPROTO_ECMP_HASH_SRCPORT),
              !!(hash & OFPROTO_ECMP_HASH_DSTPORT));

    if (!enable &&
        (hash & (OFPROTO_ECMP_HASH_SRCPORT | OFPROTO_ECMP_HASH_DSTPORT))) {

        VLOG_ERR("%s: SrcPort/DstPort hashing can't be disabled", __FUNCTION__);
        return EFAULT;
    }

    if (!enable &&
        (hash & OFPROTO_ECMP_HASH_SRCIP) &&
        (hash & OFPROTO_ECMP_HASH_DSTIP)) {

        VLOG_ERR("%s: Both SrcIP and DstIP hashing can't be disabled "
                 "at the same time", __FUNCTION__);
        return EFAULT;
    }

    hash = enable ?
            (ofproto->l3_mgr->ecmp_hash | hash) :
            (ofproto->l3_mgr->ecmp_hash & ~hash);
    ofproto->l3_mgr->ecmp_hash = hash;

    /* Disable hashing on Ethernet/IPv6 headers */
    size = 0;
    fields[size++] = XP_ETHERNET_S_TAG_TPID;
    fields[size++] = XP_ETHERNET_S_TAG_VID;

    if (hash & OFPROTO_ECMP_HASH_SRCPORT) {
        fields[size++] = XP_UDP_SOURCE_PORT;
        fields[size++] = XP_TCP_SOURCE_PORT;
    }

    if (hash & OFPROTO_ECMP_HASH_DSTPORT) {
        fields[size++] = XP_UDP_DESTINATION_PORT;
        fields[size++] = XP_TCP_DESTINATION_PORT;
    }

    if (hash & OFPROTO_ECMP_HASH_SRCIP) {
        fields[size++] = XP_IPV4_SOURCE_IP_ADDRESS;
    }

    if (hash & OFPROTO_ECMP_HASH_DSTIP) {
        fields[size++] = XP_IPV4_DESTINATION_IP_ADDRESS;
    }

    status = xpsL3SetHashFields(ofproto->xpdev->id, fields, size);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: xpsL3SetHashFields failed: %d", __FUNCTION__, status);
        return EFAULT;
    }

    return 0;
} /* xp_routing_ecmp_hash_set */

static int
l3_iface_mac_set(struct ofproto_xpliant *ofproto, xpsInterfaceId_t l3_intf_id,
                 macAddr_t mac, xpsVlan_t vid)
{
    XP_STATUS status = XP_NO_ERR;
    static struct ovsthread_once once = OVSTHREAD_ONCE_INITIALIZER;
    uint8_t xp_mac[ETH_ADDR_LEN];

    ops_xp_mac_copy_and_reverse(xp_mac, mac);

    if (ovsthread_once_start(&once)) {
        memcpy(ofproto->xpdev->router_mac, mac, ETH_ADDR_LEN);
        ofproto->xpdev->router_mac[5] = 0;

        status = xpsL3SetEgressRouterMacMSbs(ofproto->xpdev->id, &xp_mac[1]);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Failed to set egress router MAC "ETH_ADDR_FMT" Error: %d",
                     ETH_ADDR_BYTES_ARGS(ofproto->xpdev->router_mac), status);
            return EFAULT;
        }

        ovsthread_once_done(&once);
    }

    status = xpsL3SetIntfEgressRouterMacLSB(ofproto->xpdev->id, l3_intf_id,
                                            xp_mac[0]);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to set egress MAC LSB for L3 VLAN interface %u. "
                 "Error: %d", l3_intf_id, status);
        return EFAULT;
    }

    if (vid) {
        status = xpsL3AddIngressRouterVlanMac(ofproto->xpdev->id, vid, xp_mac);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Failed to set ingress router MAC for VLAN %u. "
                     "Error: %d", vid, status);
            return EFAULT;
        }
    } else {
        status = xpsL3AddIngressRouterMac(ofproto->xpdev->id, xp_mac);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Failed to set ingress router MAC. Error: %d", status);
            return EFAULT;
        }
    }

    return 0;
}

void
ops_xp_routing_disable_l3_interface(struct ofproto_xpliant *ofproto,
                                    xp_l3_intf_t *l3_intf)
{
    XP_STATUS status;
    xpsInterfaceType_e l3_intf_type = XPS_PORT;
    int rc;

    ovs_assert(ofproto);
    ovs_assert(l3_intf);
    ovs_assert(ofproto->vlan_mgr);

    VLOG_INFO("%s: L3 interface ID 0x%08X, VLAN %u",
              __FUNCTION__, l3_intf->l3_intf_id, l3_intf->vlan_id);

    status = xpsInterfaceGetType(l3_intf->intf_id, &l3_intf_type);
    if (status != XP_NO_ERR) {
        VLOG_WARN("Failed to get interface type for interface %u",
                  l3_intf->intf_id);
    }

    /* Remove interface ID to name mapping in xpdev. */
    ops_xp_dev_remove_intf_entry(ofproto->xpdev,
                                 l3_intf->l3_intf_id, 0);

    switch (l3_intf_type) {
    case XPS_PORT_ROUTER:
        status = xpsL3UnBindPortIntf(l3_intf->intf_id);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Failed to unbind port for L3 port interface. Error: %d",
                     status);
        }

        status = xpsL3DeInitPortIntf(ofproto->xpdev->id, l3_intf->l3_intf_id);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Failed to de-initialize L3 port interface. Error: %d",
                     status);
        }

        status = xpsL3DestroyPortIntf(l3_intf->l3_intf_id);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Failed to destroy L3 port interface. Error: %d",
                     status);
        }
        break;
    case XPS_SUBINTERFACE_ROUTER:
        status = xpsL3UnBindSubIntf(l3_intf->intf_id, l3_intf->l3_intf_id,
                                    l3_intf->vlan_id);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Failed to unbind VLAN %u for L3 sub-interface. Error: %d",
                     l3_intf->vlan_id, status);
        }

        status = xpsL3DeInitSubIntf(ofproto->xpdev->id, l3_intf->l3_intf_id);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Failed to de-initialize L3 sub-interface. Error: %d",
                     status);
        }

        status = xpsL3DestroySubIntf(l3_intf->l3_intf_id);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Failed to destroy L3 sub-interface. Error: %d",
                     status);
        }
        break;
    case XPS_VLAN_ROUTER:
        status = xpsL3DestroyVlanIntf(l3_intf->vlan_id, l3_intf->l3_intf_id);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Could not remove L3 interface. Error: %d", status);
        }

        status = xpsVlanSetArpBcCmd(ofproto->xpdev->id, l3_intf->vlan_id,
                                    XP_PKTCMD_FWD);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Failed to set ARP forward action for VLAN %u on Device %d",
                     l3_intf->vlan_id, ofproto->xpdev->id);
        }
        break;
    default:
        /* do nothing, just free control structure */
        break;
    }

    free(l3_intf);
}

void
ops_xp_routing_update_l3_interface(struct ofproto_xpliant *ofproto,
                                   xp_l3_intf_t *l3_intf)
{
    XP_STATUS status;
    xpsInterfaceType_e l3_intf_type = XPS_PORT;
    int rc;

    ovs_assert(ofproto);
    ovs_assert(l3_intf);

    VLOG_INFO("%s: L3 interface ID 0x%08X, VLAN %u",
              __FUNCTION__, l3_intf->l3_intf_id, l3_intf->vlan_id);

    status = xpsInterfaceGetType(l3_intf->intf_id, &l3_intf_type);
    if (status != XP_NO_ERR) {
        VLOG_WARN("Failed to get interface type for interface %u",
                  l3_intf->intf_id);
    }

    switch (l3_intf_type) {
    case XPS_PORT_ROUTER:
        status = xpsL3BindPortIntf(l3_intf->intf_id, l3_intf->l3_intf_id);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Failed bind L3 port interface. Error: %d",
                     status);
        }

        break;
    case XPS_SUBINTERFACE_ROUTER:
        status = xpsL3BindSubIntf(l3_intf->intf_id, l3_intf->l3_intf_id,
                                  l3_intf->vlan_id);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Failed bind VLAN %u for L3 sub-interface. Error: %d",
                     l3_intf->vlan_id, status);
        }
        break;
    case XPS_VLAN_ROUTER:
    default:
        /* do nothing */
        break;
    }

}

static xp_l3_intf_t *
l3_vlan_iface_create(struct ofproto_xpliant *ofproto,
                     xpsVlan_t vid, macAddr_t mac)
{
    xp_l3_intf_t *l3_intf;
    XP_STATUS status;
    int rc;

    l3_intf = xzalloc(sizeof *l3_intf);

    status = xpsL3CreateVlanIntf(vid, &l3_intf->l3_intf_id);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to create L3 VLAN interface on VLAN %u. Error: %d",
                 vid, status);
        goto error;
    }

    status = xpsL3SetIntfVrf(ofproto->xpdev->id, l3_intf->l3_intf_id,
                             ofproto->vrf_id);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to configure VRF index for L3 VLAN interface "
                 "on VLAN %u. Error: %d", vid, status);
        goto error;
    }

    status = xpsL3SetIntfIpv4UcRoutingEn(ofproto->xpdev->id,
                                         l3_intf->l3_intf_id, 1);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to enable IPv4 UC routing for L3 VLAN interface "
                 "on VLAN %u. Error: %d", vid, status);
        goto error;
    }

    status = xpsL3SetIntfIpv6UcRoutingEn(ofproto->xpdev->id,
                                         l3_intf->l3_intf_id, 1);
    if (status) {
        VLOG_ERR("Failed to enable IPv6 UC routing for L3 VLAN interface "
                 "on VLAN %u. Error: %d", vid, status);
        goto error;
    }

    rc = l3_iface_mac_set(ofproto, l3_intf->l3_intf_id, mac, vid);
    if (rc != 0) {
        VLOG_ERR("Failed to set MAC for L3 VLAN interface on VLAN %u", vid);
        goto error;
    }

    status = xpsVlanSetArpBcCmd(ofproto->xpdev->id, vid, XP_PKTCMD_FWD_MIRROR);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to set ARP trap action for VLAN %u on Device %d",
                 vid, ofproto->xpdev->id);
        goto error;
    }

    l3_intf->intf_id = XPS_INTF_INVALID_ID;
    l3_intf->l3_vrf = ofproto->vrf_id;
    l3_intf->vlan_id = vid;

    return l3_intf;

error:

    free(l3_intf);

    return NULL;
}

static xp_l3_intf_t *
l3_port_iface_create(struct ofproto_xpliant *ofproto, xpsInterfaceId_t if_id,
                     macAddr_t mac)
{
    xp_l3_intf_t *l3_intf;
    XP_STATUS status;
    int rc;

    l3_intf = xzalloc(sizeof *l3_intf);

    status = xpsL3CreatePortIntf(&l3_intf->l3_intf_id);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to create L3 port interface. Error: %d",
                 status);
        goto error;
    }

    status = xpsL3InitPortIntf(ofproto->xpdev->id, l3_intf->l3_intf_id);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to initialize L3 port interface. Error: %d",
                 status);
        goto error;
    }

    status = xpsL3BindPortIntf(if_id, l3_intf->l3_intf_id);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed bind L3 port interface to port/LAG %u. Error: %d",
                 if_id, status);
        goto error;
    }

    status = xpsL3SetIntfVrf(ofproto->xpdev->id, l3_intf->l3_intf_id,
                             ofproto->vrf_id);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to configure VRF index for L3 port interface. "
                 "Error: %d", status);
        goto error;
    }

    status = xpsL3SetIntfIpv4UcRoutingEn(ofproto->xpdev->id,
                                         l3_intf->l3_intf_id, 1);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to enable IPv4 UC routing for L3 port interface. "
                 "Error: %d", status);
        goto error;
    }

    status = xpsL3SetIntfIpv6UcRoutingEn(ofproto->xpdev->id,
                                         l3_intf->l3_intf_id, 1);
    if (status) {
        VLOG_ERR("Failed to enable IPv6 UC routing for L3 port interface. "
                 "Error: %d", status);
        goto error;
    }

    rc = l3_iface_mac_set(ofproto, l3_intf->l3_intf_id, mac, (xpsVlan_t)0);
    if (rc != 0) {
        VLOG_ERR("Failed to set MAC for L3 port interface");
        goto error;
    }

    l3_intf->intf_id = if_id;
    l3_intf->l3_vrf = ofproto->vrf_id;
    l3_intf->vlan_id = (xpsVlan_t)0;

    return l3_intf;

error:

    free(l3_intf);

    return NULL;
}

static xp_l3_intf_t *
l3_sub_iface_create(struct ofproto_xpliant *ofproto, xpsInterfaceId_t if_id,
                    xpsVlan_t vid, macAddr_t mac)
{
    xp_l3_intf_t *l3_intf;
    XP_STATUS status;
    int rc;

    l3_intf = xzalloc(sizeof *l3_intf);

    status = xpsL3CreateSubIntf(&l3_intf->l3_intf_id);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to create L3 sub-interface. Error: %d",
                 status);
        goto error;
    }

    status = xpsL3InitSubIntf(ofproto->xpdev->id, l3_intf->l3_intf_id);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to initialize L3 sub-interface. Error: %d",
                 status);
        goto error;
    }

    status = xpsL3BindSubIntf(if_id, l3_intf->l3_intf_id, vid);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed bind VLAN %u for L3 sub-interface to port/LAG %u. "
                 "Error: %d", vid, if_id, status);
        goto error;
    }

    status = xpsL3SetIntfVrf(ofproto->xpdev->id, l3_intf->l3_intf_id,
                             ofproto->vrf_id);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to configure VRF index for L3 sub-interface "
                 "on VLAN %u. Error: %d", vid, status);
        goto error;
    }

    status = xpsL3SetIntfIpv4UcRoutingEn(ofproto->xpdev->id,
                                         l3_intf->l3_intf_id, 1);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to enable IPv4 UC routing for L3 sub-interface "
                 "on VLAN %u. Error: %d", vid, status);
        goto error;
    }

    status = xpsL3SetIntfIpv6UcRoutingEn(ofproto->xpdev->id,
                                         l3_intf->l3_intf_id, 1);
    if (status) {
        VLOG_ERR("Failed to enable IPv6 UC routing for L3 sub-interface "
                 "on VLAN %u. Error: %d", vid, status);
        goto error;
    }

    rc = l3_iface_mac_set(ofproto, l3_intf->l3_intf_id, mac, vid);
    if (rc != 0) {
        VLOG_ERR("Failed to set MAC for L3 sub-interface on VLAN %u", vid);
        goto error;
    }

    l3_intf->intf_id = if_id;
    l3_intf->l3_vrf = ofproto->vrf_id;
    l3_intf->vlan_id = vid;

    return l3_intf;

error:

    free(l3_intf);

    return NULL;
}

xp_l3_intf_t *
ops_xp_routing_enable_l3_interface(struct ofproto_xpliant *ofproto,
                                   xpsInterfaceId_t if_id, char *if_name, 
                                   macAddr_t mac)
{
    XP_STATUS status;
    xpsInterfaceType_e if_type;
    xp_l3_intf_t *l3_intf;
    int rc;

    ovs_assert(ofproto);

    VLOG_INFO("%s: Interface ID %u, MAC "ETH_ADDR_FMT,
              __FUNCTION__, if_id, ETH_ADDR_BYTES_ARGS(mac));

    status = xpsInterfaceGetType(if_id, &if_type);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to get interface type. Error: %d", status);
        return NULL;
    }

    if ((if_type != XPS_PORT) && (if_type != XPS_LAG)) {
        VLOG_ERR("Invalid interface type %u. Error: %d", if_type, status);
        return NULL;
    }

    l3_intf = l3_port_iface_create(ofproto, if_id, mac);
    if (l3_intf == NULL) {
        VLOG_ERR("Failed to create L3 port interface");
        return NULL;
    }

    /* Add interface ID to name mapping in xpdev.*/
    ops_xp_dev_add_intf_entry(ofproto->xpdev,
                              l3_intf->l3_intf_id,
                              if_name, 0);

    VLOG_DBG("%s: L3 interface ID 0x%08X", __FUNCTION__, l3_intf->l3_intf_id);

    return l3_intf;
}

xp_l3_intf_t *
ops_xp_routing_enable_l3_subinterface(struct ofproto_xpliant *ofproto,
                                      xpsInterfaceId_t if_id, char *if_name,
                                      xpsVlan_t vid, macAddr_t mac)
{
    XP_STATUS status;
    xpsInterfaceType_e if_type;
    xp_l3_intf_t *l3_intf;
    int rc;

    ovs_assert(ofproto);
    ovs_assert(ofproto->vlan_mgr);

    VLOG_INFO("%s: Interface ID %u, VLAN %u, MAC "ETH_ADDR_FMT,
              __FUNCTION__, if_id, vid, ETH_ADDR_BYTES_ARGS(mac));

    status = xpsInterfaceGetType(if_id, &if_type);
    if (status != XP_NO_ERR) {
        VLOG_ERR("Failed to get interface type. Error: %d", status);
        return NULL;
    }

    if (if_type != XPS_PORT) {
        VLOG_ERR("Invalid interface type %u. Error: %d", if_type, status);
        return NULL;
    }

    l3_intf = l3_sub_iface_create(ofproto, if_id, vid, mac);
    if (l3_intf == NULL) {
        VLOG_ERR("Failed to create L3 VLAN interface on VLAN %u", vid);
        return NULL;
    }

    /* Add interface ID to name mapping in xpdev.*/
    ops_xp_dev_add_intf_entry(ofproto->xpdev,
                              l3_intf->l3_intf_id,
                              if_name, 0);

    VLOG_DBG("%s: L3 interface ID 0x%08X", __FUNCTION__, l3_intf->l3_intf_id);

    return l3_intf;
}

xp_l3_intf_t *
ops_xp_routing_enable_l3_vlan_interface(struct ofproto_xpliant *ofproto,
                                        xpsVlan_t vid, char *if_name, 
                                        macAddr_t mac)
{
    xp_l3_intf_t *l3_intf;
    int rc;

    VLOG_INFO("%s: VLAN %u, MAC "ETH_ADDR_FMT,
              __FUNCTION__, vid, ETH_ADDR_BYTES_ARGS(mac));

    if (!ops_xp_vlan_is_existing(ofproto->vlan_mgr, vid)) {
        VLOG_ERR("VLAN %u does not exist", vid);
        return NULL;
    }

    l3_intf = l3_vlan_iface_create(ofproto, vid, mac);
    if (l3_intf == NULL) {
        VLOG_ERR("Failed to create L3 VLAN interface on VLAN %u", vid);
        return NULL;
    }

    /* Add interface ID to name mapping in xpdev.*/
    ops_xp_dev_add_intf_entry(ofproto->xpdev,
                              l3_intf->l3_intf_id,
                              if_name, 0);

    VLOG_DBG("%s: L3 interface ID 0x%08X", __FUNCTION__, l3_intf->l3_intf_id);

    return l3_intf;
}

const char *
ops_xp_pkt_cmd_to_string(xpPktCmd_e cmd)
{
    switch (cmd) {
    case XP_PKTCMD_DROP: return "drop";
    case XP_PKTCMD_FWD: return "fwd";
    case XP_PKTCMD_TRAP: return "trap";
    case XP_PKTCMD_FWD_MIRROR: return "mirror";
    }
    return "-";
}

static void
unixctl_l3_show(struct unixctl_conn *conn, int argc OVS_UNUSED,
                const char *argv[], void *aux OVS_UNUSED)
{
    struct sset names;
    const char *name;
    const struct ofproto_xpliant *ofproto = NULL;
    struct ds d_str = DS_EMPTY_INITIALIZER;

    ds_put_cstr(&d_str, "=================================================\n");
    ds_put_cstr(&d_str, "VRF            Routes      Hosts       NH Groups \n");
    ds_put_cstr(&d_str, "=================================================\n");

    sset_init(&names);
    ofproto_enumerate_names("vrf", &names);
    SSET_FOR_EACH (name, &names) {
        ofproto = ops_xp_ofproto_lookup(name);

        if (ofproto && ofproto->l3_mgr) {
            ovs_mutex_lock(&ofproto->l3_mgr->mutex);
            ds_put_format(&d_str, "%-15s%-12u%-12u%-12u\n",
                          ofproto->up.name,
                          hmap_count(&ofproto->l3_mgr->route_map),
                          hmap_count(&ofproto->l3_mgr->host_map),
                          hmap_count(&ofproto->l3_mgr->nh_group_map));
            ovs_mutex_unlock(&ofproto->l3_mgr->mutex);
        }
    }
    sset_destroy(&names);

    unixctl_command_reply(conn, ds_cstr(&d_str));
    ds_destroy(&d_str);
}

static void
xp_l3_mgr_show_routes(xp_l3_mgr_t *mgr, struct ds *d_str)
{
    xp_route_entry_t *route = NULL;

    if (!mgr) {
        VLOG_ERR("%s, No L3 manager present.", __FUNCTION__);
        return;
    }

    ovs_assert(d_str);

    ds_put_cstr(d_str, "=================================================================\n");
    ds_put_cstr(d_str, "Network             Gateway          Intf      Action    NH Grp  \n");
    ds_put_cstr(d_str, "=================================================================\n");

    ovs_mutex_lock(&mgr->mutex);

    /* Dump static routes. */
    HMAP_FOR_EACH(route, hmap_node, &mgr->route_map) {
        if (route->nh_group) {
            xp_nh_entry_t *nh;
            uint32_t nh_cnt = 0;

            HMAP_FOR_EACH(nh, hmap_node, &route->nh_group->nh_map) {
                ds_put_format(d_str, "%-20s%-17s%-10s%-10s%u\n",
                              (nh_cnt ? "" : route->prefix),
                              (nh->nh_port ? "*" : nh->id),
                              (nh->nh_port ? nh->id : ""),
                              ops_xp_pkt_cmd_to_string(nh->xp_nh.pktCmd),
                              route->nh_group->nh_id);
                ++nh_cnt;
            }
        } else {
            ds_put_format(d_str, "%-20s%\n", route->prefix);
        }
    }

    ovs_mutex_unlock(&mgr->mutex);
}

static void
unixctl_l3_show_routes(struct unixctl_conn *conn, int argc OVS_UNUSED,
                       const char *argv[], void *aux OVS_UNUSED)
{
    const struct ofproto_xpliant *ofproto = NULL;
    struct ds d_str = DS_EMPTY_INITIALIZER;

    ofproto = ops_xp_ofproto_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such bridge");
        return;
    }

    xp_l3_mgr_show_routes(ofproto->l3_mgr, &d_str);
    unixctl_command_reply(conn, ds_cstr(&d_str));
    ds_destroy(&d_str);
}

static void
l3_mgr_show_hosts(xp_l3_mgr_t *mgr, struct ds *d_str)
{
    xp_host_entry_t *e = NULL;

    if (!mgr) {
        VLOG_ERR("%s, No L3 manager present.", __FUNCTION__);
        return;
    }

    ovs_assert(d_str);

    ds_put_cstr(d_str, "============================================================================================\n");
    ds_put_cstr(d_str, "Host                MAC                 L3 intf        Egress intf    Service Id    Action  \n");
    ds_put_cstr(d_str, "============================================================================================\n");

    ovs_mutex_lock(&mgr->mutex);

    /* Dump static routes. */
    HMAP_FOR_EACH(e, hmap_node, &mgr->host_map) {
        int af;
        uint8_t ip[sizeof(ipv6Addr_t)];
        char ip_str[128];
        char mac_str[XP_STR_MAC_BUF_LEN];
        macAddr_t macDa;

        if (e->xp_host.type == XP_PREFIX_TYPE_IPV4) {
            af = AF_INET;
            ops_xp_ip_addr_copy_and_reverse(ip, e->xp_host.ipv4Addr, false);
        } else {
            af = AF_INET6;
            ops_xp_ip_addr_copy_and_reverse(ip, e->xp_host.ipv6Addr, true);
        }
        ops_xp_mac_copy_and_reverse(macDa, e->xp_host.nhEntry.nextHop.macDa);
        snprintf(mac_str, sizeof(mac_str),
                 ETH_ADDR_FMT, ETH_ADDR_BYTES_ARGS(macDa));

        ds_put_format(d_str, "%-20s%-20s%-15u%-15u%-14u%s\n",
                      inet_ntop(af, ip, ip_str, sizeof(ip_str)), mac_str,
                      e->xp_host.nhEntry.nextHop.l3InterfaceId,
                      e->xp_host.nhEntry.nextHop.egressIntfId,
                      e->xp_host.nhEntry.serviceInstId,
                      ops_xp_pkt_cmd_to_string(e->xp_host.nhEntry.pktCmd));
    }

    ovs_mutex_unlock(&mgr->mutex);
}

static void
unixctl_l3_show_hosts(struct unixctl_conn *conn, int argc OVS_UNUSED,
                         const char *argv[], void *aux OVS_UNUSED)
{
    const struct ofproto_xpliant *ofproto = NULL;
    struct ds d_str = DS_EMPTY_INITIALIZER;

    ofproto = ops_xp_ofproto_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such bridge");
        return;
    }

    l3_mgr_show_hosts(ofproto->l3_mgr, &d_str);
    unixctl_command_reply(conn, ds_cstr(&d_str));
    ds_destroy(&d_str);
}

static void
l3_mgr_show_nexthops(xp_l3_mgr_t *mgr, struct ds *d_str)
{
    xp_nh_entry_t *nh_e = NULL;
    xp_nh_group_entry_t *nhg_e = NULL;

    if (!mgr) {
        VLOG_ERR("%s, No L3 manager present.", __FUNCTION__);
        return;
    }

    ovs_assert(d_str);

    ds_put_cstr(d_str, "================================================================================================================\n");
    ds_put_cstr(d_str, "Group     Ref       NextHop             MAC                 L3 intf        Egress intf    Service Id    Action  \n");
    ds_put_cstr(d_str, "================================================================================================================\n");

    ovs_mutex_lock(&mgr->mutex);

    /* Dump static routes. */
    HMAP_FOR_EACH(nhg_e, hmap_node, &mgr->nh_group_map) {
        uint32_t nh_cnt = 0;
        HMAP_FOR_EACH(nh_e, hmap_node, &nhg_e->nh_map) {
            char mac_str[XP_STR_MAC_BUF_LEN];
            macAddr_t macDa;

            if (nh_cnt == 0) {
                ds_put_format(d_str, "%-10u%-10u",
                              nhg_e->nh_id, nhg_e->ref_cnt);
            } else {
                ds_put_format(d_str, "%-20s", "");
            }

            ops_xp_mac_copy_and_reverse(macDa, nh_e->xp_nh.nextHop.macDa);
            snprintf(mac_str, sizeof(mac_str),
                     ETH_ADDR_FMT, ETH_ADDR_BYTES_ARGS(macDa));

            ds_put_format(d_str, "%-20s%-20s%-15u%-15u%-14u%s\n",
                          nh_e->id, mac_str,
                          nh_e->xp_nh.nextHop.l3InterfaceId,
                          nh_e->xp_nh.nextHop.egressIntfId,
                          nh_e->xp_nh.serviceInstId,
                          ops_xp_pkt_cmd_to_string(nh_e->xp_nh.pktCmd));
            ++nh_cnt;
        }
    }

    ovs_mutex_unlock(&mgr->mutex);
}

static void
unixctl_l3_show_nexthops(struct unixctl_conn *conn, int argc OVS_UNUSED,
                         const char *argv[], void *aux OVS_UNUSED)
{
    const struct ofproto_xpliant *ofproto = NULL;
    struct ds d_str = DS_EMPTY_INITIALIZER;

    ofproto = ops_xp_ofproto_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such bridge");
        return;
    }

    l3_mgr_show_nexthops(ofproto->l3_mgr, &d_str);
    unixctl_command_reply(conn, ds_cstr(&d_str));
    ds_destroy(&d_str);
}

static void
l3_test_routes(struct ofproto_xpliant *ofproto, const char *prefix,
               uint32_t count, uint32_t ignore_err)
{
    struct ofproto_route route;
    enum ofproto_route_action action;
    uint32_t prefix_step;
    char next_prefix[64];
    uint32_t i;
    int rc;
    xp_l3_dbg_t *dbg;

    uint32_t ipv4_addr;
    uint8_t prefix_len;
    FILE *route_file = NULL;

    dbg = ofproto->l3_mgr->dbg;
    if (dbg == NULL) {
        return;
    }

    rc = ops_xp_string_to_prefix(AF_INET, prefix, &ipv4_addr, &prefix_len);
    if (rc) {
        uint32_t routes;

        route_file = fopen(prefix, "r");
        if (!route_file) {
            dbg->started = false;
            return;
        }

        if (!fscanf(route_file, "%u", &routes)) {
            fclose(route_file);
            dbg->started = false;
            return;
        }
    } else {
        prefix_step = 1 << (32 - prefix_len);
    }

    route.family = OFPROTO_ROUTE_IPV4;
    route.prefix = next_prefix;
    route.n_nexthops = 1;
    route.nexthops[0].id = "1";
    route.nexthops[0].type = OFPROTO_NH_PORT;
    route.nexthops[0].state = OFPROTO_NH_UNRESOLVED;
    action = dbg->add_routes ? OFPROTO_ROUTE_ADD : OFPROTO_ROUTE_DELETE;

    for (i = 0; (i < count) && !dbg->kickout; i++) {
        struct in_addr in_addr;
        struct timeval t1,t2,r;
        uint32_t sec, usec;

        if (route_file) {
            uint32_t mask;
            rc = fscanf(route_file, "%u%x", &mask, &ipv4_addr);
            if (rc != 2) {
                break;
            }
            prefix_len = mask;
        }
        in_addr.s_addr = htonl(ipv4_addr);

        snprintf(next_prefix, sizeof(next_prefix), "%s/%u",
                 inet_ntoa(in_addr), prefix_len);

        if (!route_file) {
            ipv4_addr += prefix_step;
        }

        /* Populate route */
        EXEC_DELAY_GET(rc = ops_xp_routing_route_entry_action(ofproto, action, &route),
                       sec, usec);

        if (rc && !ignore_err) {
            break;
        }

        /* Count insertion time even API failed */
        t1.tv_sec = dbg->exec_sec;
        t1.tv_usec = dbg->exec_usec;
        t2.tv_sec = sec;
        t2.tv_usec = usec;
        timeradd(&t2, &t1, &r);

        /* Update L3 Manager statistics */
        {
            ovs_mutex_lock(&ofproto->l3_mgr->mutex);

            dbg->exec_sec = r.tv_sec;
            dbg->exec_usec = r.tv_usec;

            if (rc == 0) {
                if (dbg->add_routes) {
                    ++(dbg->active_routes);
                    ++(dbg->updated_routes);
                } else if (dbg->active_routes) {
                    --(dbg->active_routes);
                    ++(dbg->updated_routes);
                }
            } else {
                ++(dbg->errors);
            }

            if (dbg->add_routes) {
                uint32_t totally = dbg->errors + dbg->active_routes;

                if (totally == 10000 || totally == 20000 || !(totally % 50000)) {
                    VLOG_ERR("Totally routes (%u), Populated routes (%u), "
                             "Skipped routes (%u), Execution time (%u.%06u seconds)",
                             totally, dbg->active_routes,
                             dbg->errors, dbg->exec_sec, dbg->exec_usec);
                }
            }

            ovs_mutex_unlock(&ofproto->l3_mgr->mutex);
        }
    }

    if (route_file) {
        fclose(route_file);
    }

    dbg->started = false;
}

static void *
l3_test_handler(void *arg)
{
    xp_l3_test_params_t *params = (xp_l3_test_params_t *)arg;

    l3_test_routes(params->ofproto, params->prefix,
                   params->count, params->ignore_err);
    free(params->prefix);
    return NULL;
}

static void
unixctl_l3_test_add_routes(struct unixctl_conn *conn, int argc OVS_UNUSED,
                           const char *argv[], void *aux OVS_UNUSED)
{
    struct ofproto_xpliant *ofproto = NULL;
    struct ds d_str = DS_EMPTY_INITIALIZER;
    const char *prefix_s = argv[2];
    const char *count_s = argc > 3 ? argv[3] : "1";
    const char *ignore_err_s = argc > 4 ? argv[4] : "0";
    uint32_t count = 0;
    uint32_t ignore_err = 0;
    static xp_l3_test_params_t params;
    xp_l3_dbg_t *dbg;
    int rc;

    ofproto = ops_xp_ofproto_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such VRF");
        return;
    }

    dbg = ofproto->l3_mgr->dbg;
    if (dbg == NULL) {
        unixctl_command_reply_error(conn, "debugging disabled");
        return;
    }

    if (ovs_scan(count_s, "%u", &count) == false) {
        unixctl_command_reply_error(conn, "failed to parse routes count");
        return;
    }

    if (ovs_scan(ignore_err_s, "%u", &ignore_err) == false) {
        unixctl_command_reply_error(conn, "failed to parse ignore flag");
        return;
    }

    /* Check that the test is not currently running */
    ovs_mutex_lock(&ofproto->l3_mgr->mutex);
    if (dbg->started) {
        ovs_mutex_unlock(&ofproto->l3_mgr->mutex);
        unixctl_command_reply_error(conn, "Failed to start test. "
                                    "Another test is currently running.");
        return;
    }
    dbg->exec_sec = 0;
    dbg->exec_usec = 0;
    dbg->updated_routes = 0;
    dbg->errors = 0;
    dbg->started = true;
    dbg->add_routes = true;
    dbg->kickout = false;
    ovs_mutex_unlock(&ofproto->l3_mgr->mutex);

    params.ofproto = ofproto;
    params.prefix = xstrdup(prefix_s);
    params.count = count;
    params.ignore_err = ignore_err;

    ovs_thread_create("ops-xp-l3-test", l3_test_handler, &params);

    ds_put_format(&d_str, "Started populating of %u routes "
                  "in the background...\n", count);

    unixctl_command_reply(conn, ds_cstr(&d_str));
    ds_destroy(&d_str);
}

static void
unixctl_l3_test_del_routes(struct unixctl_conn *conn, int argc OVS_UNUSED,
                           const char *argv[], void *aux OVS_UNUSED)
{
    struct ofproto_xpliant *ofproto = NULL;
    struct ds d_str = DS_EMPTY_INITIALIZER;
    const char *prefix_s = argc > 2 ? argv[2] : "";
    const char *count_s = argc > 3 ? argv[3] : "1";
    uint32_t count = 0;
    static xp_l3_test_params_t params;
    xp_l3_dbg_t *dbg;
    uint32_t i;
    int rc;

    ofproto = ops_xp_ofproto_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such bridge");
        return;
    }

    dbg = ofproto->l3_mgr->dbg;
    if (dbg == NULL) {
        unixctl_command_reply_error(conn, "debugging disabled");
        return;
    }

    if (strlen(prefix_s) == 0) {
        dbg->kickout = true;
        unixctl_command_reply(conn, "sent a signal to stop routes populating");
        return;
    }

    if (ovs_scan(count_s, "%u", &count) == false) {
        unixctl_command_reply_error(conn, "failed to parse routes count");
        return;
    }

    dbg->kickout = true;

    i = 3;
    while (dbg->started && (i > 0)) {
        sleep(1);
        --i;
    }

    /* Check that the test is not currently running */
    ovs_mutex_lock(&ofproto->l3_mgr->mutex);
    if (dbg->started) {
        ovs_mutex_unlock(&ofproto->l3_mgr->mutex);
        unixctl_command_reply_error(conn, "Failed to start test. "
                                    "Another test is currently running.");
        return;
    }
    dbg->exec_sec = 0;
    dbg->exec_usec = 0;
    dbg->updated_routes = 0;
    dbg->errors = 0;
    dbg->started = true;
    dbg->add_routes = false;
    dbg->kickout = false;
    ovs_mutex_unlock(&ofproto->l3_mgr->mutex);

    params.ofproto = ofproto;
    params.prefix = xstrdup(prefix_s);
    params.count = count;
    params.ignore_err = 1;

    ovs_thread_create("ops-xp-l3-test", l3_test_handler, &params);

    ds_put_format(&d_str, "Started removing of %u routes "
                  "in the background...\n", count);

    unixctl_command_reply(conn, ds_cstr(&d_str));
    ds_destroy(&d_str);
}

static void
unixctl_l3_test_show_routes(struct unixctl_conn *conn, int argc OVS_UNUSED,
                            const char *argv[], void *aux OVS_UNUSED)
{
    const struct ofproto_xpliant *ofproto = NULL;
    struct ds d_str = DS_EMPTY_INITIALIZER;
    xp_l3_dbg_t *dbg;

    ofproto = ops_xp_ofproto_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such bridge");
        return;
    }

    dbg = ofproto->l3_mgr->dbg;
    if (dbg == NULL) {
        unixctl_command_reply_error(conn, "debugging disabled");
        return;
    }

    ds_put_cstr(&d_str, "====================================================\n");
    ds_put_format(&d_str, "Test state        : %s\n",
                  dbg->started ? (dbg->add_routes ?
                  "populating routes" : "deleting routes") : "not running");
    if (dbg->add_routes) {
        ds_put_format(&d_str,
                          "Populated routes  : %u\n", dbg->updated_routes);
    } else {
        ds_put_format(&d_str,
                          "Removed routes    : %u\n", dbg->updated_routes);
    }
    ds_put_format(&d_str, "Active routes     : %u\n", dbg->active_routes);
    ds_put_format(&d_str, "Skipped routes    : %u\n", dbg->errors);
    ds_put_format(&d_str, "Execution time    : %u.%06u seconds\n",
                  dbg->exec_sec, dbg->exec_usec);
    ds_put_cstr(&d_str, "====================================================\n");

    unixctl_command_reply(conn, ds_cstr(&d_str));
    ds_destroy(&d_str);
}

void
ops_xp_routing_unixctl_init(void)
{
    static bool registered;
    if (registered) {
        return;
    }
    registered = true;

    unixctl_command_register("xp/l3/show", "", 0, 0,
                             unixctl_l3_show, NULL);
    unixctl_command_register("xp/l3/show-routes", "vrf", 1, 1,
                             unixctl_l3_show_routes, NULL);
    unixctl_command_register("xp/l3/show-hosts", "vrf", 1, 1,
                             unixctl_l3_show_hosts, NULL);
    unixctl_command_register("xp/l3/show-nexthops", "vrf", 1, 1,
                             unixctl_l3_show_nexthops, NULL);
    unixctl_command_register("xp/l3/test/add-routes",
                             "vrf {start_prefix | file} [count [ignore_err]]",
                             2, 4, unixctl_l3_test_add_routes, NULL);
    unixctl_command_register("xp/l3/test/del-routes",
                             "vrf [{start_prefix | file} [count]]", 1, 3,
                             unixctl_l3_test_del_routes, NULL);
    unixctl_command_register("xp/l3/test/show-routes", "vrf", 1, 1,
                             unixctl_l3_test_show_routes, NULL);
}
