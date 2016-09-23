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
 * File: ops-xp-ofproto-provider.c
 *
 * Purpose: This file contains OpenSwitch ofproto provider related
 *          application code for the Cavium/XPliant SDK.
 */

#include <config.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <poll.h>
#include <net/ethernet.h>

#include "ops-xp-ofproto-provider.h"
#include "ops-xp-ofproto-datapath.h"
#include "netdev-provider.h"
#include "netdev.h"
#include "poll-loop.h"
#include "simap.h"
#include "smap.h"
#include "sset.h"
#include "stp.h"
#include <openvswitch/vlog.h>
#include <vswitch-idl.h>
#include <openswitch-idl.h>
#include <ofproto/bond.h>
#include "unixctl.h"
#include "socket-util.h"

#include "ops-xp-vlan-bitmap.h"
#include "ops-xp-mac-learning.h"
#include "ops-xp-dev.h"
#include "ops-xp-dev-init.h"
#include "ops-xp-netdev.h"
#include "ops-xp-vlan.h"
#include "ops-xp-lag.h"
#include "ops-xp-routing.h"
#include "ops-xp-util.h"
#include "openXpsVlan.h"
#include "openXpsPacketDrv.h"
#include "openXpsStp.h"
#include "openXpsLag.h"
#include "openXpsMac.h"
#include "openXpsReasonCodeTable.h"
#include "openXpsInit.h"


VLOG_DEFINE_THIS_MODULE(xp_ofproto_provider);

/* Global variables */
static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);

/* ofproto Xpliant data structures */
const struct ofproto_class ofproto_xpliant_class;

/* All existing ofproto_xpliant instances, indexed by ->up.name */
static struct hmap all_ofproto_xpliant = HMAP_INITIALIZER(&all_ofproto_xpliant);

#define XP_VRF_DEFAULT_ID 0

typedef uint32_t OVS_BITWISE odp_port_t;
#define ODP_PORT_C(X) ((OVS_FORCE odp_port_t) (X))
#define ODPP_NONE  ODP_PORT_C(UINT32_MAX)
#define ODPP_LOCAL ODP_PORT_C(OVSP_LOCAL)

/* Xpliant OpenFlow Datapath Port */
struct xp_odp_port {
    struct hmap_node node;      /* Node in ofproto_xpliant's 'dp_ports'. */
    odp_port_t port_no;
    struct netdev *netdev;
    struct netdev_rxq **rxq;
    struct ovs_refcount ref_cnt;
};

struct port_dump_state {
    uint32_t bucket;
    uint32_t offset;
    bool ghost;

    struct ofproto_port port;
    bool has_port;
};


static void ofproto_xpliant_unixctl_init(void);
static int dp_port_get(const char *name, odp_port_t *portp);

static int do_add_port(struct ofproto_xpliant *ofproto, struct netdev *netdev,
                       const char *devname)
    OVS_REQ_WRLOCK(ofproto->dp_port_rwlock);

static int do_del_port(struct ofproto_xpliant *ofproto, odp_port_t port_no)
    OVS_REQ_WRLOCK(ofproto->dp_port_rwlock);

static bool dp_port_exists(const struct ofproto_xpliant *ofproto,
                           const char *devname);

static int dp_port_query_by_name(const struct ofproto_xpliant *ofproto,
                                 const char *devname,
                                 struct xp_odp_port **dp_portp);

static struct xp_odp_port *dp_lookup_port(const struct ofproto_xpliant *ofproto,
                                          odp_port_t port_no);

static void ofproto_xpliant_bundle_remove(struct ofport *port_);
static void bundle_destroy(struct bundle_xpliant *bundle);
static void bundle_run(struct bundle_xpliant *bundle);
static void bundle_wait(struct bundle_xpliant *bundle);
static void bundle_update(struct bundle_xpliant *bundle);

static struct ofport_xpliant *dp_port_to_ofport(
        const struct ofproto_xpliant *ofproto, odp_port_t odp_port);
static ofp_port_t odp_port_to_ofp_port(const struct ofproto_xpliant *ofproto,
                                       odp_port_t odp_port);

static inline struct ofport_xpliant *
ofport_xpliant_cast(const struct ofport *ofport)
{
    return ofport ? CONTAINER_OF(ofport, struct ofport_xpliant, up) : NULL;
}

/* ## ----------------- ## */
/* ## Factory Functions ## */
/* ## ----------------- ## */


static void
ofproto_xpliant_init(const struct shash *iface_hints OVS_UNUSED)
{
    VLOG_INFO("%s", __FUNCTION__);

    /* NOTE: We may assume that OpenFlow Datapath has not been configured yet,
     * since XDK is state-less by its nature and cannot retain
     * OpenFlow Datapath configuration between 'ofproto' restarts.
     * So, we ignore startup configuration provided through 'iface_hints' */

    /* Perform XDK initialization and start IPC server */
    ops_xp_dev_srv_init();
}

static void
ofproto_xpliant_enumerate_types(struct sset *types)
{
    /* DO NOT clear 'types' since other ofproto classes
     * might already have added names to it. See function prototype and
     * ofproto_enumerate_types(). */

    sset_add(types, "system");
    sset_add(types, "vrf");
}

static int
ofproto_xpliant_enumerate_names(const char *type, struct sset *names)
{
    struct ofproto_xpliant *ofproto;

    VLOG_DBG("%s: type %s", __FUNCTION__, type);

    /* Clears 'names' and adds the registered ofproto provider name (bridge name)
     * The caller must first initialize the sset. */

    sset_clear(names);

    HMAP_FOR_EACH (ofproto, all_ofproto_xpliant_node, &all_ofproto_xpliant) {
        if (!STR_EQ(type, ofproto->up.type)) {
            continue;
        }
        sset_add(names, ofproto->up.name);
    }

    return 0;
}

static int
ofproto_xpliant_del_datapath(const char *type, const char *name)
{
    VLOG_INFO("%s: type %s, name %s", __FUNCTION__, type, name);

    /* No specific actions required to clean-up Xpliant datapath */
    return 0;
}

static const char *
ofproto_xpliant_port_open_type(const char *datapath_type, const char *port_type)
{
    if (STR_EQ(port_type, OVSREC_INTERFACE_TYPE_INTERNAL) ||
        STR_EQ(port_type, OVSREC_INTERFACE_TYPE_VLANSUBINT) ||
        STR_EQ(port_type, OVSREC_INTERFACE_TYPE_LOOPBACK)) {
        return port_type;
    }
    return "system";
}


/* ## ------------------------ ## */
/* ## Top-Level type Functions ## */
/* ## ------------------------ ## */

static int
ofproto_xpliant_type_run(const char *type)
{
    /* TODO: Add periodic activity here if required
     * for ofproto_xpliant_class type */
    return 0;
}

static void
ofproto_xpliant_type_wait(const char *type)
{
    /* TODO */
}



/* ## --------------------------- ## */
/* ## Top-Level ofproto Functions ## */
/* ## --------------------------- ## */

static struct ofproto *
ofproto_xpliant_alloc(void)
{
    struct ofproto_xpliant *ofp = xmalloc(sizeof *ofp);

    VLOG_INFO("%s", __FUNCTION__);	

    return &ofp->up;
}

static void
ofproto_xpliant_dealloc(struct ofproto *ofproto_)
{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);

    VLOG_INFO("%s", __FUNCTION__);
	
    /* all ofproto_ fields are already cleaned up to this moment */
	
    free(ofproto);
}

static int
ofproto_xpliant_construct(struct ofproto *ofproto_)
{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);
    uint32_t dev_id = 0;
    int i;

    VLOG_INFO("%s: up.name %s, up.type %s\n", __FUNCTION__, ofproto->up.name,
              ofproto->up.type);

    /* Initialize XP device. */
    for (;;) {
        ofproto->xpdev = ops_xp_dev_by_id(dev_id);
        if (ofproto->xpdev) {
            if(!ops_xp_dev_is_initialized(ofproto->xpdev)) {
                ops_xp_dev_init(ofproto->xpdev);
            }
            break;
        }
        ops_xp_msleep(100);
    }

    memset(ofproto->sys_mac.ea, 0, ETH_ADDR_LEN);

    /* Initialize VRF instance */
    if (STR_EQ(ofproto_->type, "vrf")) {
        ofproto->bond_mgr = NULL;
        ofproto->vlan_mgr = ofproto->xpdev->vlan_mgr;
        ofproto->ml = ofproto->xpdev->ml;
        ofproto->has_bonded_bundles = false;
        ofproto->vrf = true;
        ofproto->vrf_id = XP_VRF_DEFAULT_ID;
        ofproto->l3_mgr = ops_xp_l3_mgr_create(ofproto->xpdev->id);
        if (!ofproto->l3_mgr) {
             VLOG_ERR("Unable to create L3 manager on VRF %u", ofproto->vrf_id);
             return EPERM;
        }
        /* Set default ECMP hash values */
        ops_xp_routing_ecmp_hash_set(ofproto, ofproto->l3_mgr->ecmp_hash, 1);

        hmap_init(&ofproto->bundles);
        hmap_insert(&all_ofproto_xpliant, &ofproto->all_ofproto_xpliant_node,
                    hash_string(ofproto->up.name, 0));
        sset_init(&ofproto->ports);
        sset_init(&ofproto->ghost_ports);
        ofproto->change_seq = 0;

        /* Initialize OpenFlow Datapath's ports */
        ovs_rwlock_init(&ofproto->dp_port_rwlock);
        hmap_init(&ofproto->dp_ports);
        ofproto->dp_port_seq = seq_create();

        ofproto_init_tables(ofproto_, N_TABLES);
        ofproto->up.tables[TBL_INTERNAL].flags = OFTABLE_HIDDEN | OFTABLE_READONLY;

        return 0;
    }

    /* Initialize 'system' ofproto */
    ofproto->vrf = false;
    ofproto->vrf_id = XP_VRF_DEFAULT_ID;
    ofproto->l3_mgr = NULL;
    ofproto->vlan_mgr = ofproto->xpdev->vlan_mgr;
    ofproto->ml = ofproto->xpdev->ml;

    hmap_init(&ofproto->bundles);

    sset_init(&ofproto->ports);
    sset_init(&ofproto->ghost_ports);
    ofproto->change_seq = 0;

    /* Initialize OpenFlow Datapath's ports */
    ovs_rwlock_init(&ofproto->dp_port_rwlock);
    hmap_init(&ofproto->dp_ports);
    ofproto->dp_port_seq = seq_create();

    hmap_insert(&all_ofproto_xpliant, &ofproto->all_ofproto_xpliant_node,
                hash_string(ofproto->up.name, 0));

    ofproto_init_tables(ofproto_, N_TABLES);
    ofproto->up.tables[TBL_INTERNAL].flags = OFTABLE_HIDDEN | OFTABLE_READONLY;

    ofproto_xpliant_unixctl_init();

    return 0;
}


static void
ofproto_xpliant_destruct(struct ofproto *ofproto_)
{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);
    struct xp_odp_port *port, *port_next;

    VLOG_INFO("%s: up.name %s", __FUNCTION__, ofproto->up.name);

    ops_xp_l3_mgr_unref(ofproto);

    hmap_remove(&all_ofproto_xpliant, &ofproto->all_ofproto_xpliant_node);

    hmap_destroy(&ofproto->bundles);

    sset_destroy(&ofproto->ports);
    sset_destroy(&ofproto->ghost_ports);

    seq_destroy(ofproto->dp_port_seq);

    ovs_rwlock_wrlock(&ofproto->dp_port_rwlock);
    HMAP_FOR_EACH_SAFE (port, port_next, node, &ofproto->dp_ports) {
        do_del_port(ofproto, port->port_no);
    }
    ovs_rwlock_unlock(&ofproto->dp_port_rwlock);

    ops_xp_dev_free(ofproto->xpdev);

    hmap_destroy(&ofproto->dp_ports);
    ovs_rwlock_destroy(&ofproto->dp_port_rwlock);
}

static int
ofproto_xpliant_run(struct ofproto *ofproto_)
{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);
    uint64_t new_seq = 0;

    new_seq = seq_read(connectivity_seq_get());
    if (ofproto->change_seq != new_seq) {
        ofproto->change_seq = new_seq;
    }

    if (!STR_EQ(ofproto_->type, "vrf")) {
        if (timer_expired(&ofproto->ml->mlearn_timer)) {
            ops_xp_mac_learning_on_mlearn_timer_expired(ofproto->ml);
        }

        if (timer_expired(&ofproto->ml->idle_timer)) {
            ops_xp_mac_learning_on_idle_timer_expired(ofproto->ml);
        }
    }

    return 0;
}

