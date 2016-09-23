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
 * File: ops-xp-port.c
 *
 * Purpose: This file contains OpenSwitch port related application code for the
 *          Cavium/XPliant SDK.
 */

#if !defined(OPS_AS7512) && !defined(OPS_XP_SIM)
#define OPS_XP_SIM
#endif

#include <stdlib.h>
#include <string.h>

#include <openvswitch/vlog.h>

#include "ops-xp-netdev.h"
#include "ops-xp-host.h"
#include "ops-xp-vlan.h"
#include "ops-xp-port.h"
#include "openXpsPort.h"
#include "openXpsQos.h"
#include "openXpsPolicer.h"
#include "openXpsMac.h"
#include "ops-xp-dev.h"
#include "ops-xp-dev-init.h"
#include "ops-xp-mac-learning.h"

VLOG_DEFINE_THIS_MODULE(xp_port);

#define XP_PORT_LINK_POLL_INTERVAL  (1000u)


int
ops_xp_port_mac_mode_set(struct xp_port_info *p_info, xpMacConfigMode mac_mode)
{
    XP_STATUS status = XP_NO_ERR;
    struct xp_port_info *port_info;
    uint8_t mac_num;
    int rc, port_cnt, i = 0;

    /* Check whether the port has been initialized */
    if (!p_info->initialized) {
        XP_LOCK();
        status = xpsIsPortInited(p_info->id, p_info->port_num);
        XP_UNLOCK();
        p_info->initialized = (status == XP_PORT_NOT_INITED) ? false : true;

        if (!p_info->initialized) {
            XP_LOCK();
            status = xpsMacGetMacNumForPortNum(p_info->id, p_info->port_num,
                                               &mac_num);
            if (status != XP_NO_ERR) {
                XP_UNLOCK();
                VLOG_ERR("%s: unable to get MAC number for port %u. Err=%d",
                         __FUNCTION__, p_info->port_num, status);
                return EFAULT;
            }

            status = xpsMacPortGroupInit(p_info->id, mac_num, mac_mode,
                                         SPEED_MAX_VAL, 1 /*initSerdes*/,
                                         0 /*prbsMode*/, 0 /*firmwareUpload*/,
                                         MAX_FEC_MODE, 0 /*enableFEC*/);
            XP_UNLOCK();
            if (status != XP_NO_ERR) {
                VLOG_ERR("%s: unable to initialize port group %u. Err=%d",
                         __FUNCTION__, mac_num, status);
                return EFAULT;
            }
            p_info->initialized = true;
        }
    }

    if (mac_mode == p_info->port_mac_mode) {
        /* Already in the correct lane split state. */
        VLOG_DBG("Port is already in the correct MAC mode."
                 "port=%d, current mode=%u",
                 p_info->port_num, mac_mode);
        return 0;
    }

    VLOG_INFO("%s: set MAC mode -- port=%u mac_mode=%u", __FUNCTION__,
              p_info->port_num, mac_mode);

    /* Get port group */
    XP_LOCK();
    status = xpsMacGetMacNumForPortNum(p_info->id, p_info->port_num, &mac_num);
    if (status != XP_NO_ERR) {
        XP_UNLOCK();
        VLOG_ERR("%s: unable to get MAC number for port %u. Err=%d",
                 __FUNCTION__, p_info->port_num, status);
        return EFAULT;
    }

    /* Switch to new MAC mode */
    status = xpsMacSwitchMacConfigMode(p_info->id, mac_num, mac_mode,
                                       RS_FEC_MODE,
                                       (mac_mode == MAC_MODE_1X100GB));
    XP_UNLOCK();
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: unable to set %u MAC mode. Err=%d",
                 __FUNCTION__, mac_mode, status);
        return EFAULT;
    }

    if (mac_mode == MAC_MODE_4X10GB) {
        port_cnt = p_info->split_port_count;
    } else {
        port_cnt = 1;
    }

    for (i = 0; i < port_cnt; i++) {
        /* Disable ports on split/no-split */
        port_info = ops_xp_dev_get_port_info(p_info->id, p_info->port_num + i);
        if (port_info) {
            port_info->hw_enable = false;
            XP_LOCK();
            status = xpsMacPortEnable(port_info->id, port_info->port_num,
                                      port_info->hw_enable);
            if (status != XP_NO_ERR) {
                VLOG_ERR("%s: unable to set port enable. Err=%d",
                         __FUNCTION__, status);
                return EFAULT;
            }
            XP_UNLOCK();
        }
    }

    p_info->port_mac_mode = mac_mode;

    return 0;

}

