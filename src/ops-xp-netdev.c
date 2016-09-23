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
 * File: ops-xp-netdev.c
 *
 * Purpose: This file contains OpenSwitch netdev related application code
 *          for the Cavium/XPliant SDK.
 */

#if !defined(OPS_AS7512) && !defined(OPS_XP_SIM)
#define OPS_XP_SIM
#endif

#include <errno.h>
#include <sys/ioctl.h>

#include "ops-xp-netdev.h"
#include "latch.h"
#include "poll-loop.h"
#include <openvswitch/vlog.h>
#include <openswitch-idl.h>
#include <openswitch-dflt.h>
#include "sset.h"
#include "socket-util.h"
#include "ops-xp-dev-init.h"
#include "ops-xp-util.h"
#include "ops-xp-host.h"
#include "ops-xp-qos.h"
#include "ops-xp-vlan.h"
#include "ops-xp-mac-learning.h"
#include "openXpsMac.h"
#include "openXpsQos.h"
#include "openXpsPolicer.h"
#include "openXpsPort.h"


VLOG_DEFINE_THIS_MODULE(xp_netdev);


/* Temporary definition. Will be removed when functionality which allows
   retrieving number of queues is present in XDK. */
#define XPDEV_N_QUEUES         4000
#define XPDEV_HASH_BASIS       0

struct xpdev_queue_state {
    unsigned int *queues;
    size_t cur_queue;
    size_t n_queues;
};


static struct vlog_rate_limit poll_rl = VLOG_RATE_LIMIT_INIT(5, 20);

/* This is set pretty low because we probably won't learn anything from the
 * additional log messages. */
static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(5, 20);

static int netdev_xpliant_construct(struct netdev *);

static struct xpdev_queue *xpdev_find_queue(const struct netdev_xpliant *xpdev,
                                            unsigned int queue_id);
static int xpdev_get_queue(const struct netdev_xpliant *xpdev,
                           unsigned int queue_id, struct smap *details);


/* Protects 'xp_netdev_list'. */
static struct ovs_mutex xp_netdev_list_mutex = OVS_MUTEX_INITIALIZER;

/* Contains all 'struct sim_dev's. */
static struct ovs_list xp_netdev_list OVS_GUARDED_BY(xp_netdev_list_mutex)
    = OVS_LIST_INITIALIZER(&xp_netdev_list);


bool
is_xpliant_class(const struct netdev_class *class)
{
    return (class->construct == netdev_xpliant_construct);
}

struct netdev_xpliant *
netdev_xpliant_cast(const struct netdev *netdev)
{
    ovs_assert(is_xpliant_class(netdev_get_class(netdev)));
    return CONTAINER_OF(netdev, struct netdev_xpliant, up);
}

static struct netdev_rxq_xpliant *
netdev_rxq_xpliant_cast(const struct netdev_rxq *rx)
{
    ovs_assert(is_xpliant_class(netdev_get_class(rx->netdev)));
    return CONTAINER_OF(rx, struct netdev_rxq_xpliant, up);
}

struct netdev_xpliant *
ops_xp_netdev_from_port_num(uint32_t dev_id, uint32_t port_id)
{
    struct netdev_xpliant *netdev = NULL;
    bool found = false;

    ovs_mutex_lock(&xp_netdev_list_mutex);
    LIST_FOR_EACH(netdev, list_node, &xp_netdev_list) {
        if ((netdev->xpdev != NULL) &&
            (netdev->xpdev->id == dev_id) &&
            (netdev->port_num == port_id)) {

            /* If the port is splittable, and it is
             * split into child ports, then skip it. */
            if (netdev->is_split_parent &&
                (netdev->port_info->port_mac_mode == MAC_MODE_4X10GB)) {
                continue;
            }
            found = true;
            break;
        }
    }
    ovs_mutex_unlock(&xp_netdev_list_mutex);
    return (found == true) ? netdev : NULL;
}

static struct netdev *
netdev_xpliant_alloc(void)
{
    struct netdev_xpliant *netdev = xzalloc(sizeof *netdev);
    XP_TRACE();
    return &netdev->up;
}

static int
netdev_xpliant_init(void)
{
    return ops_xp_dev_srv_init();
}

static int
netdev_xpliant_construct(struct netdev *netdev_)
{
    struct netdev_xpliant *netdev = netdev_xpliant_cast(netdev_);
    struct hmap queues = HMAP_INITIALIZER(&netdev->queues);
    static atomic_count next_n = ATOMIC_COUNT_INIT(0xaa550000);
    unsigned int n;

    VLOG_INFO("%s: netdev %s", __FUNCTION__, netdev_get_name(netdev_));

    ovs_mutex_init(&netdev->mutex);

    netdev->intf_initialized = false;

    netdev->port_num = XP_MAX_TOTAL_PORTS;
    netdev->xpdev = NULL;
    memset(&(netdev->pcfg), 0x0, sizeof(struct port_cfg));
    netdev->flags = 0;
    netdev->link_status = false;
    netdev->ifId = XPS_INTF_INVALID_ID;
    netdev->vif = XPS_INTF_MAP_INTFID_TO_VIF(netdev->ifId);
    netdev->queues = queues;
    netdev->rxq = NULL;
    netdev->qos_type = "xpliant-qos";
    netdev->n_queues = XPDEV_N_QUEUES;

    netdev->port_info = NULL;
    netdev->is_split_parent = false;
    netdev->is_split_subport = false;
    netdev->parent_port_info = NULL;

    netdev->subintf_parent_name = NULL;
    netdev->subintf_vlan_id = 0;

    netdev->xpnet_if_id = 0;
    netdev->xpnet_port_filter_id = 0;

    netdev->tap_fd = 0;

    n = atomic_count_inc(&next_n);
    netdev->hwaddr[0] = 0xaa;
    netdev->hwaddr[1] = 0x55;
    netdev->hwaddr[2] = n >> 24;
    netdev->hwaddr[3] = n >> 16;
    netdev->hwaddr[4] = n >> 8;
    netdev->hwaddr[5] = n;

    ovs_mutex_lock(&xp_netdev_list_mutex);
    list_push_back(&xp_netdev_list, &netdev->list_node);
    ovs_mutex_unlock(&xp_netdev_list_mutex);

    return 0;
}

static void
netdev_xpliant_destruct(struct netdev *netdev_)
{
    XP_STATUS ret = XP_NO_ERR;
    struct netdev_xpliant *netdev = netdev_xpliant_cast(netdev_);
    int retval = 0;

    VLOG_INFO("%s: netdev %s", __FUNCTION__, netdev_get_name(netdev_));

    ovs_mutex_lock(&xp_netdev_list_mutex);

    if (netdev->subintf_parent_name != NULL) {
        free(netdev->subintf_parent_name);
    }

    if (netdev->xpnet_if_id) {
        retval = ops_xp_host_if_delete(netdev->xpdev, netdev->xpnet_if_id);

        if (retval) {
            VLOG_ERR("Failed to delete kernel XPNET interface %s",
                     netdev_get_name(&(netdev->up)));
        }
    }

    if (netdev->tap_fd > 0) {
        close(netdev->tap_fd);
    }

    list_remove(&netdev->list_node);
    ovs_mutex_unlock(&xp_netdev_list_mutex);
    hmap_destroy(&netdev->queues);
    ops_xp_dev_free(netdev->xpdev);
    ovs_mutex_destroy(&netdev->mutex);
}

static void
netdev_xpliant_dealloc(struct netdev *netdev_)
{
    struct netdev_xpliant *netdev = netdev_xpliant_cast(netdev_);

    free(netdev);
}