static void
ofproto_xpliant_wait(struct ofproto *ofproto_)
{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);
    /* TODO */
}

static void
ofproto_xpliant_type_get_memory_usage(const char *type, struct simap *usage)
{
    VLOG_INFO("%s: type %s", __FUNCTION__, type);
    /* TODO */
}

/* ## ---------------- ## */
/* ## ofport Functions ## */
/* ## ---------------- ## */

static void
ofproto_xpliant_set_tables_version(struct ofproto *ofproto,
                                   cls_version_t version)
{
    return;
}

static struct ofport *
ofproto_xpliant_port_alloc(void)
{
    struct ofport_xpliant *port = xmalloc(sizeof *port);
    VLOG_INFO("%s", __FUNCTION__);
    return &port->up;
}

static void
ofproto_xpliant_port_dealloc(struct ofport *port_)
{
    struct ofport_xpliant *port = ofport_xpliant_cast(port_);
    VLOG_INFO("%s", __FUNCTION__);
    free(port);
}

static int
ofproto_xpliant_port_construct(struct ofport *port_)
{
    struct ofport_xpliant *port = ofport_xpliant_cast(port_);
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(port->up.ofproto);
    const struct netdev *netdev = port->up.netdev;
    struct xp_odp_port *odp_port;
    int error = 0;

    VLOG_INFO("%s: up.name %s, netdev %s", __FUNCTION__,
              ofproto->up.name, netdev_get_name(netdev));

    if (!STR_EQ(netdev_get_type(netdev), OVSREC_INTERFACE_TYPE_SYSTEM)) {
        port->odp_port = ODPP_NONE;
        port->bundle = NULL;
        port->stp_port = NULL;
        port->stp_state = STP_DISABLED;
        port->may_enable = false;
        port->is_tunnel = false;
        return 0;
    }

    error = dp_port_query_by_name(ofproto, netdev_get_name(netdev), &odp_port);
    if (error) {
        VLOG_ERR("Failed to get datapath port number. %s", strerror(error));
        return error;
    }

    port->odp_port = odp_port->port_no;
    port->bundle = NULL;
    port->stp_port = NULL;
    port->stp_state = STP_DISABLED;
    port->may_enable = true;
    port->is_tunnel = false;

    if (netdev_get_tunnel_config(netdev)) {
        port->is_tunnel = true;
    } else {
        /* Sanity-check that a mapping doesn't already exist.  This
         * shouldn't happen for non-tunnel ports. */
        if (odp_port_to_ofp_port(ofproto, port->odp_port) != OFPP_NONE) {
            VLOG_ERR("port %s already has an OpenFlow port number",
                     odp_port->netdev->name);
            return EBUSY;
        }

        ovs_rwlock_wrlock(&ofproto->xpdev->odp_to_ofport_lock);
        hmap_insert(&ofproto->xpdev->odp_to_ofport_map, &port->odp_port_node,
                    hash_odp_port(port->odp_port));
        ovs_rwlock_unlock(&ofproto->xpdev->odp_to_ofport_lock);
    }

    return error;
}

static void
ofproto_xpliant_port_destruct(struct ofport *port_)
{
    struct ofport_xpliant *port = ofport_xpliant_cast(port_);
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(port->up.ofproto);
    const char *devname = netdev_get_name(port->up.netdev);

    VLOG_INFO("%s: up.name %s, netdev %s",
              __FUNCTION__, ofproto->up.name, devname);

    if (port->odp_port != ODPP_NONE && !port->is_tunnel) {
        ovs_rwlock_wrlock(&ofproto->xpdev->odp_to_ofport_lock);
        hmap_remove(&ofproto->xpdev->odp_to_ofport_map, &port->odp_port_node);
        ovs_rwlock_unlock(&ofproto->xpdev->odp_to_ofport_lock);
    }

    sset_find_and_delete(&ofproto->ports, devname);
    sset_find_and_delete(&ofproto->ghost_ports, devname);
    ofproto_xpliant_bundle_remove(port_);
}

static void
ofproto_xpliant_port_modified(struct ofport *port_)
{
    struct ofport_xpliant *port = ofport_xpliant_cast(port_);
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(port->up.ofproto);
    struct xp_odp_port *odp_port;

    VLOG_INFO("%s: up.name %s", __FUNCTION__, ofproto->up.name);

    ovs_rwlock_wrlock(&ofproto->dp_port_rwlock);

    odp_port = dp_lookup_port(ofproto, port->odp_port);
    if (odp_port && odp_port->netdev != port->up.netdev) {
        netdev_close(odp_port->netdev);
        odp_port->netdev = netdev_ref(port->up.netdev);
    }

    ovs_rwlock_unlock(&ofproto->dp_port_rwlock);
}

static void
ofproto_xpliant_port_reconfigured(struct ofport *port_,
                                  enum ofputil_port_config old_config)
{
    struct ofport_xpliant *port = ofport_xpliant_cast(port_);
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(port->up.ofproto);
    enum ofputil_port_config changed = old_config ^ port->up.pp.config;

    VLOG_INFO("%s: up.name %s", __FUNCTION__, ofproto->up.name);

    if (changed & (OFPUTIL_PC_NO_RECV | OFPUTIL_PC_NO_RECV_STP |
                   OFPUTIL_PC_NO_FWD | OFPUTIL_PC_NO_FLOOD |
                   OFPUTIL_PC_NO_PACKET_IN)) {
        if (changed & OFPUTIL_PC_NO_FLOOD && port->bundle) {
            bundle_update(port->bundle);
        }
    }
}

static struct ofport_xpliant *
get_ofp_port(const struct ofproto_xpliant *ofproto, ofp_port_t ofp_port)
{
    struct ofport *ofport = ofproto_get_port(&ofproto->up, ofp_port);
    return ofport ? ofport_xpliant_cast(ofport) : NULL;
}

static struct ofport_xpliant *
dp_port_to_ofport(const struct ofproto_xpliant *ofproto, odp_port_t odp_port)
{
    struct ofport_xpliant *port;

    ovs_rwlock_rdlock(&ofproto->xpdev->odp_to_ofport_lock);
    HMAP_FOR_EACH_IN_BUCKET (port, odp_port_node, hash_odp_port(odp_port),
                             &ofproto->xpdev->odp_to_ofport_map) {
        if (port->odp_port == odp_port) {
            ovs_rwlock_unlock(&ofproto->xpdev->odp_to_ofport_lock);
            return port;
        }
    }

    ovs_rwlock_unlock(&ofproto->xpdev->odp_to_ofport_lock);

    return NULL;
}

static ofp_port_t
odp_port_to_ofp_port(const struct ofproto_xpliant *ofproto, odp_port_t odp_port)
{
    struct ofport_xpliant *port;

    port = dp_port_to_ofport(ofproto, odp_port);
    if (port && &ofproto->up == port->up.ofproto) {
        return port->up.ofp_port;
    } else {
        return OFPP_NONE;
    }
}

static void
ofproto_port_from_dp_port(struct ofproto_xpliant *ofproto,
                          struct ofproto_port *ofproto_port,
                          struct xp_odp_port *dp_port)
{
    ofproto_port->name = xstrdup(netdev_get_name(dp_port->netdev));
    ofproto_port->type = xstrdup(netdev_get_type(dp_port->netdev));
    ofproto_port->ofp_port = odp_port_to_ofp_port(ofproto, dp_port->port_no);
}

static int
dp_port_query_by_name(const struct ofproto_xpliant *ofproto,
        const char *devname, struct xp_odp_port **dp_portp)
{
    struct xp_odp_port *port;

    HMAP_FOR_EACH (port, node, &ofproto->dp_ports) {
        if (STR_EQ(netdev_get_name(port->netdev), devname)) {
            *dp_portp = port;
            return 0;
        }
    }
    return ENOENT;
}

/* Checks if port named 'devname' already exists in Xpliant datapath.
 * If so, returns true; otherwise, returns false. */
static bool
dp_port_exists(const struct ofproto_xpliant *ofproto, const char *devname)
{
    struct xp_odp_port *dp_port;
    return !dp_port_query_by_name(ofproto, devname, &dp_port);
}

static struct xp_odp_port *
dp_lookup_port(const struct ofproto_xpliant *ofproto, odp_port_t port_no)
    OVS_REQ_RDLOCK(ofproto->dp_port_rwlock)
{
    struct xp_odp_port *port;

    HMAP_FOR_EACH_IN_BUCKET (port, node, hash_int(odp_to_u32(port_no), 0),
                             &ofproto->dp_ports) {
        if (port->port_no == port_no) {
            return port;
        }
    }
    return NULL;
}

static int
ofproto_xpliant_port_query_by_name(const struct ofproto *ofproto_,
                                   const char *devname,
                                   struct ofproto_port *ofproto_port)
{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);
    struct xp_odp_port *dp_port;
    int error;

    VLOG_DBG("%s: up.name %s, devname %s",
              __FUNCTION__, ofproto->up.name, devname);

    if (sset_contains(&ofproto->ghost_ports, devname)) {
        const char *type = netdev_get_type_from_name(devname);

        /* We may be called before ofproto->up.port_by_name is populated with
         * the appropriate ofport. For this reason, we must get the name and
         * type from the netdev layer directly. */
        if (type) {
            const struct ofport *ofport;

            ofport = shash_find_data(&ofproto->up.port_by_name, devname);
            ofproto_port->ofp_port = ofport ? ofport->ofp_port : OFPP_NONE;
            ofproto_port->name = xstrdup(devname);
            ofproto_port->type = xstrdup(type);
            return 0;
        }
        return ENODEV;

    }

    if (!sset_contains(&ofproto->ports, devname)) {
        return ENODEV;
    }

    ovs_rwlock_rdlock(&ofproto->dp_port_rwlock);
    error = dp_port_query_by_name(ofproto, devname, &dp_port);
    if (!error) {
        ofproto_port_from_dp_port(ofproto, ofproto_port, dp_port);
    }
    ovs_rwlock_unlock(&ofproto->dp_port_rwlock);

    return error;
}

struct ofport_xpliant *
ofport_xpliant_query_by_name(const struct ofproto_xpliant *ofproto,
                             const char *name)
{
    const struct ofport *ofport;

    ovs_assert(ofproto);
    ovs_assert(name);

    ofport = shash_find_data(&ofproto->up.port_by_name, name);

    if (ofport) {
        return ofport_xpliant_cast(ofport);
    }

    return NULL;
}

static int
dp_port_get(const char *name, odp_port_t *portp)
{
    struct netdev *netdev_;
    struct netdev_xpliant *netdev;
    uint32_t port_num;

    ovs_assert(name);
    ovs_assert(portp);

    netdev_ = netdev_from_name(name);
    if (!netdev_) {
        return EINVAL;
    }

    netdev = netdev_xpliant_cast(netdev_);
    if (netdev->port_num == CPU_PORT) {
        port_num = ODPP_LOCAL;
    } else {
        port_num = netdev->port_num + 1;
    }

    *portp = ODP_PORT_C(port_num);

    netdev_close(netdev_);

    return 0;
}

static void
port_unref(struct xp_odp_port *port)
{
    if (port && ovs_refcount_unref(&port->ref_cnt) == 1) {
        if (port->rxq) {
            int n_rxq = netdev_n_rxq(port->netdev);
            int i;

            for (i = 0; i < n_rxq; i++) {
                netdev_rxq_close(port->rxq[i]);
            }

            free(port->rxq);
        }

        netdev_close(port->netdev);

        free(port);
    }
}

static int
do_add_port(struct ofproto_xpliant *ofproto, struct netdev *netdev,
            const char *devname)
    OVS_REQ_WRLOCK(ofproto->dp_port_rwlock)
{
    struct xp_odp_port *port;
    odp_port_t port_no;
    XP_STATUS status = XP_NO_ERR;
    int error;
    int i;

    /* Choose datapath port number */
    error = dp_port_get(devname, &port_no);
    if (error) {
        VLOG_ERR("%s: invalid device name", devname);
        return error;
    }

    port = xzalloc(sizeof *port);
    port->port_no = port_no;
    port->netdev = netdev_ref(netdev);
    port->rxq = NULL;

    ovs_refcount_init(&port->ref_cnt);

    hmap_insert(&ofproto->dp_ports, &port->node,
                hash_int(odp_to_u32(port_no), 0));
    seq_change(ofproto->dp_port_seq);
    return 0;
}