/* The handler that will process port link status related activities
 * in polling mode. It cyclically polls ports for link status and
 * calls netdev link state callback if port link state changes */
void *
ops_xp_port_event_handler(void *arg)
{
    struct xpliant_dev *dev = (struct xpliant_dev *)arg;
    struct netdev_xpliant *netdev = NULL;
    XP_STATUS status;
    struct xp_port_info *port_info;
    uint8_t link_status;
#if (XP_DEV_EVENT_MODE == XP_DEV_EVENT_MODE_POLL)
    uint8_t link;
#endif /* XP_DEV_EVENT_MODE */
    uint32_t port;

    for (;;) {
        /* Sleep for a while */
        ops_xp_msleep(XP_PORT_LINK_POLL_INTERVAL);

        /* Iterate over all enabled ports */
        for (port = 0; port < XP_MAX_TOTAL_PORTS; port++) {
            netdev = ops_xp_netdev_from_port_num(dev->id, port);
            if (netdev) {
                ovs_mutex_lock(&netdev->mutex);
                /* Skip if no netdev, netdev is not initialized or netdev is
                 * disabled */
                if (!netdev->intf_initialized || !netdev->pcfg.enable) {
                    ovs_mutex_unlock(&netdev->mutex);
                    continue;
                }

                link_status = netdev->link_status;
                port_info = netdev->port_info;
                ovs_mutex_unlock(&netdev->mutex);

#if (XP_DEV_EVENT_MODE == XP_DEV_EVENT_MODE_POLL)
                XP_LOCK();
                status = xpsMacGetLinkStatus(port_info->id,
                                             port_info->port_num, &link);
                XP_UNLOCK();
                if (status != XP_NO_ERR) {
                    VLOG_WARN("%s: could not get %u port link status. Err=%d",
                              __FUNCTION__, port_info->port_num, status);
                }

                if (link_status != !!link) {
                    ops_xp_netdev_link_state_callback(netdev, link);
                }
#endif /* XP_DEV_EVENT_MODE */

                /* If port tuning has not been done yet, then try to tune the port */
                if (!port_info->serdes_tuned) {
                    XP_LOCK();
                    status = xpsMacPortSerdesTuneConditionGet(port_info->id,
                                                              port_info->port_num);
                    if (status == XP_NO_ERR) {
                        status = xpsMacPortSerdesTune(port_info->id,
                                                      &port_info->port_num, 1,
                                                      DFE_ICAL, 0);
                        if (status != XP_NO_ERR) {
                            VLOG_WARN("%s: could not tune serdes for port %u. Err=%d",
                                      __FUNCTION__, port_info->port_num, status);
                        }
                        status = xpsMacPortSerdesDfeWait(port_info->id,
                                                         port_info->port_num);
                        if (status != XP_NO_ERR) {
                            VLOG_WARN("%s: Serdes Dfe wait failed for port %u. Err=%d",
                                      __FUNCTION__, port_info->port_num, status);
                        }
                        /* Apply additional handling for 100G ports */
                        if (port_info->port_mac_mode == MAC_MODE_1X100GB) {
                            status = xpsMacPortSerdesSignalOverride(port_info->id,
                                                                    port_info->port_num, 1);
                            status = xpsMacPortSerdesSignalOverride(port_info->id,
                                                                    port_info->port_num, 0);
                        }

                        port_info->serdes_tuned = true;
                    }
                    XP_UNLOCK();
                }
            }
        }
    }

    return NULL;
}

#if (XP_DEV_EVENT_MODE == XP_DEV_EVENT_MODE_INTERRUPT)
static void
port_on_link_up_event(xpsDevice_t dev_id, uint8_t port_num)
{
    struct netdev_xpliant *netdev;

    netdev = netdev_xpliant_from_port_num(dev_id, port_num);
    if (netdev) {
        netdev_xpliant_link_state_callback(netdev, true);
    }
}

static void
port_on_link_down_event(xpsDevice_t dev_id, uint8_t port_num)
{
    struct netdev_xpliant *netdev;

    netdev = netdev_xpliant_from_port_num(dev_id, port_num);
    if (netdev) {
        netdev_xpliant_link_state_callback(netdev, false);
    }
}
#endif /* XP_DEV_EVENT_MODE */