static int
netdev_xpliant_set_hw_intf_info(struct netdev *netdev_, const struct smap *args)
{
    struct netdev_xpliant *netdev = netdev_xpliant_cast(netdev_);
    struct netdev *p_netdev_ = NULL;
    struct netdev_xpliant *p_netdev = NULL;
    struct xp_port_info *port_info = NULL;
    struct ether_addr ZERO_MAC = {{0}};
    struct ether_addr *ether_mac = &ZERO_MAC;
    int status = XP_NO_ERR;
    int rc = 0;
    uint32_t dev_num;
    xpsPortConfig_t port_config;

    const char *hw_unit = smap_get(args, INTERFACE_HW_INTF_INFO_MAP_SWITCH_UNIT);
    const char *hw_id = smap_get(args, INTERFACE_HW_INTF_INFO_MAP_SWITCH_INTF_ID);
    const char *mac_addr = smap_get(args, INTERFACE_HW_INTF_INFO_MAP_MAC_ADDR);
    const char *is_splittable = smap_get(args, INTERFACE_HW_INTF_INFO_MAP_SPLIT_4);
    const char *split_parent = smap_get(args, INTERFACE_HW_INTF_INFO_SPLIT_PARENT);

    VLOG_INFO("%s API call for %s.\n", __FUNCTION__, netdev_get_name(netdev_));

    ovs_mutex_lock(&netdev->mutex);

    if (netdev->intf_initialized == false) {

        dev_num = (hw_unit) ? atoi(hw_unit) : XP_MAX_DEVICES;
        if (dev_num >= XP_MAX_DEVICES) {
            VLOG_ERR("Invalid switch unit id %s", hw_unit);
            goto error;
        }

        netdev->xpdev = ops_xp_dev_by_id(dev_num);
        if (netdev->xpdev == NULL) {
            VLOG_ERR("Switch unit is not initialized");
            goto error;
        }

        netdev->port_num = (hw_id) ? (atoi(hw_id) - 1) : XP_MAX_TOTAL_PORTS;
        if (netdev->port_num >= XP_MAX_TOTAL_PORTS) {
            VLOG_ERR("Invalid switch port id %s", hw_id);
            goto error;
        }

        if (mac_addr) {
            ether_mac = ether_aton(mac_addr);
            if (ether_mac != NULL) {
                VLOG_INFO("%s: Set MAC "ETH_ADDR_FMT" on port #%u",
                          __FUNCTION__, ETH_ADDR_BYTES_ARGS(mac_addr),
                          netdev->port_num + 1);
                memcpy(netdev->hwaddr, ether_mac, ETH_ADDR_LEN);
            }
        }

        XP_LOCK();
        status = xpsPortGetPortIntfId(netdev->xpdev->id, netdev->port_num,
                                      &netdev->ifId);
        if (status != XP_NO_ERR) {
            XP_UNLOCK();
            VLOG_ERR("%s: could not get port ifId for port #%u. Err=%d",
                     __FUNCTION__, netdev->port_num, status);
            goto error;
        }

        status = xpsPortGetConfig(netdev->xpdev->id, netdev->ifId,
                                  &port_config);
        if (status != XP_NO_ERR) {
            XP_UNLOCK();
            VLOG_ERR("%s: could not set port config for port #%u. Err=%d",
                     __FUNCTION__, netdev->ifId, status);
            goto error;
        }
        XP_UNLOCK();

        netdev->vif = port_config.ingressVif;

        /* Get the port_info struct for a given hardware unit & port number. */
        port_info = ops_xp_dev_get_port_info(netdev->xpdev->id, netdev->port_num);
        if (NULL == port_info) {
            VLOG_ERR("Unable to get port info struct for "
                     "Interface=%s, devId=%d, port_num=%d",
                     netdev_get_name(&(netdev->up)), netdev->xpdev->id, netdev->port_num);
            goto error;
        }

        /* Save the port_info porinter in netdev struct. */
        netdev->port_info = port_info;

        /* For all the ports that can be split into multiple
         * subports, 'split_4' property is set to true.
         * This is set only on the parent ports. */
        if (STR_EQ(is_splittable, INTERFACE_HW_INTF_INFO_MAP_SPLIT_4_TRUE)) {
            netdev->is_split_parent = true;
            port_info->split_port_count = XP_MAX_CHAN_PER_MAC;
        } else {
            /* For all the split children ports 'split_parent'
             * property is set to the name of the parent port.
             * This is done in subsystem.c file. */
            if (split_parent != NULL) {
                netdev->is_split_subport = true;

                /* Get parent ports netdev struct. */
                p_netdev_ = netdev_from_name(split_parent);
                if (p_netdev_ != NULL) {
                    p_netdev = netdev_xpliant_cast(p_netdev_);

                    /* Save pointer to parent port's port_info struct. */
                    netdev->parent_port_info = p_netdev->port_info;

                    /* netdev_from_name() opens a reference, so we need to close it here. */
                    netdev_close(p_netdev_);

                } else {
                    VLOG_ERR("Unable to find the netdev for the parent port. "
                             "intf_name=%s parent_name=%s",
                             netdev_get_name(&(netdev->up)), split_parent);
                    goto error;
                }
            }
        }

        /* Add the port to the default VLAN. This will allow it to trap LACP
         * frames in case it becomes member of a dynamic LAG.*/
        rc = ops_xp_vlan_member_add(netdev->xpdev->vlan_mgr,
                                    XP_DEFAULT_VLAN_ID, netdev->ifId,
                                    XP_L2_ENCAP_DOT1Q_UNTAGGED, 0);
        if (rc != XP_NO_ERR) {
            VLOG_ERR("%s: could not add interface: %u to default vlan: %d"
                     "Err=%d", __FUNCTION__, netdev->ifId,
                     XP_DEFAULT_VLAN_ID, rc);
        }

        rc = ops_xp_host_if_create(netdev->xpdev, (char *)netdev_get_name(&(netdev->up)),
                                   netdev->ifId, ether_mac, &netdev->xpnet_if_id);

        if (rc) {
            VLOG_ERR("Failed to initialize interface %s", netdev_get_name(&(netdev->up)));
        } else {
            netdev->intf_initialized = true;
        }
    }

    ovs_mutex_unlock(&netdev->mutex);
    return 0;

error:
    ovs_mutex_unlock(&netdev->mutex);

    rc = EINVAL;
    return rc;
}

static void
get_interface_autoneg_config(const char *autoneg_cfg, int *autoneg)
{
    /* Auto negotiation configuration. */
    if (STR_EQ(autoneg_cfg, INTERFACE_HW_INTF_CONFIG_MAP_AUTONEG_ON)) {
        *autoneg = true;
    } else {
        *autoneg = false;
    }
}

static void
get_interface_speed_config(const char *speed_cfg, int *speed)
{
    /* Speed configuration. */
    if (sscanf(speed_cfg, "%d,", speed) != 1) {
        /* Set 40G as default speed */
        *speed = 40000;
    }
}

static void
get_interface_duplex_config(const char *duplex_cfg, int *duplex)
{
    /* Duplex configuration. */
    if (STR_EQ(duplex_cfg, INTERFACE_HW_INTF_CONFIG_MAP_DUPLEX_FULL)) {
        *duplex = 1;
    } else {
        *duplex = 0;
    }
}

static void
get_interface_pause_config(const char *pause_cfg, int *pause_rx, int *pause_tx)
{
    *pause_rx = false;
    *pause_tx = false;

        /* Pause configuration. */
    if (STR_EQ(pause_cfg, INTERFACE_HW_INTF_CONFIG_MAP_PAUSE_RX)) {
        *pause_rx = true;
    } else if (STR_EQ(pause_cfg, INTERFACE_HW_INTF_CONFIG_MAP_PAUSE_TX)) {
        *pause_tx = true;
    } else if (STR_EQ(pause_cfg, INTERFACE_HW_INTF_CONFIG_MAP_PAUSE_RXTX)) {
        *pause_rx = true;
        *pause_tx = true;
    }
}