static int
ofproto_xpliant_port_add(struct ofproto *ofproto_, struct netdev *netdev)
{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);
    const char *devname = netdev_get_name(netdev);
    int err = 0;

    VLOG_INFO("%s: up.name %s, devname %s", __FUNCTION__,
              ofproto->up.name, devname);

    if (!STR_EQ(netdev_get_type(netdev), OVSREC_INTERFACE_TYPE_SYSTEM)) {
        sset_add(&ofproto->ghost_ports, devname);
        return 0;
    }

    if (!dp_port_exists(ofproto, devname)) {
        ovs_rwlock_wrlock(&ofproto->dp_port_rwlock);
        err = do_add_port(ofproto, netdev, devname);
        ovs_rwlock_unlock(&ofproto->dp_port_rwlock);
        if (err) {
            return err;
        }
    }

    sset_add(&ofproto->ports, devname);
    return 0;
}

static int
do_del_port(struct ofproto_xpliant *ofproto, odp_port_t port_no)
    OVS_REQ_WRLOCK(ofproto->dp_port_rwlock)
{
    struct xp_odp_port *port;
    XP_STATUS status = XP_NO_ERR;

    port = dp_lookup_port(ofproto, port_no);
    if (!port) {
        return EINVAL;
    }

    hmap_remove(&ofproto->dp_ports, &port->node);
    seq_change(ofproto->dp_port_seq);

    port_unref(port);

    return 0;
}

static int
ofproto_xpliant_port_del(struct ofproto *ofproto_, ofp_port_t ofp_port)
{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);
    struct ofport_xpliant *ofport = get_ofp_port(ofproto, ofp_port);
    int error = 0;

    if (!ofport) {
        return 0;
    }

    VLOG_INFO("%s: ofport %d up.name %s", __FUNCTION__,
              ofp_port, ofproto->up.name);

    sset_find_and_delete(&ofproto->ghost_ports,
                         netdev_get_name(ofport->up.netdev));

    if (STR_EQ(netdev_get_type(ofport->up.netdev), OVSREC_INTERFACE_TYPE_SYSTEM)) {
        ovs_rwlock_wrlock(&ofproto->dp_port_rwlock);
        error = do_del_port(ofproto, ofport->odp_port);
        ovs_rwlock_unlock(&ofproto->dp_port_rwlock);
        if (!error) {
            /* The caller is going to close ofport->up.netdev.  If this is a
             * bonded port, then the bond is using that netdev, so remove it
             * from the bond.  The client will need to reconfigure everything
             * after deleting ports, so then the slave will get re-added. */
            ofproto_xpliant_bundle_remove(&ofport->up);
        }
    }

    return error;
}

static int
ofproto_xpliant_port_get_stats(const struct ofport *ofport_, struct netdev_stats *stats)
{
    struct ofport_xpliant *ofport = ofport_xpliant_cast(ofport_);
    int error = 0;

    VLOG_INFO("%s", __FUNCTION__);

    error = netdev_get_stats(ofport->up.netdev, stats);

    return error;
}

static int
ofproto_xpliant_port_dump_start(const struct ofproto *ofproto_ OVS_UNUSED, void **statep)
{
    VLOG_DBG("ofproto_xpliant_port_dump_start");
    *statep = xzalloc(sizeof(struct port_dump_state));
    return 0;
}

static int
ofproto_xpliant_port_dump_next(const struct ofproto *ofproto_, void *state_,
                               struct ofproto_port *port)
{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);
    struct port_dump_state *state = state_;
    const struct sset *sset;
    struct sset_node *node;

    VLOG_DBG("%s", __FUNCTION__);

    if (state->has_port) {
        ofproto_port_destroy(&state->port);
        state->has_port = false;
    }
    sset = state->ghost ? &ofproto->ghost_ports : &ofproto->ports;
    while ((node = sset_at_position(sset, &state->bucket, &state->offset))) {
        int error;

        error = ofproto_xpliant_port_query_by_name(ofproto_,
                node->name, &state->port);
        if (!error) {
            *port = state->port;
            state->has_port = true;
            return 0;
        } else if (error != ENODEV) {
            return error;
        }
    }

    if (!state->ghost) {
        state->ghost = true;
        state->bucket = 0;
        state->offset = 0;
        return ofproto_xpliant_port_dump_next(ofproto_, state_, port);
    }

    return EOF;
}

static int
ofproto_xpliant_port_dump_done(const struct ofproto *ofproto_ OVS_UNUSED, void *state_)
{
    struct port_dump_state *state = state_;

    VLOG_DBG("%s", __FUNCTION__);

    if (state->has_port) {
        ofproto_port_destroy(&state->port);
    }
    free(state);
    return 0;
}

static int
ofproto_xpliant_port_poll(const struct ofproto *ofproto_, char **devnamep)
{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);

	/* TODO */

    return 0;
}

static void
ofproto_xpliant_port_poll_wait(const struct ofproto *ofproto_)
{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);

	/* TODO */
}

/* ## ------------------------- ## */
/* ## OFPP_NORMAL configuration ## */
/* ## ------------------------- ## */

/* Bundles. */

/* Expires all MAC learning entries associated with 'bundle' and forces its
 * ofproto to revalidate every flow. */
static void
bundle_flush_macs(struct bundle_xpliant *bundle)
{
    struct xp_mac_learning *ml = bundle->ofproto->ml;

    VLOG_INFO("bundle_flush_macs");

    ovs_rwlock_wrlock(&ml->rwlock);
    ops_xp_mac_learning_flush_intfId(ml, bundle->intfId, false);
    ovs_rwlock_unlock(&ml->rwlock);
}

struct bundle_xpliant *
bundle_lookup(const struct ofproto_xpliant *ofproto, void *aux)
{
    struct bundle_xpliant *bundle;

    VLOG_DBG("bundle_lookup: ofproto.%s",
             ofproto ? ofproto->up.name : "_");

    HMAP_FOR_EACH_IN_BUCKET (bundle, hmap_node, hash_pointer(aux, 0),
                             &ofproto->bundles) {
        if (bundle->aux == aux) {
            return bundle;
        }
    }
    return NULL;
}

static struct bundle_xpliant *
bundle_lookup_by_intf_id(const struct ofproto_xpliant *ofproto,
                         xpsInterfaceId_t intfId)
{
    struct bundle_xpliant *bundle;

    HMAP_FOR_EACH (bundle, hmap_node, &ofproto->bundles) {
        if (bundle->intfId == intfId) {
            return bundle;
        }
    }

    return NULL;
}

static void
bundle_update(struct bundle_xpliant *bundle)
{
    struct ofport_xpliant *port;

    VLOG_INFO("bundle_update");

    bundle->floodable = true;
    LIST_FOR_EACH (port, bundle_node, &bundle->ports) {
        if ((port->up.pp.config & OFPUTIL_PC_NO_FLOOD)
              || !stp_forward_in_state(port->stp_state)) {
            bundle->floodable = false;
            break;
        }
    }
}

xpsPort_t
ops_xp_get_ofport_number(const struct ofport_xpliant *port)
{
    ovs_assert(port != NULL);
    return netdev_xpliant_cast(port->up.netdev)->port_num;
}

xpsInterfaceId_t
ops_xp_get_ofport_intf_id(const struct ofport_xpliant *port)
{
    ovs_assert(port != NULL);
    return netdev_xpliant_cast(port->up.netdev)->ifId;
}

static void
bundle_run(struct bundle_xpliant *bundle)
{
}

static void
bundle_wait(struct bundle_xpliant *bundle)
{
}

static void
bundle_del_port(struct ofport_xpliant *port)
{
    struct bundle_xpliant *bundle = port->bundle;
    int rc = 0;

    VLOG_INFO("bundle_del_port: bundle.%s, netdev.%s",
              bundle ? bundle->name : "_",
              port ? netdev_get_name(port->up.netdev) : "_");

    list_remove(&port->bundle_node);
    port->bundle = NULL;
    bundle->ports_updated = true;

    bundle_update(bundle);

    /* As the port is no longer member of any VLAN need to add it to the default
     * one. This will allow it to trap LACP frames in case it becomes member of
     * a dynamic LAG. */
    if (STR_EQ(netdev_get_type(port->up.netdev), OVSREC_INTERFACE_TYPE_SYSTEM)) {

        struct netdev_xpliant *netdev = netdev_xpliant_cast(port->up.netdev);

        if (!ops_xp_vlan_port_is_member(netdev->xpdev->vlan_mgr,
                                        XP_DEFAULT_VLAN_ID,
                                        netdev->ifId)) {
            rc = ops_xp_vlan_member_add(netdev->xpdev->vlan_mgr,
                                        XP_DEFAULT_VLAN_ID, netdev->ifId,
                                        XP_L2_ENCAP_DOT1Q_UNTAGGED, 0);
            if (rc) {
                VLOG_ERR("%s: Could not add interface: %u to default vlan: %d "
                         "Err=%d", __FUNCTION__, netdev->ifId,
                         XP_DEFAULT_VLAN_ID, rc);
            }
        } else {
            rc = ops_xp_vlan_member_set_tagging(netdev->xpdev->vlan_mgr,
                                                XP_DEFAULT_VLAN_ID,
                                                netdev->ifId,
                                                false, 0);
            if (rc) {
                VLOG_ERR("%s: Could set interface: %u tagging "
                         "on default vlan: %d Err=%d", __FUNCTION__,
                         netdev->ifId, XP_DEFAULT_VLAN_ID, rc);
            }

            ops_xp_port_default_vlan_set(netdev->xpdev->id, netdev->port_num,
                                         XP_DEFAULT_VLAN_ID);
        }
    }
}

static bool
bundle_add_port(struct bundle_xpliant *bundle, ofp_port_t ofp_port)
{
    struct ofport_xpliant *port;
    int rc = 0;

    VLOG_INFO("bundle_add_port: bundle.%s, ofp_port.%u",
              bundle ? bundle->name : "_", ofp_port);

    port = get_ofp_port(bundle->ofproto, ofp_port);
    if (!port) {
        return false;
    }

    if (port->bundle != bundle) {
        /* If port participates in another bundle,
         * then remove it from that bundle */
        if (port->bundle) {
            ofproto_xpliant_bundle_remove(&port->up);
        }

        port->bundle = bundle;
        list_push_back(&bundle->ports, &port->bundle_node);
        if ((port->up.pp.config & OFPUTIL_PC_NO_FLOOD)
            || !stp_forward_in_state(port->stp_state)) {
            bundle->floodable = false;
        }

        /* As the port is going to be a part of the bundle need to remove it from
         * the default VLAN as membership will be applied based on bundle config */
        if (STR_EQ(netdev_get_type(port->up.netdev), OVSREC_INTERFACE_TYPE_SYSTEM)) {

            struct netdev_xpliant *netdev = netdev_xpliant_cast(port->up.netdev);

            rc = ops_xp_vlan_member_remove(netdev->xpdev->vlan_mgr,
                                           XP_DEFAULT_VLAN_ID,
                                           netdev->ifId);
            if (rc) {
                VLOG_ERR("%s: Could not remove interface: %u from default vlan: %d "
                         "Err=%d", __FUNCTION__, netdev->ifId,
                         XP_DEFAULT_VLAN_ID, rc);
            }
        }

        bundle->ports_updated = true;
    }

    return true;
}