static void
port_update_config(struct port_cfg *cur_pcfg,
                   const struct port_cfg *new_pcfg)
{
    cur_pcfg->enable = new_pcfg->enable;
    cur_pcfg->autoneg = new_pcfg->autoneg;
    cur_pcfg->speed = new_pcfg->speed;
    cur_pcfg->duplex = new_pcfg->duplex;
    cur_pcfg->pause_rx = new_pcfg->pause_rx;
    cur_pcfg->pause_tx = new_pcfg->pause_tx;
    cur_pcfg->mtu = new_pcfg->mtu;
}

static void
link_netdev(struct netdev_xpliant *netdev)
{
#ifdef OPS_XP_SIM
    const char *name = netdev_get_name(&netdev->up);

#define SWNS "/sbin/ip netns exec swns "
    ops_xp_system(SWNS "/sbin/ifconfig veth-%s up", name);
    ops_xp_system(SWNS "/sbin/tc qdisc add dev veth-%s ingress", name);
    ops_xp_system(SWNS "/sbin/tc filter add dev veth-%s parent ffff: " \
                       "protocol all u32 match u8 0 0 " \
                       "action mirred egress redirect dev tap0_0_%u",
                       name, netdev->port_num);

    ops_xp_system(SWNS "/sbin/tc qdisc add dev tap0_0_%u ingress",
                  netdev->port_num);
    ops_xp_system(SWNS "/sbin/tc filter add dev tap0_0_%u parent ffff: " \
                       "protocol all u32 match u8 0 0 " \
                       "action mirred egress redirect dev veth-%s",
                       netdev->port_num, name);
#undef SWNS
#endif
}

static void
unlink_netdev(struct netdev_xpliant *netdev)
{
#ifdef OPS_XP_SIM
    const char *name = netdev_get_name(&netdev->up);

#define SWNS "/sbin/ip netns exec swns "
    ops_xp_system(SWNS "/sbin/tc qdisc del dev tap0_0_%u parent ffff:",
                  netdev->port_num);
    ops_xp_system(SWNS "/sbin/tc qdisc del dev veth-%s parent ffff:", name);
    ops_xp_system(SWNS "/sbin/ifconfig veth-%s down", name);
#undef SWNS
#endif
}