/* Compare the existing port configuration,
 * and check if anything changed. */
static int
is_port_config_changed(const struct port_cfg *cur_pcfg, const struct port_cfg *pcfg)
{
    if ((cur_pcfg->enable != pcfg->enable) ||
        (cur_pcfg->autoneg != pcfg->autoneg) ||
        (cur_pcfg->speed != pcfg->speed) ||
        (cur_pcfg->duplex != pcfg->duplex) ||
        (cur_pcfg->pause_rx != pcfg->pause_rx) ||
        (cur_pcfg->pause_tx != pcfg->pause_tx) ||
        (cur_pcfg->mtu != pcfg->mtu)) {

        return 1;
    }

    return 0;
}

static void
handle_xp_host_port_filters(struct netdev_xpliant *netdev, int enable)
{
    if ((enable == true) && (netdev->xpnet_port_filter_id == 0)) {
        ops_xp_host_port_filter_create((char *)netdev_get_name(&(netdev->up)),
                                       netdev->xpdev, netdev->ifId,
                                       netdev->xpnet_if_id,
                                       &netdev->xpnet_port_filter_id);
    } else if ((enable == false) && (netdev->xpnet_port_filter_id != 0)) {
        ops_xp_host_filter_delete((char *)netdev_get_name(&(netdev->up)),
                                  netdev->xpdev, netdev->xpnet_port_filter_id);
        netdev->xpnet_port_filter_id = 0;
    }
}

static xpMacConfigMode
get_parent_mac_mode(int speed)
{
    xpMacConfigMode mode = MAC_MODE_1X40GB;

    /* Parent MAC mode must be one of 1x only */
    switch (speed) {
    case 10000:
        mode = MAC_MODE_1X10GB;
        break;
    case 40000:
        mode = MAC_MODE_1X40GB;
        break;
    case 50000:
        mode = MAC_MODE_1X50GB;
        break;
    case 100000:
        mode = MAC_MODE_1X100GB;
        break;
    default:
        break;
    }

    VLOG_INFO("%s: Select MAC mode %u based on speed %d",
              __FUNCTION__, mode, speed);

    return mode;
}

static int
netdev_xpliant_set_hw_intf_config(struct netdev *netdev_, const struct smap *args)
{
    struct netdev_xpliant *netdev = netdev_xpliant_cast(netdev_);
    struct port_cfg pcfg;
    XP_STATUS xp_rc = XP_NO_ERR;
    xpMacConfigMode mac_mode = MAC_MODE_4X10GB;
    int rc = EINVAL;

    const bool hw_enable = smap_get_bool(args, INTERFACE_HW_INTF_CONFIG_MAP_ENABLE, false);
    const char *autoneg = smap_get(args, INTERFACE_HW_INTF_CONFIG_MAP_AUTONEG);
    const char *speeds = smap_get(args, INTERFACE_HW_INTF_CONFIG_MAP_SPEEDS);
    const char *duplex = smap_get(args, INTERFACE_HW_INTF_CONFIG_MAP_DUPLEX);
    const char *pause = smap_get(args, INTERFACE_HW_INTF_CONFIG_MAP_PAUSE);
    const int mtu = smap_get_int(args, INTERFACE_HW_INTF_CONFIG_MAP_MTU, 0);

    VLOG_INFO("%s call for %s.\n", __FUNCTION__, netdev_get_name(netdev_));

    if (netdev->intf_initialized == false) {
        VLOG_WARN("%s: netdev interface %s is not initialized.",
                  __FUNCTION__, netdev_get_name(&(netdev->up)));
        return EPERM;
    }

    memset(&pcfg, 0x0, sizeof(struct port_cfg));

    /* If interface is enabled */
    if (hw_enable) {

        pcfg.enable = true;

        get_interface_autoneg_config(autoneg, &(pcfg.autoneg));
        get_interface_speed_config(speeds, &(pcfg.speed));
        get_interface_duplex_config(duplex, &(pcfg.duplex));
        get_interface_pause_config(pause, &(pcfg.pause_rx), &(pcfg.pause_tx));
        pcfg.mtu = mtu;

    } else {
        /* Treat the absence of hw_enable info as a "disable" action. */
        pcfg.enable = false;
    }

    if (!is_port_config_changed(&(netdev->pcfg), &pcfg)) {
        return 0;
    }

    VLOG_INFO("Intf=%s, port=%d config is changed",
              netdev_get_name(&(netdev->up)), netdev->port_num);

    ovs_mutex_lock(&netdev->mutex);

    /* Splittable port lane configuration. */
    if (pcfg.enable == true) {
        if (netdev->is_split_parent) {
            /* Select parent mac mode based on configured speed */
            mac_mode = get_parent_mac_mode(pcfg.speed);
            ops_xp_port_mac_mode_set(netdev->port_info, mac_mode);
        } else if (netdev->is_split_subport) {
            /* Subport of splittable port */
            ops_xp_port_mac_mode_set(netdev->parent_port_info, mac_mode);
        } else {
            /* Port is not splittable */
            ops_xp_port_mac_mode_set(netdev->port_info, mac_mode);
        }
    }

    handle_xp_host_port_filters(netdev, pcfg.enable);

    /* Apply port configuration */
    rc = ops_xp_port_set_config(netdev, &pcfg);
    if (rc) {
        VLOG_WARN("Failed to configure netdev interface %s.", netdev_get_name(&(netdev->up)));
    }

    netdev_change_seq_changed(netdev_);

    ovs_mutex_unlock(&netdev->mutex);

    return 0;

error:
    ovs_mutex_unlock(&netdev->mutex);

    return rc;
}

static int
netdev_xpliant_set_etheraddr(struct netdev *netdev_,
                             const struct eth_addr mac)
{
    struct netdev_xpliant *netdev = netdev_xpliant_cast(netdev_);

    ovs_mutex_lock(&netdev->mutex);

    if (memcmp(netdev->hwaddr, mac.ea, ETH_ADDR_LEN)) {
        VLOG_INFO("%s: Set MAC "ETH_ADDR_FMT" on netdev %s",
                  __FUNCTION__, ETH_ADDR_ARGS(mac),
                  netdev_get_name(netdev_));
        memcpy(netdev->hwaddr, mac.ea, ETH_ADDR_LEN);
        netdev_change_seq_changed(netdev_);
    }

    ovs_mutex_unlock(&netdev->mutex);

    return 0;
}

static int
netdev_xpliant_get_etheraddr(const struct netdev *netdev_,
                             struct eth_addr *mac)
{
    struct netdev_xpliant *netdev = netdev_xpliant_cast(netdev_);

    ovs_mutex_lock(&netdev->mutex);
    memcpy(mac->ea, netdev->hwaddr, ETH_ADDR_LEN);
    ovs_mutex_unlock(&netdev->mutex);

    return 0;
}

static int
netdev_xpliant_get_mtu(const struct netdev *netdev_, int *mtup)
{
    struct netdev_xpliant *netdev = netdev_xpliant_cast(netdev_);

    ovs_mutex_lock(&netdev->mutex);
    *mtup = netdev->pcfg.mtu;
    ovs_mutex_unlock(&netdev->mutex);

    return 0;
}