int
bundle_update_vlan_config(struct ofproto_xpliant *ofproto,
                          struct bundle_xpliant *bundle,
                          const struct ofproto_bundle_settings *s)
{
    struct ofport_xpliant *port = NULL;
    xpsVlan_t vlan = 0;
    xpsVlan_t old_vlan = 0;
    enum port_vlan_mode old_vlan_mode;
    unsigned long *trunks = NULL;
    xpsPortFrameType_e frame_type = FRAMETYPE_ALL;
    int retVal = 0;

    ovs_assert(ofproto);
    ovs_assert(bundle);
    ovs_assert(s);

    old_vlan = (bundle->vlan != -1) ? bundle->vlan : XP_DEFAULT_VLAN_ID;
    old_vlan_mode = bundle->vlan_mode;

    /* NOTE: "bundle" holds previous VLAN configuration (if any).
     * "s" holds current desired VLAN configuration. */

    /* Set VLAN tagging mode */
    if ((s->vlan_mode != bundle->vlan_mode) ||
        (s->use_priority_tags != bundle->use_priority_tags)) {

        bundle->vlan_mode = s->vlan_mode;
        bundle->use_priority_tags = s->use_priority_tags;
    }

    /* Set VLAN tag. */
    vlan = ((s->vlan_mode == PORT_VLAN_TRUNK) ? XP_DEFAULT_VLAN_ID :
            (ops_xp_is_vlan_id_valid((xpsVlan_t)s->vlan) ?
            s->vlan : XP_DEFAULT_VLAN_ID));

    if (vlan != bundle->vlan) {
        bundle->vlan = vlan;
    }

    VLOG_INFO("%s(%d) bundle->vlan %d, "
             "s->vlan_mode: %d",
             __FUNCTION__, __LINE__, bundle->vlan, s->vlan_mode);

    /* Get trunked VLANs. */
    switch (s->vlan_mode) {
    case PORT_VLAN_ACCESS:
        trunks = bitmap_allocate(XP_VLAN_MAX_COUNT);
        bitmap_set1(trunks, vlan);
        frame_type = FRAMETYPE_UN_PRI;
        break;

    case PORT_VLAN_TRUNK:
        if (!s->trunks) {
            trunks = bitmap_allocate(XP_VLAN_MAX_COUNT);
        } else {
            trunks = s->trunks;
        }

        /*VLOG_DBG("%s(%d) bundle->vlan %d",
                 __FUNCTION__, __LINE__, bundle->vlan);*/
        break;

    case PORT_VLAN_NATIVE_UNTAGGED:
    case PORT_VLAN_NATIVE_TAGGED:
        if (s->trunks) {
            trunks = bitmap_clone(s->trunks, XP_VLAN_MAX_COUNT);
        } else {
            trunks = bitmap_allocate(XP_VLAN_MAX_COUNT);
        }
        /* Force trunking the native VLAN */
        bitmap_set1(trunks, vlan);
        break;

    default:
        OVS_NOT_REACHED();
    }

    /* Handle a case when the same vlan was and is in trunks list right now
     * and only tagged/untagged port mode changed for it. If this is so just
     * remove it from bundle->trunks and port from it on hardware
     * so xp_vlan_bitmap_xor will retun correct value. */
    if ((bundle->trunks != NULL) &&
        ((old_vlan != vlan) || (old_vlan_mode != s->vlan_mode))) 
    {
        if (bitmap_is_set(bundle->trunks, vlan)) {
            bitmap_set0(bundle->trunks, vlan);

            if (ops_xp_vlan_is_existing(ofproto->vlan_mgr, vlan)) {
                retVal = ops_xp_vlan_member_remove(ofproto->vlan_mgr, vlan,
                                                   bundle->intfId);
                if (retVal) {
                    return retVal;
                }
            }

        }

        if (bitmap_is_set(trunks, old_vlan)) {
            bitmap_set0(bundle->trunks, old_vlan);

            if (ops_xp_vlan_is_existing(ofproto->vlan_mgr, old_vlan)) {
                retVal = ops_xp_vlan_member_remove(ofproto->vlan_mgr, old_vlan,
                                                   bundle->intfId);
                if (retVal) {
                    return retVal;
                }
            }
        }
    }

    if (!ops_xp_vlan_bitmap_equal(trunks, bundle->trunks) ||
        (bundle->is_lag && bundle->ports_updated)) {

        unsigned long *trunks_diff = NULL;
        int vid = 0;

        /* Create bitmap with changes in VLAN participation */
        trunks_diff = ops_xp_vlan_bitmap_xor(trunks, bundle->trunks);

        /* Update VLAN bitmap in the bundle */
        bitmap_free(bundle->trunks);

        if (trunks == s->trunks) {
            bundle->trunks = ops_xp_vlan_bitmap_clone(trunks);
        } else {
            bundle->trunks = trunks;
            trunks = NULL;
        }

        /*VLOG_DBG("%s(%d) bundle->vlan %d, bundle->trunks: %x",
                 __FUNCTION__, __LINE__, bundle->vlan, *bundle->trunks);*/

        retVal = xpsPortSetField(ofproto->xpdev->id, bundle->intfId,
                                 XPS_PORT_ACCEPTED_FRAME_TYPE, frame_type);
        if (retVal != XP_NO_ERR) {
            VLOG_ERR("%s: Could not set frametype in interface %d. "
                     "Error code: %d\n", 
                     __FUNCTION__, bundle->intfId, retVal);
            return EPERM;
        }

        /* Update PVID */
        LIST_FOR_EACH (port, bundle_node, &bundle->ports) {
            VLOG_INFO("%s: Set default VLAN %u on Port %u",
                      __FUNCTION__, bundle->vlan,
                      ops_xp_get_ofport_number(port));
            retVal = ops_xp_port_default_vlan_set(ofproto->xpdev->id,
                                                  ops_xp_get_ofport_number(port),
                                                  bundle->vlan);
            if (retVal) {
                return retVal;
            }
        }

        /* Update VLAN participation of the bundle */
        if (trunks_diff) {

            VLAN_BITMAP_FOR_EACH_1(vid, trunks_diff) {

                if (ops_xp_vlan_is_existing(ofproto->vlan_mgr, vid)) {
                    if (bitmap_is_set(bundle->trunks, vid)) {

                        xpsL2EncapType_e encapType = ((bundle->vlan != vid) ||
                            (bundle->vlan_mode == PORT_VLAN_NATIVE_TAGGED))
                                                  ? XP_L2_ENCAP_DOT1Q_TAGGED
                                                  : XP_L2_ENCAP_DOT1Q_UNTAGGED;


                        VLOG_INFO("%s(%d) bundle->intfId %d, bundle->vlan %d, "
                                  "bundle->trunks: %x, vid: %d, tagged: %d",
                                  __FUNCTION__, __LINE__,
                                  bundle->intfId, bundle->vlan, *bundle->trunks,
                                  vid, encapType);

                        retVal = ops_xp_vlan_member_add(ofproto->vlan_mgr, vid,
                                                        bundle->intfId,
                                                        encapType, 0);
                        if (retVal) {
                            return retVal;
                        }
                    } else {
                        retVal = ops_xp_vlan_member_remove(ofproto->vlan_mgr,
                                                           vid,
                                                           bundle->intfId);
                        if (retVal) {
                            return retVal;
                        }
                    }
                }
            } /* VLAN_BITMAP_FOR_EACH_1(vid, trunks_diff) */

            bitmap_free(trunks_diff);
        } /* if (trunks_diff) */
    } /* if (!vlan_bitmap_equal(trunks, bundle->trunks) || ...) */

    if (trunks != s->trunks) {
        bitmap_free(trunks);
    }

    return 0;
}

/* Creates/removes LAG on HW if needed. */
static int
bundle_create_remove_hw_lag(struct ofproto_xpliant *ofproto,
                            struct bundle_xpliant *bundle,
                            const struct ofproto_bundle_settings *s)
{
    int ret_val;

    ovs_assert(ofproto);
    ovs_assert(bundle);
    ovs_assert(s);

    if ((bundle->intfId == XPS_INTF_INVALID_ID) &&
        (s->hw_bond_should_exist || s->bond_handle_alloc_only)) {

        /* Create a HW LAG if there is more than one member present
           in the bundle or if requested by upper layer. */
        ret_val = ops_xp_lag_create(bundle->name, &bundle->intfId);
        if (ret_val) {
            return ret_val;
        }

        bundle->is_lag = true;

    } else if ((bundle->intfId != XPS_INTF_INVALID_ID) && bundle->is_lag &&
               !s->hw_bond_should_exist) {

        /* LAG should not exist in HW any more. */
        ops_xp_lag_destroy(ofproto->xpdev->id, bundle->name, bundle->intfId);

        bundle->intfId = XPS_INTF_INVALID_ID;
        bundle->is_lag = false;
    }

    return 0;
}

/* Updates configuration of LAG on HW. */
static int
bundle_update_hw_lag_config(struct ofproto_xpliant *ofproto,
                            struct bundle_xpliant *bundle,
                            const struct ofproto_bundle_settings *s)
{
    int ret_val;
    xpsLagPortIntfList_t if_list;
    const struct ofport_xpliant *port;
    uint8_t i;

    ovs_assert(ofproto);
    ovs_assert(bundle);
    ovs_assert(s);

    if_list.size = 0;

    for (i = 0; i < s->n_slaves; i++) {

        port = get_ofp_port(ofproto, s->slaves[i]);
        if (port) {
            /* Check if there are some slaves other than "xpliant" in the list
             * and return error if so. */
            if (!STR_EQ(netdev_get_type(port->up.netdev), OVSREC_INTERFACE_TYPE_SYSTEM)) {
                VLOG_ERR("%s, Could not add port to bundle. Only \"xpliant\" "
                         "netdev\'s are acceptable as bundle slaves",
                         __FUNCTION__);
                return EINVAL;
            }

            if_list.portIntf[if_list.size] = ops_xp_get_ofport_intf_id(port);
            if_list.size++;
        }
    }

    /* Attach ports to the LAG. */
    ret_val = ops_xp_lag_attach_ports(ofproto->xpdev->id, bundle->name,
                                      bundle->intfId, &if_list);
    if (ret_val) {
        VLOG_ERR("%s: Could not atatch ports to LAG %s. "
                 "Error code: %d\n", __FUNCTION__, bundle->name, ret_val);
        return ret_val;
    }

    return 0;
}

static int
bundle_update_single_slave_config(struct ofproto_xpliant *ofproto,
                                  struct bundle_xpliant *bundle,
                                  const struct ofproto_bundle_settings *s)
{
    int ret_val = 0;
    struct ofport_xpliant *port = NULL;

    ovs_assert(ofproto);
    ovs_assert(bundle);
    ovs_assert(s);
    port = CONTAINER_OF(list_front(&bundle->ports),
                        struct ofport_xpliant, bundle_node);

    if (!STR_EQ(bundle->name, netdev_get_name(port->up.netdev))) {
        /* This is something different then a physical port or a tunnel.
         * Nothing to do more here. */
        return 0;
    }

    if (!is_xpliant_class(netdev_get_class(port->up.netdev))) {

        VLOG_ERR("%s, Could not create bundle. Only \"xpliant\" and"
                 "\"xpliant_<tunnel-type>\" netdev\'s "
                 "are acceptable as bundle slaves", __FUNCTION__);
        bundle_destroy(bundle);
        return EINVAL;
    }

    bundle->intfId = ops_xp_get_ofport_intf_id(port);

    VLOG_INFO("%s, bundle: %s, bundle->ifId: %d",
              __FUNCTION__, bundle->name, bundle->intfId);

    return 0;
}

/* Host Functions */
/* Function to unconfigure and free all port ip's */
static void
port_unconfigure_ips(struct bundle_xpliant *bundle)
{
    struct ofproto_xpliant *ofproto;
    bool is_ipv6 = false;
    xp_net_addr_t *addr, *next;

    ofproto = bundle->ofproto;

    /* Unconfigure primary ipv4 address and free */
    if (bundle->ip4addr) {
        ops_xp_routing_delete_host_entry(ofproto, &bundle->ip4addr->id);
        free(bundle->ip4addr->address);
        free(bundle->ip4addr);
    }

    /* Unconfigure secondary ipv4 address and free the hash */
    HMAP_FOR_EACH_SAFE (addr, next, addr_node, &bundle->secondary_ip4addr) {
        ops_xp_routing_delete_host_entry(ofproto, &addr->id);
        hmap_remove(&bundle->secondary_ip4addr, &addr->addr_node);
        free(addr->address);
        free(addr);
    }
    hmap_destroy(&bundle->secondary_ip4addr);

    is_ipv6 = true;

    /* Unconfigure primary ipv6 address and free */
    if (bundle->ip6addr) {
        ops_xp_routing_delete_host_entry(ofproto, &bundle->ip6addr->id);
        free(bundle->ip6addr->address);
        free(bundle->ip6addr);
    }

    /* Unconfigure secondary ipv6 address and free the hash */
    HMAP_FOR_EACH_SAFE (addr, next, addr_node, &bundle->secondary_ip6addr) {
        ops_xp_routing_delete_host_entry(ofproto, &bundle->ip6addr->id);
        hmap_remove(&bundle->secondary_ip6addr, &addr->addr_node);
        free(addr->address);
        free(addr);
    }
    hmap_destroy(&bundle->secondary_ip6addr);

} /* port_unconfigure_ips */

/*
 * Function to find if the ipv4 secondary address already exist in the hash.
 */
static struct net_address *
port_ip4_addr_find(struct bundle_xpliant *bundle, const char *address)
{
    xp_net_addr_t *addr;

    HMAP_FOR_EACH_WITH_HASH (addr, addr_node, hash_string(address, 0),
                             &bundle->secondary_ip4addr) {
        if (!strcmp(addr->address, address)) {
            return addr;
        }
    }

    return NULL;
} /* port_ip4_addr_find */

/*
 * Function to find if the ipv6 secondary address already exist in the hash.
 */