int
ops_xp_port_set_config(struct netdev_xpliant *netdev,
                       const struct port_cfg *new_cfg)
{
    XP_STATUS status = XP_NO_ERR;
    xpsPortConfig_t port_config;
    uint8_t link = 0;
    int rc;

    VLOG_INFO("%s: apply config for netdev %s enable=%u", __FUNCTION__,
              netdev_get_name(&(netdev->up)), new_cfg->enable);

    /* Handle port enable change at the first stage */
    if (netdev->pcfg.enable != new_cfg->enable) {
        if (new_cfg->enable == false) {

            unlink_netdev(netdev);

            XP_LOCK();
            status = xpsPolicerEnablePortPolicing(netdev->port_num, 0);
            if (status != XP_NO_ERR) {
                VLOG_WARN("%s: unable to disable port policing for %s.",
                          __FUNCTION__, netdev_get_name(&(netdev->up)));
            }
#if (XP_DEV_EVENT_MODE == XP_DEV_EVENT_MODE_INTERRUPT)
            status = xpsMacEventHandlerDeRegister(netdev->xpdev->id,
                                                  netdev->port_num, LINK_UP);
            if (status != XP_NO_ERR) {
                VLOG_WARN("%s: unable to deregister LINK_UP event handler for %s.",
                          __FUNCTION__, netdev_get_name(&(netdev->up)));
            }

            status = xpsMacEventHandlerDeRegister(netdev->xpdev->id,
                                                  netdev->port_num, LINK_DOWN);
            if (status != XP_NO_ERR) {
                VLOG_WARN("Unable to deregister LINK_DOWN event handler for %s.",
                          __FUNCTION__, netdev_get_name(&(netdev->up)));
            }

            status = xpsMacLinkStatusInterruptEnableSet(netdev->xpdev->id,
                                                        netdev->port_num, false);
            if (status != XP_NO_ERR) {
                VLOG_WARN("%s: failed to disable interrupts on port #%u. Err=%d",
                          __FUNCTION__, netdev->ifId, status);
            }
#endif /* XP_DEV_EVENT_MODE */
            XP_UNLOCK();

            netdev->link_status = false;

            /* Notify ML that port is down */
            ops_xp_mac_learning_on_port_down(netdev->xpdev->ml, netdev->ifId);

        } else {

            link_netdev(netdev);

            XP_LOCK();

            status = xpsPolicerEnablePortPolicing(netdev->ifId, 1);
            if (status != XP_NO_ERR) {
                VLOG_WARN("%s: unable to enable port policing for port #%u. Err=%d",
                          __FUNCTION__, netdev->ifId, status);
            }
#if (XP_DEV_EVENT_MODE == XP_DEV_EVENT_MODE_INTERRUPT)
            status = xpsMacEventHandlerRegister(netdev->xpdev->id,
                                                netdev->port_num, LINK_UP,
                                                port_on_link_up_event);
            if (status != XP_NO_ERR) {
                VLOG_WARN("%s: unable to register LINK_UP event handler " \
                          "for port #%u. Err=%d",
                          __FUNCTION__, netdev->ifId, status);
            }

            status = xpsMacEventHandlerRegister(netdev->xpdev->id,
                                                netdev->port_num, LINK_DOWN,
                                                port_on_link_down_event);
            if (status != XP_NO_ERR) {
                VLOG_WARN("%s: unable to register LINK_DOWN event handler " \
                          "for port #%u. Err=%d",
                          __FUNCTION__, netdev->ifId, status);
            }

            status = xpsMacLinkStatusInterruptEnableSet(netdev->xpdev->id,
                                                        netdev->port_num, true);
            if (status != XP_NO_ERR) {
                VLOG_WARN("%s: failed to enable interrupts on port #%u. Err=%d",
                          __FUNCTION__, netdev->ifId, status);
            }
#endif /* XP_DEV_EVENT_MODE */
            XP_UNLOCK();
        }
    }

    /* Apply port configuration only if port is enabled */
    if (new_cfg->enable == true) {
#if 0
        if (netdev->pcfg.autoneg != new_cfg->autoneg) {
            xp_rc = xpsMacSetPortAutoNeg(netdev->xpdev->id, netdev->port_num, pcfg.autoneg);
        }

        if ((netdev->pcfg.pause_rx != new_cfg->pause_rx) ||
            (netdev->pcfg.pause_tx != new_cfg->pause_tx)) {
            xp_rc = xpsMacSetBpanPauseAbility(netdev->xpdev->id, netdev->port_num, true);
        }

        if (netdev->pcfg.mtu != new_cfg->mtu) {
            xp_rc = xpsMacSetRxMaxFrmLen(netdev->xpdev->id, netdev->port_num, (uint16_t)pcfg.mtu);
        }
#endif
    }

    rc = ops_xp_port_set_enable(netdev->xpdev->id, netdev->port_num,
                                new_cfg->enable);
    if (rc) {
        VLOG_WARN("%s: failed to set enable to %d for port #%u. Err=%d",
                  __FUNCTION__, new_cfg->enable, netdev->port_num, rc);
    }

    /* Update the netdev struct with new config. */
    port_update_config(&(netdev->pcfg), new_cfg);

    return 0;
}

int
ops_xp_port_get_enable(xpsDevice_t id, xpsPort_t port_num, bool *enable)
{
    struct xp_port_info *port_info = ops_xp_dev_get_port_info(id, port_num);

    if (port_info) {
        *enable = port_info->hw_enable;
        return 0;
    }

    return EPERM;
}

int
ops_xp_port_set_enable(xpsDevice_t id, xpsPort_t port_num, bool enable)
{
    struct xp_port_info *port_info = ops_xp_dev_get_port_info(id, port_num);
    XP_STATUS status = XP_NO_ERR;
    if (port_info) {
        if (port_info->hw_enable != enable) {
            XP_LOCK();
            status = xpsMacPortEnable(port_info->id, port_info->port_num,
                                      enable);
            XP_UNLOCK();
            port_info->hw_enable = enable;

            if (enable) {
                /* We must tune port after it is enabled */
                port_info->serdes_tuned = false;
            }
        }
    } else {
        status = XP_ERR_NULL_POINTER;
    }

    return (status == XP_NO_ERR) ? 0 : EPERM;
}