static int
netdev_xpliant_get_carrier(const struct netdev *netdev_, bool *carrier)
{
    struct netdev_xpliant *netdev = netdev_xpliant_cast(netdev_);
    /*XP_TRACE_RL(poll_rl);*/
    ovs_mutex_lock(&netdev->mutex);
    *carrier = netdev->link_status;
    ovs_mutex_unlock(&netdev->mutex);

    return 0;
}

static long long int
netdev_xpliant_get_carrier_resets(const struct netdev *netdev_)
{
    struct netdev_xpliant *netdev = netdev_xpliant_cast(netdev_);
    long long int link_resets = 0;
    /*XP_TRACE_RL(poll_rl);*/
    ovs_mutex_lock(&netdev->mutex);
    link_resets = netdev->link_resets;
    ovs_mutex_unlock(&netdev->mutex);

    return link_resets;
}

static int
netdev_xpliant_update_flags(struct netdev *netdev_,
                            enum netdev_flags off,
                            enum netdev_flags on,
                            enum netdev_flags *old_flagsp)
{
    struct netdev_xpliant *netdev = netdev_xpliant_cast(netdev_);
    int rc = 0;
    bool state = false;

    if ((off | on) & ~NETDEV_UP) {
        return EOPNOTSUPP;
    }

    ovs_mutex_lock(&netdev->mutex);
    /* Check for if netdev is enabled  */
    if (netdev->pcfg.enable == false) {
        /* Skip disabled netdev */
        ovs_mutex_unlock(&netdev->mutex);
        return EOPNOTSUPP;
    }

    /* Get the current state to update the old flags. */
    rc = ops_xp_port_get_enable(netdev->xpdev->id, netdev->port_num, &state);
    if (!rc) {
        if (state) {
            *old_flagsp |= NETDEV_UP;
        } else {
            *old_flagsp &= ~NETDEV_UP;
        }

        /* Set the new state to that which is desired. */
        if (on & NETDEV_UP) {
            rc = ops_xp_port_set_enable(netdev->xpdev->id,
                                        netdev->port_num, true);
        } else if (off & NETDEV_UP) {
            rc = ops_xp_port_set_enable(netdev->xpdev->id,
                                        netdev->port_num, false);
        }
    }

    ovs_mutex_unlock(&netdev->mutex);

    return rc;
}

static int
netdev_xpliant_get_stats(const struct netdev *netdev_,
                         struct netdev_stats *stats)
{
    struct netdev_xpliant *netdev = netdev_xpliant_cast(netdev_);
    struct xp_Statistics xpStat;
    int rc = 0;
    XP_STATUS ret = XP_NO_ERR;

    memset(stats, 0xFF, sizeof(*stats));

    ovs_mutex_lock(&netdev->mutex);
    /* Check for if netdev is enabled  */
    if (netdev->pcfg.enable == false) {
        /* Skip disabled netdev */
        ovs_mutex_unlock(&netdev->mutex);
        return 0;
    }

#ifndef OPS_XP_SIM
    XP_LOCK();
    ret = xpsMacGetCounterStats(netdev->xpdev->id, netdev->port_num,
            0, (uint8_t)((sizeof(xpStat) / sizeof(xpStat.frameRxAll)) - 1),
            &xpStat);
    XP_UNLOCK();
#endif
    if (ret != XP_NO_ERR) {
        VLOG_WARN("unable to get stats on %s. RC = %u",
                  netdev_get_name(netdev_), ret);
        rc = EOPNOTSUPP;
    } else {
        stats->rx_packets = xpStat.frameRxAll;
        stats->tx_packets = xpStat.frameTransmittedAll;
        stats->rx_bytes = xpStat.octetsRx;
        stats->tx_bytes = xpStat.octetsTransmittedTotal;
        stats->rx_errors = xpStat.frameRxAnyErr;
        stats->tx_errors = xpStat.frameTransmittedWithErr;
        stats->multicast = xpStat.frameRxMulticastAddr;
        stats->rx_length_errors = xpStat.frameRxLengthErr;
        stats->rx_crc_errors = xpStat.frameRxFcsErr;
        stats->rx_fifo_errors = xpStat.frameDroppedFromRxFIFOFullCondition;
    }

    ovs_mutex_unlock(&netdev->mutex);

    return rc;
}

static int
netdev_xpliant_get_features(const struct netdev *netdev_,
                            enum netdev_features *current,
                            enum netdev_features *advertised,
                            enum netdev_features *supported,
                            enum netdev_features *peer)
{
    int ret;
    uint8_t pause = 0;
    uint8_t autoNeg = 0;
    xpSpeed speed = SPEED_MAX_VAL;
    struct netdev_xpliant *netdev = netdev_xpliant_cast(netdev_);

    *current = 0;
    *advertised = 0;
    *supported = 0;

    ovs_mutex_lock(&netdev->mutex);

    /* Current settings. */
    if (netdev->link_status) {
        XP_LOCK();
        ret = xpsMacGetPortSpeed(netdev->xpdev->id, netdev->port_num, &speed);
        XP_UNLOCK();
        if (XP_NO_ERR != ret) {
            VLOG_WARN("unable to get speed on %s. RC = %u",
                      netdev_get_name(netdev_), ret);
            ovs_mutex_unlock(&netdev->mutex);
            return EOPNOTSUPP;
        }
    }

    if (speed == SPEED_10MB) {
        *current |= NETDEV_F_10MB_FD;
    } else if (speed == SPEED_100MB) {
        *current |= NETDEV_F_100MB_FD;
    } else if (speed == SPEED_1GB) {
        *current |= NETDEV_F_1GB_FD;
    } else if (speed == SPEED_10GB) {
        *current |= NETDEV_F_10GB_FD;
    } else if (speed == SPEED_40GB) {
        *current |= NETDEV_F_40GB_FD;
    } else if (speed == SPEED_100GB) {
        *current |= NETDEV_F_100GB_FD;
    } else {
        *current |= NETDEV_F_OTHER;
    }

    ovs_mutex_unlock(&netdev->mutex);

    return 0;
}

void
ops_xp_netdev_link_state_callback(struct netdev_xpliant *netdev,
                                  int link_status)
{
    ovs_mutex_lock(&netdev->mutex);

    netdev->link_status = !!link_status;

    if (link_status) {
        netdev->link_resets++;
    } else {
        /* Notify ML that port is down */
        ops_xp_mac_learning_on_port_down(netdev->xpdev->ml, netdev->ifId);
    }

    netdev_change_seq_changed(&(netdev->up));

    ovs_mutex_unlock(&netdev->mutex);
}

static int
netdev_xpliant_set_policing(struct netdev *netdev, unsigned int kbits_rate,
                            unsigned int kbits_burst)
{
    struct netdev_xpliant *dev = netdev_xpliant_cast(netdev);
    xpsPolicerEntry_t polEntry = {};
    XP_STATUS ret = XP_NO_ERR;

    XP_LOCK();

    polEntry.pbs = kbits_burst;
    polEntry.pir = kbits_rate;
    polEntry.cbs = kbits_burst;
    polEntry.cir = kbits_rate;
    polEntry.dropYellow = 0;
    polEntry.dropRed = 1;
    polEntry.colorAware = 0;
    polEntry.updateResultGreen = 0;
    polEntry.updateResultYellow = 0;
    polEntry.updateResultRed = 0;
    polEntry.polResult = 0;

    ret = xpsPolicerAddPortPolicingEntry(dev->ifId, XP_ACM_POLICING, &polEntry);

    XP_UNLOCK();

    if (XP_NO_ERR != ret) {
        VLOG_WARN("unable to set policing on %s. RC = %u",
                  netdev_get_name(netdev), ret);
        return EOPNOTSUPP;
    }

    dev->kbits_rate = kbits_rate;
    dev->kbits_burst = kbits_burst;
    return 0;
}