static struct net_address *
port_ip6_addr_find(struct bundle_xpliant *bundle, const char *address)
{
    xp_net_addr_t *addr;

    HMAP_FOR_EACH_WITH_HASH (addr, addr_node, hash_string(address, 0),
                             &bundle->secondary_ip6addr) {
        if (!strcmp(addr->address, address)) {
            return addr;
        }
    }

    return NULL;
} /* port_ip6_addr_find */

/*
 * Function to check for changes in secondary ipv4 configuration of a
 * given port
 */
static void
port_config_secondary_ipv4_addr(struct ofproto_xpliant *ofproto,
                                struct bundle_xpliant *bundle,
                                const struct ofproto_bundle_settings *s)
{
    struct shash new_ip_list;
    xp_net_addr_t *addr, *next;
    struct shash_node *addr_node;
    int i;
    bool is_ipv6 = false;

    shash_init(&new_ip_list);

    /* Create hash of the current secondary ip's */
    for (i = 0; i < s->n_ip4_address_secondary; i++) {
       if(!shash_add_once(&new_ip_list, s->ip4_address_secondary[i],
                           s->ip4_address_secondary[i])) {
            VLOG_WARN("Duplicate address in secondary list %s\n",
                      s->ip4_address_secondary[i]);
        }
    }

    /* Compare current and old to delete any obselete one's */
    HMAP_FOR_EACH_SAFE (addr, next, addr_node, &bundle->secondary_ip4addr) {
        if (!shash_find_data(&new_ip_list, addr->address)) {
            hmap_remove(&bundle->secondary_ip4addr, &addr->addr_node);
            ops_xp_routing_delete_host_entry(ofproto, &addr->id);
            free(addr->address);
            free(addr);
        }
    }

    /* Add the newly added addresses to the list */
    SHASH_FOR_EACH (addr_node, &new_ip_list) {
        xp_net_addr_t *addr;
        const char *address = addr_node->data;
        if (!port_ip4_addr_find(bundle, address)) {
            /*
             * Add the new address to the list
             */
            addr = xzalloc(sizeof *addr);
            addr->address = xstrdup(address);
            hmap_insert(&bundle->secondary_ip4addr, &addr->addr_node,
                        hash_string(addr->address, 0));
            ops_xp_routing_add_host_entry(ofproto, XPS_INTF_INVALID_ID,
                                          is_ipv6, addr->address,
                                          NULL, XPS_INTF_INVALID_ID,
                                          0, true, &addr->id);
        }
    }
} /* port_config_secondary_ipv4_addr */

/*
 * Function to check for changes in secondary ipv6 configuration of a
 * given port
 */
static void
port_config_secondary_ipv6_addr(struct ofproto_xpliant *ofproto,
                                struct bundle_xpliant *bundle,
                                const struct ofproto_bundle_settings *s)
{
    struct shash new_ip6_list;
    xp_net_addr_t *addr, *next;
    struct shash_node *addr_node;
    int i;
    bool is_ipv6 = true;

    shash_init(&new_ip6_list);

    /* Create hash of the current secondary ip's */
    for (i = 0; i < s->n_ip6_address_secondary; i++) {
        if(!shash_add_once(&new_ip6_list, s->ip6_address_secondary[i],
                           s->ip6_address_secondary[i])) {
            VLOG_WARN("Duplicate address in secondary list %s\n",
                      s->ip6_address_secondary[i]);
        }
    }

    /* Compare current and old to delete any obselete one's */
    HMAP_FOR_EACH_SAFE (addr, next, addr_node, &bundle->secondary_ip6addr) {
        if (!shash_find_data(&new_ip6_list, addr->address)) {
            hmap_remove(&bundle->secondary_ip6addr, &addr->addr_node);
            ops_xp_routing_delete_host_entry(ofproto, &addr->id);
            free(addr->address);
            free(addr);
        }
    }

    /* Add the newly added addresses to the list */
    SHASH_FOR_EACH (addr_node, &new_ip6_list) {
        xp_net_addr_t *addr;
        const char *address = addr_node->data;
        if (!port_ip6_addr_find(bundle, address)) {
            /*
             * Add the new address to the list
             */
            addr = xzalloc(sizeof *addr);
            addr->address = xstrdup(address);
            hmap_insert(&bundle->secondary_ip6addr, &addr->addr_node,
                        hash_string(addr->address, 0));
            ops_xp_routing_add_host_entry(ofproto, XPS_INTF_INVALID_ID,
                                          is_ipv6, addr->address,
                                          NULL, XPS_INTF_INVALID_ID,
                                          0, true, &addr->id);
        }
    }
}

/* Function to check for changes in ip configuration of a given port */
static int
port_ip_reconfigure(struct ofproto_xpliant *ofproto,
                    struct bundle_xpliant *bundle,
                    const struct ofproto_bundle_settings *s)
{
    bool is_ipv6 = false;

    VLOG_DBG("In port_ip_reconfigure with ip_change val=0x%x", s->ip_change);

    /* If primary ipv4 got added/deleted/modified */
    if (s->ip_change & PORT_PRIMARY_IPv4_CHANGED) {
        if (s->ip4_address) {
            if (bundle->ip4addr) {
                if (strcmp(bundle->ip4addr->address, s->ip4_address) != 0) {
                    /* If current and earlier are different, delete old */
                    ops_xp_routing_delete_host_entry(ofproto,
                                                     &bundle->ip4addr->id);
                    free(bundle->ip4addr->address);

                    /* Add new */
                    bundle->ip4addr->address = xstrdup(s->ip4_address);
                    ops_xp_routing_add_host_entry(ofproto, XPS_INTF_INVALID_ID,
                                                  false, bundle->ip4addr->address,
                                                  NULL, XPS_INTF_INVALID_ID,
                                                  0, true, &bundle->ip4addr->id);
                }
                /* else no change */
            } else {
                /* Earlier primary was not there, just add new */
                bundle->ip4addr = xzalloc(sizeof(xp_net_addr_t));
                bundle->ip4addr->address = xstrdup(s->ip4_address);
                ops_xp_routing_add_host_entry(ofproto, XPS_INTF_INVALID_ID,
                                              false, bundle->ip4addr->address,
                                              NULL, XPS_INTF_INVALID_ID,
                                              0, true, &bundle->ip4addr->id);
            }
        } else {
            /* Primary got removed, earlier if it was there then remove it */
            if (bundle->ip4addr != NULL) {
                ops_xp_routing_delete_host_entry(ofproto, &bundle->ip4addr->id);
                free(bundle->ip4addr->address);
                free(bundle->ip4addr);
                bundle->ip4addr = NULL;
            }
        }
    }

    /* If primary ipv6 got added/deleted/modified */
    if (s->ip_change & PORT_PRIMARY_IPv6_CHANGED) {
        is_ipv6 = true;
        if (s->ip6_address) {
            if (bundle->ip6addr) {
                if (strcmp(bundle->ip6addr->address, s->ip6_address) !=0) {
                    /* If current and earlier are different, delete old */
                    ops_xp_routing_delete_host_entry(ofproto,
                                                     &bundle->ip6addr->id);
                    free(bundle->ip6addr->address);

                    /* Add new */
                    bundle->ip6addr->address = xstrdup(s->ip6_address);
                    ops_xp_routing_add_host_entry(ofproto, XPS_INTF_INVALID_ID,
                                                  true, bundle->ip6addr->address,
                                                  NULL, XPS_INTF_INVALID_ID,
                                                  0, true, &bundle->ip6addr->id);

                }
                /* else no change */
            } else {
                /* Earlier primary was not there, just add new */
                bundle->ip6addr = xzalloc(sizeof(xp_net_addr_t));
                bundle->ip6addr->address = xstrdup(s->ip6_address);
                ops_xp_routing_add_host_entry(ofproto, XPS_INTF_INVALID_ID,
                                              true, bundle->ip6addr->address,
                                              NULL, XPS_INTF_INVALID_ID,
                                              0, true, &bundle->ip6addr->id);
            }
        } else {
            /* Primary got removed, earlier if it was there then remove it */
            if (bundle->ip6addr != NULL) {
                ops_xp_routing_delete_host_entry(ofproto, &bundle->ip6addr->id);
                free(bundle->ip6addr->address);
                free(bundle->ip6addr);
                bundle->ip6addr = NULL;
            }
        }
    }

    /* If any secondary ipv4 addr added/deleted/modified */
    if (s->ip_change & PORT_SECONDARY_IPv4_CHANGED) {
        VLOG_DBG("ip4_address_secondary modified");
        port_config_secondary_ipv4_addr(ofproto, bundle, s);
    }

    if (s->ip_change & PORT_SECONDARY_IPv6_CHANGED) {
        VLOG_DBG("ip6_address_secondary modified");
        port_config_secondary_ipv6_addr(ofproto, bundle, s);
    }

    return 0;
}

/* Updates L3 configuration. */
void
bundle_update_l3_config(struct ofproto_xpliant *ofproto,
                        struct bundle_xpliant *bundle,
                        const struct ofproto_bundle_settings *s)
{
    struct ofport_xpliant *port;
    xpsVlan_t vlan_id = 0;
    const char *type = NULL;

    ovs_assert(ofproto);
    ovs_assert(bundle);
    ovs_assert(s);

    port = get_ofp_port(bundle->ofproto, s->slaves[0]);
    if (!port) {
        VLOG_ERR("slave is not in the ports");
    }

    type = netdev_get_type(port->up.netdev);

    /* For internal vlan interfaces, we get vlanid from tag column
     * For sub-interfaces, we get vlanid from netdev config
     * For regular l3 interfaces we do not need vlanid
     * For loopback interfaces we just configure IP addresses
     */
    if (STR_EQ(type, OVSREC_INTERFACE_TYPE_INTERNAL)) {
        vlan_id = s->vlan;
    } else if (STR_EQ(type, OVSREC_INTERFACE_TYPE_VLANSUBINT)) {
        ops_xp_netdev_get_subintf_vlan(port->up.netdev, &vlan_id);
    } else if (STR_EQ(type, OVSREC_INTERFACE_TYPE_LOOPBACK)) {
        port_ip_reconfigure(ofproto, bundle, s);
        return;
    }

    if (bundle->l3_intf) {
        /* if reserved vlan changed/removed or if port status is disabled */
        if (bundle->l3_intf->vlan_id != vlan_id || !s->enable) {
            ops_xp_routing_disable_l3_interface(ofproto, bundle->l3_intf);
            bundle->l3_intf = NULL;

        } else if (s->enable && bundle->is_lag && bundle->ports_updated) {
            /* Ports were updated so need to update L3 inteface config. */
            ops_xp_routing_update_l3_interface(ofproto, bundle->l3_intf);
        }
    }

    if (!bundle->l3_intf && s->enable) {
        struct eth_addr mac;

        netdev_get_etheraddr(port->up.netdev, &mac);

        VLOG_INFO("%s: NETDEV %s, MAC "ETH_ADDR_FMT" VLAN %u",
                  __FUNCTION__, netdev_get_name(port->up.netdev),
                  ETH_ADDR_ARGS(mac), vlan_id);

        if (STR_EQ(type, OVSREC_INTERFACE_TYPE_SYSTEM)) {
            if (!bundle->is_lag) {
                bundle->intfId = ops_xp_get_ofport_intf_id(port);
            }
            bundle->l3_intf = \
                    ops_xp_routing_enable_l3_interface(ofproto,
                                                       bundle->intfId,
                                                       bundle->name,
                                                       mac.ea);
        } else if (STR_EQ(type, OVSREC_INTERFACE_TYPE_VLANSUBINT)) {
            if (vlan_id) {
                bundle->intfId = ops_xp_get_ofport_intf_id(port);
                bundle->l3_intf = \
                        ops_xp_routing_enable_l3_subinterface(ofproto,
                                                              bundle->intfId,
                                                              bundle->name,
                                                              vlan_id, mac.ea);
            }
        } else if (STR_EQ(type, OVSREC_INTERFACE_TYPE_INTERNAL)) {
            if (vlan_id) {
                bundle->l3_intf = \
                        ops_xp_routing_enable_l3_vlan_interface(ofproto,
                                                                vlan_id,
                                                                bundle->name,
                                                                mac.ea);
            }
        } else {
            VLOG_ERR("%s: unknown interface type: %s", __FUNCTION__, type);
        }
    }

    /* Check for ip changes */
    if (bundle->l3_intf) {
        port_ip_reconfigure(ofproto, bundle, s);
    }
}