static int
netdev_xpliant_get_qos_types(const struct netdev *netdev, struct sset *types)
{
    struct netdev_xpliant *dev = netdev_xpliant_cast(netdev);

    XP_TRACE_RL(poll_rl);

    /* We support only one QoS type */
    sset_add(types, dev->qos_type);

    return 0;
}

static int
netdev_xpliant_get_qos_capabilities(const struct netdev *netdev,
                                    const char *type,
                                    struct netdev_qos_capabilities *caps)
{
    struct netdev_xpliant *dev = netdev_xpliant_cast(netdev);

    XP_TRACE_RL(poll_rl);

    caps->n_queues = dev->n_queues;

    return 0;
}

static int
netdev_xpliant_get_qos(const struct netdev *netdev,
                       const char **typep, struct smap *details)
{
    XP_TRACE_RL(poll_rl);

    /* The the only supported QoS type is the same as netdev one. */
    *typep = netdev_get_type(netdev);

    return 0;
}

static int
netdev_xpliant_set_qos(struct netdev *netdev,
                       const char *type, const struct smap *details)
{
    struct netdev_xpliant *dev = netdev_xpliant_cast(netdev);

    XP_TRACE();

    if (!STR_EQ(type, dev->qos_type)) {
        VLOG_WARN("%s does not support %s type of QoS on",
                  netdev_get_name(netdev), type);
        return EOPNOTSUPP;
    }

    return 0;
}

static int
netdev_xpliant_get_queue(const struct netdev *netdev,
                         unsigned int queue_id, struct smap *details)
{
    struct netdev_xpliant *dev = netdev_xpliant_cast(netdev);
    int ret = 0;

    XP_TRACE_RL(poll_rl);

    ovs_mutex_lock(&dev->mutex);
    ret = xpdev_get_queue(dev, queue_id, details);
    ovs_mutex_unlock(&dev->mutex);

    return ret;
}

static int
netdev_xpliant_set_queue(struct netdev *netdev,
                         unsigned int queue_id, const struct smap *details)
{
    struct netdev_xpliant *dev = netdev_xpliant_cast(netdev);
    const char *rate_kbps_s = smap_get(details, "rate-kbps");
    const char *max_burst_s = smap_get(details, "max-burst");
    const char *priority_s = smap_get(details, "priority");
    uint32_t rate_kbps = rate_kbps_s ? strtoul(rate_kbps_s, NULL, 10) : 0;
    uint32_t max_burst = max_burst_s ? strtoul(max_burst_s, NULL, 10) : 0;
    uint32_t priority = priority_s ? strtoul(priority_s, NULL, 10) : 0;
    XP_STATUS ret = XP_NO_ERR;

    XP_TRACE();

    XP_LOCK();

    ret = xpsQosFcSetPfcPriority(dev->xpdev->id, dev->port_num, queue_id,
                                 priority);

    XP_UNLOCK();

    if (XP_NO_ERR != ret) {
        VLOG_WARN("unable to set queue %d on %s priority RC = %u",
                  queue_id, netdev_get_name(netdev), ret);
        return EOPNOTSUPP;
    }
    XP_LOCK()

    ret = xpsQosShaperConfigurePortShaper(dev->xpdev->id, dev->port_num,
                                          rate_kbps, max_burst);

    XP_UNLOCK();

    if (XP_NO_ERR != ret) {
        VLOG_WARN("unable to set queue %d on %s. RC = %u",
                  queue_id, netdev_get_name(netdev), ret);
        return EOPNOTSUPP;
    }

    XP_LOCK();

    ret = xpsQosShaperSetPortShaperEnable(dev->xpdev->id, dev->port_num, 1);

    XP_UNLOCK();

    if (XP_NO_ERR != ret) {
        VLOG_WARN("unable to set queue %d on %s. RC = %u",
                  queue_id, netdev_get_name(netdev), ret);
        return EOPNOTSUPP;
    }

    ovs_mutex_lock(&dev->mutex);

    struct xpdev_queue *queue = xpdev_find_queue(dev, queue_id);

    if (queue) {
        queue->rate_kbps = rate_kbps;
        queue->max_burst = max_burst;
        queue->priority  = priority;

        ovs_mutex_unlock(&dev->mutex);
        return 0;
    }

    ovs_mutex_unlock(&dev->mutex);

    VLOG_WARN("could not find queue %d entry on %s",
              queue_id, netdev_get_name(netdev));

    return ENOENT;
}

static int
netdev_xpliant_delete_queue(struct netdev *netdev, unsigned int queue_id)
{
    int ret = 0;
    struct netdev_xpliant *dev = netdev_xpliant_cast(netdev);

    XP_TRACE();

    XP_LOCK();
    ret = xpsQosShaperSetPortShaperEnable(dev->xpdev->id, dev->port_num, 0);
    XP_UNLOCK();

    if (XP_NO_ERR != ret) {
        VLOG_WARN("unable to set queue %d on %s. RC = %u",
                  queue_id, netdev_get_name(netdev), ret);

        return EOPNOTSUPP;
    }

    return 0;
}

static int
netdev_xpliant_get_queue_stats(const struct netdev *netdev_,
                               unsigned int queue_id,
                               struct netdev_queue_stats *stats)
{
    struct netdev_xpliant *netdev = netdev_xpliant_cast(netdev_);
    struct xpdev_queue *queue;
    uint32_t wrap;
    uint64_t count = UINT64_MAX;
    XP_STATUS status = XP_NO_ERR;

    XP_TRACE_RL(poll_rl);

    /* Any unsupported counts shall
     * be returned with all one bits
     * (UINT64_MAX) */
    stats->tx_bytes   = UINT64_MAX;
    stats->tx_packets = UINT64_MAX;
    stats->tx_errors  = UINT64_MAX;

    ovs_mutex_lock(&netdev->mutex);
    /* Check for if netdev is enabled  */
    if (netdev->pcfg.enable == false) {
        /* Skip disabled netdev */
        ovs_mutex_unlock(&netdev->mutex);
        return EOPNOTSUPP;
    }

    queue = xpdev_find_queue(netdev, queue_id);

    if (queue) {
        stats->created = queue->created;
    } else {
        VLOG_DBG("Failed to Fetch queue details for q_id %d port: %s",
                  queue_id, netdev_get_name(netdev_));
    }

    XP_LOCK();

    status = xpsQosQcGetQueueFwdByteCountForPort(netdev->xpdev->id,
                                                 netdev->port_num,
                                                 queue_id, &count, &wrap);

    if (XP_NO_ERR != status) {
        /* No Error is returned, UINT64_MAX is assigned */
        VLOG_DBG("unable to get queue %d Forward Byte Count for %s. RC = %u",
                  queue_id, netdev_get_name(netdev_), status);
    } else {
        stats->tx_bytes = count;
    }

    status = xpsQosQcGetQueueFwdPacketCountForPort(netdev->xpdev->id,
                                                   netdev->port_num,
                                                   queue_id, &count, &wrap);

    if (XP_NO_ERR != status) {
        /* No Error is returned, UINT64_MAX is assigned */
        VLOG_DBG("unable to get queue %d Forward Count for %s. RC = %u",
                   queue_id, netdev_get_name(netdev_), status);
    } else {
        stats->tx_packets = count;
    }

    status = xpsQosQcGetQueueDropPacketCountForPort(netdev->xpdev->id,
                                                    netdev->port_num,
                                                    queue_id, &count, &wrap);

    if (XP_NO_ERR != status) {
        /* No Error is returned, UINT64_MAX is assigned */
        VLOG_DBG("unable to get queue %d Drop Count for %s. RC = %u",
                   queue_id, netdev_get_name(netdev_), status);
    } else {
        /* Considering tx_drops as tx_errors. As number of packets
         * that were either unable to be queued (tail dropped),
         * discarded, or were unsuccessfully transmitted  from the
         * queue are considered as tx_errors */
        stats->tx_errors = count;
    }

    XP_UNLOCK();

    ovs_mutex_unlock(&netdev->mutex);

    return 0;
}

static int
netdev_xpliant_queue_dump_start(const struct netdev *netdev,
                                void **statep)
{
    struct netdev_xpliant *dev = netdev_xpliant_cast(netdev);
    struct xpdev_queue_state *state = xmalloc(sizeof *state);
    struct xpdev_queue *queue;
    size_t i = 0;

    XP_TRACE();

    *statep = state;

    ovs_mutex_lock(&dev->mutex);

    state->n_queues = hmap_count(&dev->queues);
    state->cur_queue = 0;
    state->queues = xmalloc(state->n_queues * sizeof *state->queues);

    HMAP_FOR_EACH (queue, hmap_node, &dev->queues) {
        state->queues[i++] = queue->queue_id;
    }

    ovs_mutex_unlock(&dev->mutex);

    return 0;
}

static int
netdev_xpliant_queue_dump_next(const struct netdev *netdev, void *state,
                               unsigned int *queue_idp, struct smap *details)
{
    struct netdev_xpliant *dev = netdev_xpliant_cast(netdev);
    struct xpdev_queue_state *q_state = state;
    int error = EOF;

    XP_TRACE();

    ovs_mutex_lock(&dev->mutex);

    while (q_state->cur_queue < q_state->n_queues) {
        unsigned int queue_id = q_state->queues[q_state->cur_queue++];
        error = xpdev_get_queue(dev, queue_id, details);

        if (!error) {
            *queue_idp = queue_id;
            break;
        }
    }

    ovs_mutex_unlock(&dev->mutex);

    return error;
}

static int
netdev_xpliant_queue_dump_done(const struct netdev *netdev OVS_UNUSED,
                               void *state)
{
    struct xpdev_queue_state *q_state = state;

    XP_TRACE();

    free(q_state->queues);
    free(q_state);

    return 0;
}

static int
netdev_xpliant_dump_queue_stats(const struct netdev *netdev_,
                                void (*cb)(unsigned int queue_id,
                                           struct netdev_queue_stats *,
                                           void *aux),
                                void *aux)
{
    struct netdev_queue_stats stats[OPS_QOS_MAX_QUEUE];
    unsigned int queue_id;

    XP_TRACE();

    /* For each queue, retrieve the statistics for
     * Txpackets, TxBytes & TxDrops */
    for (queue_id = 0; queue_id < OPS_QOS_MAX_QUEUE; queue_id++) {
        netdev_xpliant_get_queue_stats(netdev_, queue_id, &stats[queue_id]);

        if (cb) {
            (*cb)(queue_id, &stats[queue_id], aux);
        } else {
            VLOG_DBG("NULL PI callback function for port: %s",
                      netdev_get_name(netdev_));
        }
    }

    return 0;
}

const struct netdev_class netdev_xpliant_class =
{
    "system",
    netdev_xpliant_init,
    NULL,                       /* run */
    NULL,                       /* wait */

    netdev_xpliant_alloc,
    netdev_xpliant_construct,
    netdev_xpliant_destruct,
    netdev_xpliant_dealloc,
    NULL,                       /* get_config */
    NULL,                       /* set_config */

    netdev_xpliant_set_hw_intf_info,
    netdev_xpliant_set_hw_intf_config,

    NULL,                       /* get_tunnel_config */

    NULL,                       /* build_header */
    NULL,                       /* push_header */
    NULL,                       /* pop_header */
    NULL,                       /* get_numa_id */
    NULL,                       /* set_multiq */

    NULL,                       /* send */
    NULL,                       /* send_wait */

    netdev_xpliant_set_etheraddr,
    netdev_xpliant_get_etheraddr,
    netdev_xpliant_get_mtu,
    NULL,                       /* set_mtu */
    NULL,                       /* get_ifindex */
    netdev_xpliant_get_carrier,
    netdev_xpliant_get_carrier_resets,
    NULL,                       /* set_miimon_interval */
    netdev_xpliant_get_stats,
    netdev_xpliant_get_features,
    NULL,                       /* set_advertisements */

    netdev_xpliant_set_policing,
    netdev_xpliant_get_qos_types,
    netdev_xpliant_get_qos_capabilities,
    netdev_xpliant_get_qos,
    netdev_xpliant_set_qos,
    netdev_xpliant_get_queue,
    netdev_xpliant_set_queue,
    netdev_xpliant_delete_queue,
    netdev_xpliant_get_queue_stats,
    netdev_xpliant_queue_dump_start,
    netdev_xpliant_queue_dump_next,
    netdev_xpliant_queue_dump_done,
    netdev_xpliant_dump_queue_stats,

    NULL,                       /* get_in4 */
    NULL,                       /* set_in4 */
    NULL,                       /* get_in6 */
    NULL,                       /* add_router */
    NULL,                       /* get_next_hop */
    NULL,                       /* get_status */
    NULL,                       /* arp_lookup */

    netdev_xpliant_update_flags,

    NULL,                       /* rxq_alloc */
    NULL,                       /* rxq_construct */
    NULL,                       /* rxq_destruct */
    NULL,                       /* rxq_dealloc */
    NULL,                       /* rxq_recv */
    NULL,                       /* rxq_wait */
    NULL,                       /* rxq_drain */
};

static int
netdev_xpliant_internal_get_stats(const struct netdev *netdev_, struct netdev_stats *stats)
{
    memset(stats, 0xFF, sizeof(*stats));
    return 0;
}

static int
netdev_xpliant_internal_set_hw_intf_info(struct netdev *netdev_,
                                         const struct smap *args)
{
    struct netdev_xpliant *netdev = netdev_xpliant_cast(netdev_);
    int rc = 0;
    struct ether_addr *ether_mac = NULL;
    bool is_bridge_interface = smap_get_bool(args, INTERFACE_HW_INTF_INFO_MAP_BRIDGE, DFLT_INTERFACE_HW_INTF_INFO_MAP_BRIDGE);

    VLOG_INFO("%s: netdev %s, is_bridge_interface %u",
              __FUNCTION__, netdev_get_name(netdev_), is_bridge_interface);

    ovs_mutex_lock(&netdev->mutex);

    if (netdev->intf_initialized == false) {
        if(is_bridge_interface) {
            ether_mac = (struct ether_addr *) netdev->hwaddr;
            uint32_t dev_num = 0;

            netdev->xpdev = ops_xp_dev_by_id(dev_num);
            if (netdev->xpdev == NULL) {
                VLOG_ERR("Switch unit is not initialized");
                goto error;
            }

            netdev->port_num = CPU_PORT;
			
		    netdev->tap_fd = ops_xp_tun_alloc((char *)netdev_get_name(netdev_), (IFF_TAP | IFF_NO_PI));
            if (netdev->tap_fd <= 0) {
                VLOG_ERR("Unable to create %s device.", netdev_get_name(netdev_));
                goto error;
            }

            rc = set_nonblocking(netdev->tap_fd);
            if (rc) {
                VLOG_ERR("Unable to set %s device into nonblocking mode.", netdev_get_name(netdev_));
                close(netdev->tap_fd);
                goto error;
            }

            if (0 != ops_xp_net_if_setup((char *)netdev_get_name(netdev_), ether_mac)) {
                VLOG_ERR("Unable to setup %s interface.", netdev_get_name(netdev_));
                close(netdev->tap_fd);
                goto error;
            }
        }

        netdev->intf_initialized = true;
    }

    ovs_mutex_unlock(&netdev->mutex);

    return 0;

error:
    ovs_mutex_unlock(&netdev->mutex);

    rc = EINVAL;
    return rc;
}