static void
bundle_destroy(struct bundle_xpliant *bundle)
{
    struct ofproto_xpliant *ofproto;
    struct ofport_xpliant *port, *next_port;
    uint32_t vni = 0;

    VLOG_INFO("bundle_destroy: bundle.%s",
              bundle ? bundle->name : "_");

    if (!bundle) {
        return;
    }

    ofproto = bundle->ofproto;

    /* Unconfigure and free the l3 port/bundle ip related stuff */
    port_unconfigure_ips(bundle);

    if (bundle->l3_intf) {
        ops_xp_routing_disable_l3_interface(ofproto, bundle->l3_intf);
        bundle->l3_intf = NULL;
    }

    bundle_flush_macs(bundle);

    /* Clear all vlan membership for bundle intfId. */
    ovs_rwlock_wrlock(&ofproto->vlan_mgr->rwlock);
    ops_xp_vlan_port_clear_all_membership(ofproto->vlan_mgr, bundle->intfId);
    ovs_rwlock_unlock(&ofproto->vlan_mgr->rwlock);

    if (bundle->intfId != XPS_INTF_INVALID_ID) {
        ops_xp_dev_remove_intf_entry(ofproto->xpdev, bundle->intfId, 0);

        if (bundle->is_lag) {
            ops_xp_lag_destroy(ofproto->xpdev->id, bundle->name, bundle->intfId);
        }
    }

    LIST_FOR_EACH_SAFE (port, next_port, bundle_node, &bundle->ports) {
        bundle_del_port(port);
    }

    hmap_remove(&ofproto->bundles, &bundle->hmap_node);
    bitmap_free(bundle->trunks);
    free(bundle->name);
    free(bundle);
}

static int
ofproto_xpliant_bundle_set(struct ofproto *ofproto_, void *aux,
                           const struct ofproto_bundle_settings *s)
{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);
    struct ofport_xpliant *port = NULL;
    struct ofport_xpliant *next_port = NULL;
    const char *opt_arg;
    bool need_flush = false;
    struct bundle_xpliant *bundle = NULL;
    size_t i;
    int ret_val = 0;

    VLOG_INFO("ofproto_xpliant_bundle_set: ofproto.%s.%s bundle %s.%s",
              ofproto ? ofproto->up.name : "_",
              ofproto ? ofproto->up.type : "_",
              s ? s->name : "no_name",
              s ? "add" : "remove");

    if (!s) {
        bundle_destroy(bundle_lookup(ofproto, aux));
        return 0;
    }

    bundle = bundle_lookup(ofproto, aux);

    if (!bundle) {
        bundle = xmalloc(sizeof *bundle);

        bundle->ofproto = ofproto;
        hmap_insert(&ofproto->bundles, &bundle->hmap_node,
                    hash_pointer(aux, 0));
        bundle->aux = aux;
        bundle->name = NULL;
        bundle->intfId = XPS_INTF_INVALID_ID;
        bundle->is_lag = false;

        list_init(&bundle->ports);
        bundle->vlan_mode = PORT_VLAN_ACCESS;
        bundle->vlan = -1;
        bundle->trunks = NULL;
        bundle->use_priority_tags = s->use_priority_tags;

        bundle->floodable = true;

        /* L3 interface parameters init */
        bundle->l3_intf = NULL;
        bundle->ip4addr = NULL;
        bundle->ip6addr = NULL;
        hmap_init(&bundle->secondary_ip4addr);
        hmap_init(&bundle->secondary_ip6addr);
    }

    bundle->ports_updated = false;

    if (!bundle->name || !STR_EQ(s->name, bundle->name)) {
        free(bundle->name);
        bundle->name = xstrdup(s->name);
    }

    ret_val = bundle_create_remove_hw_lag(ofproto, bundle, s);
    if (s->bond_handle_alloc_only || ret_val) {
        return ret_val;
    }

    /* Remove extra ports from the bundle */
    LIST_FOR_EACH_SAFE (port, next_port, bundle_node, &bundle->ports) {
        for (i = 0; i < s->n_slaves; i++) {
            if (s->slaves[i] == port->up.ofp_port) {
                break;
            }
        }

        if (i == s->n_slaves) {
            bundle_del_port(port);
        }
    }

    /* Update set of ports. */
    for (i = 0; i < s->n_slaves; i++) {
        bundle_add_port(bundle, s->slaves[i]);
    }

    ovs_assert(list_size(&bundle->ports) <= s->n_slaves);

    if (list_is_empty(&bundle->ports)) {
        bundle_destroy(bundle);
        return 0;
    }

    VLOG_DBG("%s, ###### Bundle name: %s", __FUNCTION__, bundle->name);

    /* Handle LAG configuration in HW */
    if (bundle->is_lag) {
        ret_val = bundle_update_hw_lag_config(ofproto, bundle, s);
        if (ret_val) {
            return ret_val;
        }
    }

    /* Look for port configuration options
     * FIXME: - fill up stubs with actual actions */
    opt_arg = smap_get(s->port_options[PORT_OPT_VLAN], "vlan_options_p0");
    if (opt_arg != NULL) {
       VLOG_DBG("VLAN config options option_arg= %s", opt_arg);
    }

    opt_arg = smap_get(s->port_options[PORT_OPT_BOND], "bond_options_p0");
    if (opt_arg != NULL) {
       VLOG_DBG("BOND config options option_arg= %s", opt_arg);
    }

    /* Handle L3 configuration. */
    if (ofproto->vrf && (s->n_slaves > 0)) {
        bundle_update_l3_config(ofproto, bundle, s);
    } else if (!ofproto->vrf) {
        /* Update L2 port configuration */
        if (!bundle->is_lag && (s->n_slaves == 1)) {
            ret_val = bundle_update_single_slave_config(ofproto, bundle, s);
            if (ret_val) {
                return ret_val;
            }
        }
        /* Handle Port/LAG VLAN configuration */
        if (bundle->intfId != XPS_INTF_INVALID_ID) {
            bundle_update_vlan_config(ofproto, bundle, s);

            ops_xp_dev_add_intf_entry(ofproto->xpdev, bundle->intfId,
                                      bundle->name, 0);
        }
    }

    return 0;
}

static void
ofproto_xpliant_bundle_remove(struct ofport *port_)
{
    struct ofport_xpliant *port = ofport_xpliant_cast(port_);
    struct bundle_xpliant *bundle = port->bundle;
    const char *name = netdev_get_name(port->up.netdev);

    VLOG_INFO("ofproto_xpliant_bundle_remove: netdev.%s.%s, ofproto.%s.%s",
              name, netdev_get_type_from_name(name),
              bundle ? bundle->ofproto->up.name : "_",
              bundle ? bundle->ofproto->up.type : "_");

    if (bundle && (bundle->intfId != XPS_INTF_INVALID_ID)) {
        XP_STATUS retVal = xpsPortSetField(bundle->ofproto->xpdev->id, 
                                           bundle->intfId,
                                           XPS_PORT_ACCEPTED_FRAME_TYPE, 
                                           FRAMETYPE_ALL);
        if (retVal != XP_NO_ERR) {
            VLOG_ERR("%s: Could not set frametype in interface %d. "
                     "Error code: %d\n", 
                     __FUNCTION__, bundle->intfId, retVal);
            return;
        }
    }

    if (bundle) {
        bundle_del_port(port);
        if (list_is_empty(&bundle->ports)) {
            bundle_destroy(bundle);
        }
    }
}

static int
ofproto_xpliant_set_vlan(struct ofproto *ofproto_, int vid, bool add)
{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);
    struct bundle_xpliant *bundle;
    XP_STATUS status;
    int error;

    VLOG_INFO("%s: vid=%d, oper=%s", __FUNCTION__, vid, (add ? "add" : "del"));

    if (add) {
        /* Default VLAN is created on system init so no need to do this again.*/
        if (vid != XP_DEFAULT_VLAN_ID) {
            /* Create VLAN */
            error = ops_xp_vlan_create(ofproto->vlan_mgr, (xpsVlan_t)vid);
            if (error) {
                return error;
            }
        }

        /* Add this VLAN to any port specified by 'trunks' map */
        HMAP_FOR_EACH (bundle, hmap_node, &ofproto->bundles) {
            if (bundle->trunks && bitmap_is_set(bundle->trunks, vid)) {
                xpsL2EncapType_e encapType  = ((bundle->vlan != vid) ||
                    (bundle->vlan_mode == PORT_VLAN_NATIVE_TAGGED))
                                          ? XP_L2_ENCAP_DOT1Q_TAGGED
                                          : XP_L2_ENCAP_DOT1Q_UNTAGGED;
                status = ops_xp_vlan_member_add(ofproto->vlan_mgr,
                                                (xpsVlan_t)vid,
                                                bundle->intfId, encapType, 0);
                if (status) {
                    return EPERM;
                }
            }
        }
    } else {
        /* Delete this VLAN from any port specified by 'trunks' map */
        HMAP_FOR_EACH (bundle, hmap_node, &ofproto->bundles) {
            if (bundle->trunks && bitmap_is_set(bundle->trunks, vid)) {
                status = ops_xp_vlan_member_remove(ofproto->vlan_mgr,
                                                   (xpsVlan_t)vid,
                                                   bundle->intfId);
                if (status) {
                    return EPERM;
                }
            }
        }
        /* Do not remove default VLAN */
        if (vid != XP_DEFAULT_VLAN_ID) {
            /* Remove VLAN only if no l3 interfaces are still using it */
            if (ops_xp_vlan_is_membership_empty(ofproto->vlan_mgr,
                                                (xpsVlan_t)vid)) {
                error = ops_xp_vlan_remove(ofproto->vlan_mgr, (xpsVlan_t)vid);
                if (error) {
                    return error;
                }
            }
        }

        ops_xp_mac_learning_on_vlan_removed(ofproto->vlan_mgr->xp_dev->ml, 
                                            (xpsVlan_t)vid);
    }

    return 0;
}


/* Mirrors. */

static int
ofproto_xpliant_mirror_set(struct ofproto *ofproto_, void *aux,
             const struct ofproto_mirror_settings *s)
{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);
    struct ofbundle **srcs, **dsts;
    int error = EOPNOTSUPP;

	/* TODO */

    return error;
}

static int
ofproto_xpliant_mirror_get_stats(struct ofproto *ofproto, void *aux,
                                 uint64_t *packets, uint64_t *bytes)
{
	/* TODO */

    return EOPNOTSUPP;
}

static int
ofproto_xpliant_set_flood_vlans(struct ofproto *ofproto_, unsigned long *flood_vlans)
{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);

	/* TODO */

    return EOPNOTSUPP;
}

static bool
ofproto_xpliant_is_mirror_output_bundle(const struct ofproto *ofproto_, void *aux)
{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);

	/* TODO */
		
    return 0;
}

static void
ofproto_xpliant_forward_bpdu_changed(struct ofproto *ofproto_)
{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);
	
	/* TODO */    
}

static int
ofproto_xpliant_add_l3_host_entry(const struct ofproto *ofproto_, void *aux,
                                  bool is_ipv6_addr, char *ip_addr,
                                  char *next_hop_mac_addr, int *l3_egress_id)
{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);
    struct bundle_xpliant *bundle;
    int error = 0;

    bundle = bundle_lookup(ofproto, aux);
    if ( (bundle == NULL) ||
         (bundle->l3_intf == NULL) ) {
        VLOG_ERR("Failed to get port bundle/l3_intf not configured");
        return EPERM; /* Return error */
    }

    error = ops_xp_routing_add_host_entry(ofproto, bundle->intfId,
                                          is_ipv6_addr, ip_addr,
                                          next_hop_mac_addr,
                                          bundle->l3_intf->l3_intf_id,
                                          bundle->l3_intf->vlan_id, false,
                                          l3_egress_id);
    if (error) {
        VLOG_ERR("Failed to add L3 host entry for ip %s", ip_addr);
    }

    return error;
}

static int
ofproto_xpliant_delete_l3_host_entry(const struct ofproto *ofproto_, void *aux,
                                     bool is_ipv6_addr, char *ip_addr,
                                     int *l3_egress_id)

{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);
    struct bundle_xpliant *bundle;
    int error = 0;

    bundle = bundle_lookup(ofproto, aux);
    if (bundle == NULL) {
        VLOG_ERR("Failed to get port bundle");
        return EPERM; /* Return error */
    }

    error = ops_xp_routing_delete_host_entry(ofproto, l3_egress_id);
    if (error) {
        VLOG_ERR("Failed to delete L3 host entry for ip %s", ip_addr);
    }

    return error;
}

static int
ofproto_xpliant_get_l3_host_hit(const struct ofproto *ofproto_, void *aux,
                                bool is_ipv6_addr, char *ip_addr,
                                bool *hit_bit)
{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);
    struct bundle_xpliant *bundle;
    int error = 0;

    bundle = bundle_lookup(ofproto, aux);
    if (bundle == NULL) {
        VLOG_ERR("Failed to get port bundle");
        return EPERM; /* Return error */
    }

    return error;
}