static int
netdev_xpliant_internal_set_hw_intf_config(struct netdev *netdev_,
                                           const struct smap *args)
{
    struct netdev_xpliant *netdev = netdev_xpliant_cast(netdev_);
    const char *hw_enable = smap_get(args, INTERFACE_HW_INTF_CONFIG_MAP_ENABLE);

    VLOG_INFO("%s: netdev %s\n", __FUNCTION__, netdev_get_name(netdev_));

    if (netdev->intf_initialized == false) {
        VLOG_WARN("%s: netdev interface %s is not initialized.",
                  __FUNCTION__, netdev_get_name(&(netdev->up)));
        return EPERM;
    }

    ovs_mutex_lock(&netdev->mutex);

    /* If interface is enabled */
    if (STR_EQ(hw_enable, INTERFACE_HW_INTF_CONFIG_MAP_ENABLE_TRUE)) {
        netdev->flags |= NETDEV_UP;
        netdev->link_status = true;
    } else {
        netdev->flags &= ~NETDEV_UP;
        netdev->link_status = false;
    }

    netdev_change_seq_changed(netdev_);

    ovs_mutex_unlock(&netdev->mutex);
    return 0;
}

static int
netdev_xpliant_internal_update_flags(struct netdev *netdev_,
                                     enum netdev_flags off,
                                     enum netdev_flags on,
                                     enum netdev_flags *old_flagsp)
{
    /*  We ignore the incoming flags as the underlying hardware responsible to
     *  change the status of the flags is absent. Thus, we set new flags to
     *  preconfigured values. */
    struct netdev_xpliant *netdev = netdev_xpliant_cast(netdev_);
    if ((off | on) & ~NETDEV_UP) {
        return EOPNOTSUPP;
    }

    ovs_mutex_lock(&netdev->mutex);
    *old_flagsp = netdev->flags;
    ovs_mutex_unlock(&netdev->mutex);

    return 0;
}

static const struct netdev_class netdev_xpliant_internal_class = {
    "internal",
    NULL,                       /* init */
    NULL,                       /* run */
    NULL,                       /* wait */

    netdev_xpliant_alloc,
    netdev_xpliant_construct,
    netdev_xpliant_destruct,
    netdev_xpliant_dealloc,
    NULL,                       /* get_config */
    NULL,                       /* set_config */
    netdev_xpliant_internal_set_hw_intf_info,
    netdev_xpliant_internal_set_hw_intf_config,
    NULL,                       /* get_tunnel_config */
    NULL,                       /* build header */
    NULL,                       /* push header */
    NULL,                       /* pop header */
    NULL,                       /* get_numa_id */
    NULL,                       /* set_multiq */

    NULL,                       /* send */
    NULL,                       /* send_wait */

    netdev_xpliant_set_etheraddr,
    netdev_xpliant_get_etheraddr,
    NULL,                       /* get_mtu */
    NULL,                       /* set_mtu */
    NULL,                       /* get_ifindex */
    netdev_xpliant_get_carrier,
    NULL,                       /* get_carrier_resets */
    NULL,                       /* get_miimon */
    netdev_xpliant_internal_get_stats,

    NULL,                       /* get_features */
    NULL,                       /* set_advertisements */

    NULL,                       /* set_policing */
    NULL,                       /* get_qos_types */
    NULL,                       /* get_qos_capabilities */
    NULL,                       /* get_qos */
    NULL,                       /* set_qos */
    NULL,                       /* get_queue */
    NULL,                       /* set_queue */
    NULL,                       /* delete_queue */
    NULL,                       /* get_queue_stats */
    NULL,                       /* queue_dump_start */
    NULL,                       /* queue_dump_next */
    NULL,                       /* queue_dump_done */
    NULL,                       /* dump_queue_stats */

    NULL,                       /* get_in4 */
    NULL,                       /* set_in4 */
    NULL,                       /* get_in6 */
    NULL,                       /* add_router */
    NULL,                       /* get_next_hop */
    NULL,                       /* get_status */
    NULL,                       /* arp_lookup */

    netdev_xpliant_internal_update_flags,

    NULL,                       /* rxq_alloc */
    NULL,                       /* rxq_construct */
    NULL,                       /* rxq_destruct */
    NULL,                       /* rxq_dealloc */
    NULL,                       /* rxq_recv */
    NULL,                       /* rxq_wait */
    NULL,                       /* rxq_drain */
};

void
ops_xp_netdev_get_subintf_vlan(struct netdev *netdev_, xpsVlan_t *vlan)
{
    struct netdev_xpliant *netdev = netdev_xpliant_cast(netdev_);

    VLOG_DBG("get subinterface vlan as %u\n", netdev->subintf_vlan_id);
    *vlan = netdev->subintf_vlan_id;
}

static int
netdev_xpliant_subintf_set_config(struct netdev *netdev_, const struct smap *args)
{
    struct netdev_xpliant *netdev = netdev_xpliant_cast(netdev_);
    struct netdev *parent = NULL;
    struct netdev_xpliant *parent_netdev = NULL;
    const char *parent_intf_name = NULL;
    int vlan_id = 0;

    ovs_mutex_lock(&netdev->mutex);
    parent_intf_name = smap_get(args, "parent_intf_name");
    vlan_id = smap_get_int(args, "vlan", 0);

    if ((vlan_id != 0) && (parent_intf_name != NULL)) {
        VLOG_DBG("netdev set_config gets info for parent interface %s, and vlan = %d",
                 parent_intf_name, vlan_id);
        parent = netdev_from_name(parent_intf_name);
        if (parent != NULL) {
            parent_netdev = netdev_xpliant_cast(parent);
            if (netdev->xpdev == NULL) {
                netdev->xpdev = ops_xp_dev_ref(parent_netdev->xpdev);
            }
            netdev->ifId = parent_netdev->ifId;
            memcpy(netdev->hwaddr, parent_netdev->hwaddr, ETH_ADDR_LEN);
            netdev->subintf_vlan_id = (xpsVlan_t)vlan_id;
            if (netdev->subintf_parent_name == NULL) {
                netdev->subintf_parent_name = xstrdup(parent_intf_name);
            }
            netdev_close(parent);
        }
    }

    ovs_mutex_unlock(&netdev->mutex);

    return 0;
}

static int
netdev_xpliant_subintf_update_flags(struct netdev *netdev_,
                                    enum netdev_flags off,
                                    enum netdev_flags on,
                                    enum netdev_flags *old_flagsp)
{
    /*  We ignore the incoming flags as the underlying hardware responsible to
     *  change the status of the flags is absent. Thus, we set new flags to
     *  preconfigured values. */
    struct netdev_xpliant *netdev = netdev_xpliant_cast(netdev_);
    struct netdev *parent = NULL;
    struct netdev_xpliant *parent_netdev = NULL;
    enum netdev_flags parent_flagsp = 0;
    bool state = false;
    int rc = 0;

    VLOG_DBG("%s: netdev %s", __FUNCTION__, netdev_get_name(netdev_));

    if ((off | on) & ~NETDEV_UP) {
        return EOPNOTSUPP;
    }