static int
ofproto_xpliant_l3_route_action(const struct ofproto *ofproto_,
                                enum ofproto_route_action action,
                                struct ofproto_route *route)
{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);
    int error = 0;

    error = ops_xp_routing_route_entry_action(ofproto, action, route);

    return error;
}

int
ofproto_xpliant_l3_ecmp_set(const struct ofproto *ofproto_, bool enable)
{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);
    int error = 0;

    if (!enable) {
        VLOG_ERR("Disabling ECMP is currently not supported");
        return EOPNOTSUPP;
    } 

    return error;
}

int
ofproto_xpliant_l3_ecmp_hash_set(const struct ofproto *ofproto_,
                                 unsigned int hash, bool enable)
{
    struct ofproto_xpliant *ofproto = ops_xp_ofproto_cast(ofproto_);
    int error = 0;

    error = ops_xp_routing_ecmp_hash_set(ofproto, hash, enable);

    if (error) {
        VLOG_ERR("Failed to set ECMP hash fields");
    }

    return error;
}


/* ofproto class structure,
*  defined for Xpliant ofproto implementation.
*/

const struct ofproto_class ofproto_xpliant_class = {

/* ## ----------------- ## */
/* ## Factory Functions ## */
/* ## ----------------- ## */

    ofproto_xpliant_init,
    ofproto_xpliant_enumerate_types,
    ofproto_xpliant_enumerate_names,
    ofproto_xpliant_del_datapath,
    ofproto_xpliant_port_open_type,

/* ## ------------------------ ## */
/* ## Top-Level type Functions ## */
/* ## ------------------------ ## */

    NULL, /* temp ofproto_xpliant_type_run,*/
    NULL, /* temp ofproto_xpliant_type_wait, */

/* ## --------------------------- ## */
/* ## Top-Level ofproto Functions ## */
/* ## --------------------------- ## */

    ofproto_xpliant_alloc,
    ofproto_xpliant_construct,
    ofproto_xpliant_destruct,
    ofproto_xpliant_dealloc,
    ofproto_xpliant_run,
    ofproto_xpliant_wait,
    NULL,                       /* get_memory_usage */
    ofproto_xpliant_type_get_memory_usage,
    NULL,                       /* flush */
    NULL,                       /* query_tables */
    ofproto_xpliant_set_tables_version,

/* ## ---------------- ## */
/* ## ofport Functions ## */
/* ## ---------------- ## */

    ofproto_xpliant_port_alloc,
    ofproto_xpliant_port_construct,
    ofproto_xpliant_port_destruct,
    ofproto_xpliant_port_dealloc,
    ofproto_xpliant_port_modified,
    ofproto_xpliant_port_reconfigured,
    ofproto_xpliant_port_query_by_name,
    ofproto_xpliant_port_add,
    ofproto_xpliant_port_del,
    ofproto_xpliant_port_get_stats,
    ofproto_xpliant_port_dump_start,
    ofproto_xpliant_port_dump_next,
    ofproto_xpliant_port_dump_done,
    NULL, /* temp ofproto_xpliant_port_poll, */
    ofproto_xpliant_port_poll_wait,
    NULL,                       /* port_is_lacp_current */
    NULL,                       /* port_get_lacp_stats */

/* ## ----------------------- ## */
/* ## OpenFlow Rule Functions ## */
/* ## ----------------------- ## */
    NULL,                       /* rule_choose_table */
    ofproto_xpliant_rule_alloc,
    ofproto_xpliant_rule_construct,
    ofproto_xpliant_rule_insert,
    ofproto_xpliant_rule_delete,
    ofproto_xpliant_rule_destruct,
    ofproto_xpliant_rule_dealloc,
    ofproto_xpliant_rule_get_stats,
    ofproto_xpliant_rule_execute,
    ofproto_xpliant_set_frag_handling,
    ofproto_xpliant_packet_out,

/* ## ------------------------- ## */
/* ## OFPP_NORMAL configuration ## */
/* ## ------------------------- ## */

    NULL,                       /* set_netflow */
    NULL,                       /* get_netflow_ids */
    NULL,                       /* set_sflow */
    NULL,                       /* set_ipfix */
    NULL,                       /* set_cfm */
    NULL,                       /* cfm_status_changed */
    NULL,                       /* get_cfm_status */
    NULL,                       /* set_lldp */
    NULL,                       /* get_lldp_status */
    NULL,                       /* set_aa */
    NULL,                       /* aa_mapping_set */
    NULL,                       /* aa_mapping_unset */
    NULL,                       /* aa_vlan_get_queued */
    NULL,                       /* aa_vlan_get_queue_size */
    NULL,                       /* set_bfd */
    NULL,                       /* bfd_status_changed */
    NULL,                       /* get_bfd_status */
    NULL,                       /* set_stp */
    NULL,                       /* get_stp_status */
    NULL,                       /* set_stp_port */
    NULL,                       /* get_stp_port_status */
    NULL,                       /* get_stp_port_stats */
    NULL,                       /* set_rstp */
    NULL,                       /* get_rstp_status */
    NULL,                       /* set_rstp_port */
    NULL,                       /* get_rstp_port_status */
    NULL,                       /* set_queues */
    ofproto_xpliant_bundle_set,
    ofproto_xpliant_bundle_remove,
    NULL,                       /* bundle_get */
    ofproto_xpliant_set_vlan,
    ofproto_xpliant_mirror_set,
    ofproto_xpliant_mirror_get_stats,
    ofproto_xpliant_set_flood_vlans,
    ofproto_xpliant_is_mirror_output_bundle,
    ofproto_xpliant_forward_bpdu_changed,
    NULL,                       /* set_mac_table_config */
    NULL,                       /* set_mcast_snooping */
    NULL,                       /* set_mcast_snooping_port */
    NULL,                       /* set_realdev */

/* ## ------------------------ ## */
/* ## OpenFlow meter functions ## */
/* ## ------------------------ ## */

    NULL,                       /* meter_get_features */
    NULL,                       /* meter_set */
    NULL,                       /* meter_get */
    NULL,                       /* meter_del */

/* ## -------------------- ## */
/* ## OpenFlow 1.1+ groups ## */
/* ## -------------------- ## */
    ofproto_xpliant_group_alloc,
    ofproto_xpliant_group_construct,
    ofproto_xpliant_group_destruct,
    ofproto_xpliant_group_dealloc,
    ofproto_xpliant_group_modify,
    ofproto_xpliant_group_get_stats,
    ofproto_xpliant_get_datapath_version,

    ofproto_xpliant_add_l3_host_entry,
    ofproto_xpliant_delete_l3_host_entry,
    ofproto_xpliant_get_l3_host_hit,
    ofproto_xpliant_l3_route_action,
    ofproto_xpliant_l3_ecmp_set,
    ofproto_xpliant_l3_ecmp_hash_set,
};


struct ofproto_xpliant *
ops_xp_ofproto_lookup(const char *name)
{
    struct ofproto_xpliant *ofproto;

    ovs_assert(name);

    HMAP_FOR_EACH_WITH_HASH (ofproto, all_ofproto_xpliant_node,
                             hash_string(name, 0), &all_ofproto_xpliant) {
        if (STR_EQ(ofproto->up.name, name)) {
            return ofproto;
        }
    }
    return NULL;
}

static void
unixctl_shell_start(struct unixctl_conn *conn, int argc OVS_UNUSED,
                    const char *argv[] OVS_UNUSED, void *aux OVS_UNUSED)
{
    XP_STATUS status;
    status = xpsShellInit();
    if (status == XP_NO_ERR) {
        unixctl_command_reply(conn, "Started new shell session");
    } else {
        unixctl_command_reply_error(conn, "Failed to start new shell session");
    }
}

static void
unixctl_shell_stop(struct unixctl_conn *conn, int argc OVS_UNUSED,
                   const char *argv[] OVS_UNUSED, void *aux OVS_UNUSED)
{
    XP_STATUS status;
    status = xpsShellDeInit();
    if (status == XP_NO_ERR) {
        unixctl_command_reply(conn, "Stopped shell session");
    } else {
        unixctl_command_reply_error(conn, "Failed to stop shell session");
    }
}

static void
xp_unixctl_fdb_flush(struct unixctl_conn *conn, int argc OVS_UNUSED,
                     const char *argv[] OVS_UNUSED, void *aux OVS_UNUSED)
{
    struct ofproto_xpliant *ofproto;
    const char *fdb_type_s = argc > 1 ? argv[2] : "dynamic";
    bool dynamic_only = true;;

    ofproto = ops_xp_ofproto_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such bridge");
        return;
    }

    /* Get entry type. */
    if (STR_EQ(fdb_type_s, "all")) {
        dynamic_only = false;
    }

    ovs_rwlock_wrlock(&ofproto->ml->rwlock);
    ops_xp_mac_learning_flush(ofproto->ml, dynamic_only);
    ovs_rwlock_unlock(&ofproto->ml->rwlock);

    unixctl_command_reply(conn, "table successfully flushed");
}

static void
xp_unixctl_fdb_show(struct unixctl_conn *conn, int argc OVS_UNUSED,
                    const char *argv[] OVS_UNUSED, void *aux OVS_UNUSED)
{
    struct ds d_str = DS_EMPTY_INITIALIZER;
    const struct ofproto_xpliant *ofproto = NULL;

    ofproto = ops_xp_ofproto_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such bridge");
        return;
    }

    ovs_rwlock_rdlock(&ofproto->ml->rwlock);
    ops_xp_mac_learning_dump_table(ofproto->ml, &d_str);
    ovs_rwlock_unlock(&ofproto->ml->rwlock);

    unixctl_command_reply(conn, ds_cstr(&d_str));
    ds_destroy(&d_str);
}

static void
xp_unixctl_fdb_hw_dump(struct unixctl_conn *conn, int argc OVS_UNUSED,
                       const char *argv[] OVS_UNUSED, void *aux OVS_UNUSED)
{
    const struct ofproto_xpliant *ofproto = NULL;
    struct ds d_str = DS_EMPTY_INITIALIZER;
    uint32_t numOfValidEntries = 0;

    ofproto = ops_xp_ofproto_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such bridge");
        return;
    }

    xpsFdbDisplayTable(ofproto->xpdev->id, &numOfValidEntries,
                             0, 0, NULL, 0, 0);

    ds_put_format(&d_str, "Dumped %d fdb intries into xp log file",
                  numOfValidEntries);

    unixctl_command_reply(conn, ds_cstr(&d_str));
    ds_destroy(&d_str);
}

static void
xp_unixctl_fdb_add_entry(struct unixctl_conn *conn, int argc,
                         const char *argv[], void *aux OVS_UNUSED)
{
    const struct ofproto_xpliant *ofproto = NULL;
    const char *port_s = argv[2];
    const char *vlan_s = argv[3];
    const char *mac_s = argv[4];
    const char *type_s = argc > 5 ? argv[5] : "static";
    struct xp_ml_learning_data data;
    struct bundle_xpliant *bundle = NULL;
    int status;

    memset(&data, 0, sizeof(data));

    /* Get bridge. */
    ofproto = ops_xp_ofproto_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such bridge");
        return;
    }

    /* Get MAC. */
    status = ovs_scan(mac_s, XP_MAC_ADDR_SCAN_FMT, 
                      XP_MAC_ADDR_SCAN_ARGS(data.xps_fdb_entry.macAddr));

    if (!status
        || ops_xp_ml_addr_is_multicast(data.xps_fdb_entry.macAddr, false)) {

        unixctl_command_reply_error(conn, "invalid mac");
        return;
    }

    /* Get VLAN. */
    status = ovs_scan(vlan_s, "%"SCNu16,
                      &data.xps_fdb_entry.vlanId);

    ovs_rwlock_rdlock(&ofproto->vlan_mgr->rwlock);
    if (!status ||
        !ops_xp_vlan_is_existing(ofproto->vlan_mgr,
                                 data.xps_fdb_entry.vlanId)) {

        ovs_rwlock_unlock(&ofproto->vlan_mgr->rwlock);
        unixctl_command_reply_error(conn, "invalid vlan");
        return;
    }

    if (ops_xp_vlan_is_flooding(ofproto->vlan_mgr,
                                data.xps_fdb_entry.vlanId)) {

        ovs_rwlock_unlock(&ofproto->vlan_mgr->rwlock);
        unixctl_command_reply_error(conn, "can not add entry to a flooding vlan");
        return;
    }

    ovs_rwlock_unlock(&ofproto->vlan_mgr->rwlock);

    /* Get Port. */
    HMAP_FOR_EACH (bundle, hmap_node, &ofproto->bundles) {
        if (STR_EQ(bundle->name, port_s)) {
            break;
        }
    }

    if (!bundle) {
        unixctl_command_reply_error(conn, "invalid port");
        return;
    }

    data.xps_fdb_entry.intfId = bundle->intfId;

    if (ops_xp_is_l3_tunnel_intf(bundle->intfId)) {

        unixctl_command_reply_error(conn, "Fdb learning is not supported "
                                          "on L3 tunnels.");
        return;
    }

    if (ops_xp_is_l2_tunnel_intf(bundle->intfId)) {
        unixctl_command_reply_error(conn, "Fdb learning is not supported "
                                          "on L2 tunnels.");
        return;
    }

    /* Get entry type. */
    if (STR_EQ(type_s, "static")) {
        /* TODO: Figure out what is the right value here. */
        data.xps_fdb_entry.isStatic = 1;

    } else if (STR_EQ(type_s, "dynamic")) {

        data.xps_fdb_entry.isStatic = 0;

    } else {

        unixctl_command_reply_error(conn, "Wrong entry type. Acceptable types "
                                    "are: \"static\" or \"dynamic\"");
        return;
    }

    data.reasonCode = XP_BRIDGE_MAC_SA_NEW;

    ovs_rwlock_wrlock(&ofproto->ml->rwlock);
    status = ops_xp_mac_learning_learn(ofproto->ml, &data);
    ovs_rwlock_unlock(&ofproto->ml->rwlock);

    if (status != 0) {
        if (status == EEXIST) {
            unixctl_command_reply_error(conn, "entry already exists for this "
                                              "VLAN and MAC");
        } else {
            unixctl_command_reply_error(conn, "could not add entry");
        }
        return;
    }

    unixctl_command_reply(conn, "entry has been added successfully");
}