    /* Use subinterface netdev to get the parent netdev by name */
    if (netdev->subintf_parent_name != NULL) {
        parent = netdev_from_name(netdev->subintf_parent_name);
        if (parent != NULL) {
            parent_netdev = netdev_xpliant_cast(parent);

            VLOG_DBG("%s: port_get_enable for netdev %s", __FUNCTION__,
                     netdev_get_name(parent));

            ovs_mutex_lock(&parent_netdev->mutex);

            rc = ops_xp_port_get_enable(parent_netdev->xpdev->id,
                                        parent_netdev->port_num, &state);
            if (!rc) {
                if (state) {
                    parent_flagsp |= NETDEV_UP;
                } else {
                    parent_flagsp &= ~NETDEV_UP;
                }
            }
            /* Close netdev reference */
            netdev_close(parent);
            ovs_mutex_unlock(&parent_netdev->mutex);
        }
    }
    VLOG_DBG("%s parent_flagsp = %d", __FUNCTION__, parent_flagsp);

    ovs_mutex_lock(&netdev->mutex);
    VLOG_DBG("%s flagsp = %d", __FUNCTION__, netdev->flags);
    *old_flagsp = netdev->flags & parent_flagsp;
    ovs_mutex_unlock(&netdev->mutex);

    return 0;
}

static const struct netdev_class netdev_xpliant_subintf_class = {
    "vlansubint",
    NULL,                       /* init */
    NULL,                       /* run */
    NULL,                       /* wait */

    netdev_xpliant_alloc,
    netdev_xpliant_construct,
    netdev_xpliant_destruct,
    netdev_xpliant_dealloc,
    NULL,                       /* get_config */
    netdev_xpliant_subintf_set_config,
    netdev_xpliant_internal_set_hw_intf_info,
    netdev_xpliant_internal_set_hw_intf_config,
    NULL,                       /* get_tunnel_config */
    NULL,                       /* build header */
    NULL,                       /* push header */
    NULL,                       /* pop header */
    NULL,                       /* get_numa_id */
    NULL,                       /* set_multiq */

    NULL,                       /* send */
    NULL,                       /* send_wait */

    netdev_xpliant_set_etheraddr,
    netdev_xpliant_get_etheraddr,
    NULL,                       /* get_mtu */
    NULL,                       /* set_mtu */
    NULL,                       /* get_ifindex */
    netdev_xpliant_get_carrier,
    NULL,                       /* get_carrier_resets */
    NULL,                       /* get_miimon */
    NULL,                       /* get_stats */

    NULL,                       /* get_features */
    NULL,                       /* set_advertisements */

    NULL,                       /* set_policing */
    NULL,                       /* get_qos_types */
    NULL,                       /* get_qos_capabilities */
    NULL,                       /* get_qos */
    NULL,                       /* set_qos */
    NULL,                       /* get_queue */
    NULL,                       /* set_queue */
    NULL,                       /* delete_queue */
    NULL,                       /* get_queue_stats */
    NULL,                       /* queue_dump_start */
    NULL,                       /* queue_dump_next */
    NULL,                       /* queue_dump_done */
    NULL,                       /* dump_queue_stats */

    NULL,                       /* get_in4 */
    NULL,                       /* set_in4 */
    NULL,                       /* get_in6 */
    NULL,                       /* add_router */
    NULL,                       /* get_next_hop */
    NULL,                       /* get_status */
    NULL,                       /* arp_lookup */

    netdev_xpliant_subintf_update_flags,

    NULL,                       /* rxq_alloc */
    NULL,                       /* rxq_construct */
    NULL,                       /* rxq_destruct */
    NULL,                       /* rxq_dealloc */
    NULL,                       /* rxq_recv */
    NULL,                       /* rxq_wait */
    NULL,                       /* rxq_drain */
};

static const struct netdev_class netdev_xpliant_l3_loopback_class = {
    "loopback",
    NULL,                       /* init */
    NULL,                       /* run */
    NULL,                       /* wait */

    netdev_xpliant_alloc,
    netdev_xpliant_construct,
    netdev_xpliant_destruct,
    netdev_xpliant_dealloc,
    NULL,                       /* get_config */
    NULL,                       /* set_config */
    NULL,                       /* set_hw_intf_info */
    NULL,                       /* set_hw_intf_config */
    NULL,                       /* get_tunnel_config */
    NULL,                       /* build header */
    NULL,                       /* push header */
    NULL,                       /* pop header */
    NULL,                       /* get_numa_id */
    NULL,                       /* set_multiq */

    NULL,                       /* send */
    NULL,                       /* send_wait */

    netdev_xpliant_set_etheraddr,
    netdev_xpliant_get_etheraddr,
    NULL,                       /* get_mtu */
    NULL,                       /* set_mtu */
    NULL,                       /* get_ifindex */
    netdev_xpliant_get_carrier,
    NULL,                       /* get_carrier_resets */
    NULL,                       /* get_miimon */
    NULL,                       /* get_stats */

    NULL,                       /* get_features */
    NULL,                       /* set_advertisements */

    NULL,                       /* set_policing */
    NULL,                       /* get_qos_types */
    NULL,                       /* get_qos_capabilities */
    NULL,                       /* get_qos */
    NULL,                       /* set_qos */
    NULL,                       /* get_queue */
    NULL,                       /* set_queue */
    NULL,                       /* delete_queue */
    NULL,                       /* get_queue_stats */
    NULL,                       /* queue_dump_start */
    NULL,                       /* queue_dump_next */
    NULL,                       /* queue_dump_done */
    NULL,                       /* dump_queue_stats */

    NULL,                       /* get_in4 */
    NULL,                       /* set_in4 */
    NULL,                       /* get_in6 */
    NULL,                       /* add_router */
    NULL,                       /* get_next_hop */
    NULL,                       /* get_status */
    NULL,                       /* arp_lookup */

    netdev_xpliant_internal_update_flags,

    NULL,                       /* rxq_alloc */
    NULL,                       /* rxq_construct */
    NULL,                       /* rxq_destruct */
    NULL,                       /* rxq_dealloc */
    NULL,                       /* rxq_recv */
    NULL,                       /* rxq_wait */
    NULL,                       /* rxq_drain */
};

void
ops_xp_netdev_register(void)
{
    netdev_register_provider(&netdev_xpliant_class);
    netdev_register_provider(&netdev_xpliant_internal_class);
    netdev_register_provider(&netdev_xpliant_subintf_class);
    netdev_register_provider(&netdev_xpliant_l3_loopback_class);
}

/* Returns the netdev_xpliant with 'name' or NULL if there is none.
 *
 * The caller must free the returned netdev_xpliant with
 * netdev_xpliant_close(). */
struct netdev_xpliant *
ops_xp_netdev_from_name(const char *name)
{
    struct netdev *netdev = netdev_from_name(name);
    return netdev ? netdev_xpliant_cast(netdev) : NULL;
}

static struct xpdev_queue*
xpdev_find_queue(const struct netdev_xpliant *xpdev, unsigned int queue_id)
{
    struct xpdev_queue *queue = NULL;

    HMAP_FOR_EACH_IN_BUCKET (queue, hmap_node,
                             hash_int(queue_id, XPDEV_HASH_BASIS),
                             &xpdev->queues) {
        if (queue->queue_id == queue_id) {
            return queue;
        }
    }

    return NULL;
}

static int
xpdev_get_queue(const struct netdev_xpliant *xpdev, unsigned int queue_id,
                struct smap *details)
{
    struct xpdev_queue *queue = xpdev_find_queue(xpdev, queue_id);

    if (queue) {
        smap_add_format(details, "rate-kbps", "%ul", queue->rate_kbps);
        smap_add_format(details, "max-burst", "%ul", queue->max_burst);
        smap_add_format(details, "priority", "%ul", queue->priority);

        return 0;
    }

    return ENOENT;
}