static void
xp_unixctl_fdb_remove_entry(struct unixctl_conn *conn, int argc OVS_UNUSED,
                            const char *argv[] OVS_UNUSED, void *aux OVS_UNUSED)
{
    const struct ofproto_xpliant *ofproto = NULL;
    const char *vlan_s = argv[2];
    const char *mac_s = argv[3];
    macAddr_t macAddr;
    xpsVlan_t vlan = 0;
    bool status = true;

    /* Get brindge. */
    ofproto = ops_xp_ofproto_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such bridge");
        return;
    }

    /* Get MAC. */
    status = ovs_scan(mac_s, XP_MAC_ADDR_SCAN_FMT, 
                      XP_MAC_ADDR_SCAN_ARGS(macAddr));

    if (!status || ops_xp_ml_addr_is_multicast(macAddr, false)) {
        unixctl_command_reply_error(conn, "invalid mac");
        return;
    }

    /* Get VLAN. */
    status = ovs_scan(vlan_s, "%"SCNu16, &vlan);

    ovs_rwlock_rdlock(&ofproto->vlan_mgr->rwlock);
    if (!status || !ops_xp_vlan_is_existing(ofproto->vlan_mgr, vlan)) {

        ovs_rwlock_unlock(&ofproto->vlan_mgr->rwlock);
        unixctl_command_reply_error(conn, "invalid vlan");
        return;
    }
    ovs_rwlock_unlock(&ofproto->vlan_mgr->rwlock);


    ovs_rwlock_wrlock(&ofproto->ml->rwlock);
    if (ops_xp_mac_learning_age_by_vlan_and_mac(ofproto->ml, vlan, macAddr) != 0) {

        ovs_rwlock_unlock(&ofproto->ml->rwlock);
        unixctl_command_reply_error(conn, "could not remove entry");
        return;
    }
    ovs_rwlock_unlock(&ofproto->ml->rwlock);

    unixctl_command_reply(conn, "entry has been removed successfully");
}

static void
xp_unixctl_fdb_configure_aging(struct unixctl_conn *conn, int argc,
                               const char *argv[], void *aux OVS_UNUSED)
{
    const struct ofproto_xpliant *ofproto = NULL;
    unsigned int idle_time;
    int status;

    /* Get bridge */
    ofproto = ops_xp_ofproto_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such bridge");
        return;
    }

    /* Get Age time. */
    status = ovs_scan(argv[2], "%"SCNu32, &idle_time);
    if (!status) {
        unixctl_command_reply_error(conn, "invalid age cycle unit time");
        return;
    }

    status = ops_xp_mac_learning_set_idle_time(ofproto->ml, idle_time);
    if (status) {
        unixctl_command_reply_error(conn, "failed to set idle time");
        return;
    }

    unixctl_command_reply(conn, "FDB aging time been update successfully");
}

static void
xp_unixctl_port_serdes_tune(struct unixctl_conn *conn, int argc,
                            const char *argv[], void *aux OVS_UNUSED)
{
    const struct ofproto_xpliant *ofproto = NULL;
    XP_STATUS xp_status = XP_NO_ERR;
    const char *start_port_s = argv[2];
    const char *end_port_s = argc > 3 ? argv[3] : argv[2];
    const char *tune_mode_s = argc > 4 ? argv[4] : "0";
    uint32_t start_port = 0;
    uint32_t end_port = 0;
    uint32_t tune_mode = 0;
    uint32_t port = 0;
    bool status = false;

    /* Get bridge */
    ofproto = ops_xp_ofproto_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such bridge");
        return;
    }

    /* Get start port number */
    status = ovs_scan(start_port_s, "%u", &start_port);
    if (!status || start_port >= XP_MAX_TOTAL_PORTS) {
        unixctl_command_reply_error(conn, "invalid start port number");
        return;
    }

    /* Get end port number */
    status = ovs_scan(end_port_s, "%u", &end_port);
    if (!status || end_port < start_port) {
        unixctl_command_reply_error(conn, "invalid end port number");
        return;
    }

    /* Get serdes tune mode */
    status = ovs_scan(tune_mode_s, "%u", &tune_mode);
    if (!status || (xpSerdesDfeTuneMode_t)tune_mode > DFE_DISABLE_RR) {
        unixctl_command_reply_error(conn, "invalid serdes tune mode (0..5)");
        return;
    }

    for (port = start_port; port < end_port; port++) {
        uint8_t valid = 0;

        xp_status = xpsMacIsPortNumValid(ofproto->xpdev->id, port, &valid);
        if (xp_status != XP_NO_ERR || !valid) {
            continue;
        }

        xp_status = xpsIsPortInited(ofproto->xpdev->id, port);
        if (xp_status != XP_NO_ERR) {
            unixctl_command_reply_error(conn, "port is not initialized");
            return;
        }

        xp_status = xpsMacPortSerdesTune(ofproto->xpdev->id, &port, 1,
                                         (xpSerdesDfeTuneMode_t)tune_mode, 0);
        if (xp_status != XP_NO_ERR) {
            unixctl_command_reply_error(conn, "failed to tune serdes");
            return;
        }
    }

    unixctl_command_reply(conn, "tuned successfully");
}

static const char *
xp_port_speed_to_str(xpSpeed speed)
{
    switch (speed)
    {
        case SPEED_10MB:    return "10M";
        case SPEED_100MB:   return "100M";
        case SPEED_1GB:     return "1G";
        case SPEED_1GB_PCS: return "1G";
        case SPEED_10GB:    return "10G";
        case SPEED_40GB:    return "40G";
        case SPEED_100GB:   return "100G";
        case SPEED_25GB:    return "25G";
        case SPEED_50GB:    return "50G";
    }
    return "X";
}

static void
xp_unixctl_port_show(struct unixctl_conn *conn, int argc OVS_UNUSED,
                     const char *argv[] OVS_UNUSED, void *aux OVS_UNUSED)
{
    struct ds d_str = DS_EMPTY_INITIALIZER;
    const struct ofproto_xpliant *ofproto = NULL;
    XP_STATUS xp_status = XP_NO_ERR;
    const char *start_port_s = argv[2];
    const char *end_port_s = argc > 3 ? argv[3] : argv[2];
    uint32_t start_port = 0;
    uint32_t end_port = 0;
    uint32_t port = 0;
    bool status = false;

    /* Get bridge */
    ofproto = ops_xp_ofproto_lookup(argv[1]);
    if (!ofproto) {
        unixctl_command_reply_error(conn, "no such bridge");
        return;
    }

    /* Get start port number */
    status = ovs_scan(start_port_s, "%u", &start_port);
    if (!status || start_port >= XP_MAX_TOTAL_PORTS) {
        unixctl_command_reply_error(conn, "invalid start port number");
        return;
    }

    /* Get end port number */
    status = ovs_scan(end_port_s, "%u", &end_port);
    if (!status || end_port < start_port) {
        unixctl_command_reply_error(conn, "invalid end port number");
        return;
    }

    ds_put_cstr(&d_str, " ===========================\n");
    ds_put_cstr(&d_str, " port   group   speed   link\n");
    ds_put_cstr(&d_str, " ===========================\n");

    for (port = start_port; port <= end_port; port++) {
        uint8_t valid = 0;
        struct hmap_node *e = NULL;
        xpSpeed speed = SPEED_MAX_VAL;
        uint8_t link = 0;

        xp_status = xpsMacIsPortNumValid(ofproto->xpdev->id, port, &valid);
        if (xp_status != XP_NO_ERR || !valid) {
            continue;
        }

        ds_put_format(&d_str, "%5u   %5u   ",
                      port, port / XP_MAX_CHAN_PER_MAC);

        xp_status = xpsIsPortInited(ofproto->xpdev->id, port);
        if (xp_status != XP_NO_ERR) {
            ds_put_format(&d_str, "%5s   %4s\n", "X", "X");
            continue;
        }

        xp_status = xpsMacGetPortSpeed(ofproto->xpdev->id, port, &speed);
        if (xp_status != XP_NO_ERR) {
            ds_put_format(&d_str, "%5s   ", "X");
        } else {
            ds_put_format(&d_str, "%5s   ", xp_port_speed_to_str(speed));
        }

        xp_status = xpsMacGetLinkStatus(ofproto->xpdev->id, port, &link);
        if (xp_status != XP_NO_ERR) {
            ds_put_format(&d_str, "%4s\n", "X");
        } else {
            ds_put_format(&d_str, "%4s\n", (link == 0) ? "DOWN" : "UP");
        }

    }

    unixctl_command_reply(conn, ds_cstr(&d_str));
    ds_destroy(&d_str);
}

static void
xp_sdk_log_level(struct unixctl_conn *conn, int argc,
                 const char *argv[], void *aux OVS_UNUSED)
{
    const char *log_level_s = argc > 1 ? argv[1] : "ERROR";

    ops_xp_sdk_log_level_set(log_level_s);
    unixctl_command_reply(conn, "sdk log_level set successfully");
}

static void
ofproto_xpliant_unixctl_init(void)
{
    static bool registered;
    if (registered) {
        return;
    }
    registered = true;

    unixctl_command_register("xp/shell/start", "", 0, 0,
                             unixctl_shell_start, NULL);
    unixctl_command_register("xp/shell/stop", "", 0, 0,
                             unixctl_shell_stop, NULL);

    unixctl_command_register("xp/fdb/flush", "bridge [dynamic|all]", 1, 2,
                             xp_unixctl_fdb_flush, NULL);
    unixctl_command_register("xp/fdb/show", "bridge", 1, 1,
                             xp_unixctl_fdb_show, NULL);
    unixctl_command_register("xp/fdb/hw-dump", "bridge", 1, 1,
                             xp_unixctl_fdb_hw_dump, NULL);
    unixctl_command_register("xp/fdb/add-entry",
                             "bridge port vlan mac [dynamic]",
                             4, 5, xp_unixctl_fdb_add_entry, NULL);
    unixctl_command_register("xp/fdb/remove-entry", "bridge vlan mac",
                             3, 3, xp_unixctl_fdb_remove_entry, NULL);
    unixctl_command_register("xp/fdb/set-age", "bridge idle_time",
                             2, 2, xp_unixctl_fdb_configure_aging, NULL);

    unixctl_command_register(
        "xp/port/serdes/tune", "bridge start_port [end_port [mode]]",
        2, 4, xp_unixctl_port_serdes_tune, NULL);

    unixctl_command_register(
        "xp/port/show", "bridge start_port [end_port]",
        2, 3, xp_unixctl_port_show, NULL);

    unixctl_command_register("xp/sdk/log_level",
                             "[TRACE|DEBUG|WARNING|ERROR|CRITICAL|DEFAULT]",
                             1, 1, xp_sdk_log_level, NULL);

    ops_xp_routing_unixctl_init();
}
