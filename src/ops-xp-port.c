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
#include "ops-xp-dev-init.h"

VLOG_DEFINE_THIS_MODULE(xp_port);

#define XP_PORT_LINK_POLL_INTERVAL  (2000u)


int
ops_xp_port_mac_mode_set(struct xp_port_info *p_info, xpMacConfigMode mac_mode)
{
    XP_STATUS xp_rc = XP_NO_ERR;
    uint8_t mac_num;
    int i = 0;

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
    xp_rc = xpsMacGetMacNumForPortNum(p_info->id, p_info->port_num,
                                      &mac_num);
    if (xp_rc != XP_NO_ERR) {
        XP_UNLOCK();
        VLOG_ERR("%s: unable to get MAC number for port %u. Err=%u",
                 __FUNCTION__, p_info->port_num, xp_rc);
        return EFAULT;
    }

    /* Switch to new MAC mode */
    xp_rc = xpsMacSwitchMacConfigMode(p_info->id, mac_num, mac_mode, 
                                      RS_FEC_MODE, (mac_mode == MAC_MODE_1X100GB));
    XP_UNLOCK();
    if (xp_rc != XP_NO_ERR) {
        VLOG_ERR("%s: unable to set %u MAC mode. Err=%u",
                 __FUNCTION__, mac_mode, xp_rc);
        return EFAULT;
    }

    p_info->port_mac_mode = mac_mode;

    return 0;

}

#if (XP_DEV_EVENT_MODE == XP_DEV_EVENT_MODE_POLL)
/* The handler that will process port link status related activities
 * in polling mode. It cyclically polls ports for link status and
 * calls netdev link state callback if port link state changes */
void *
ops_xp_port_event_handler(void *arg)
{
    struct xpliant_dev *dev = (struct xpliant_dev *)arg;
    struct netdev_xpliant *netdev = NULL;
    XP_STATUS status;
    uint8_t link;
    uint32_t port;

    for (;;) {
        for (port = 0; port < XP_MAX_TOTAL_PORTS; port++) {
            /* Sleep for a while before going to next port.
             * Execute sleep here to handle case when port is skipped via
             * 'continue' */
            ops_xp_msleep(XP_PORT_LINK_POLL_INTERVAL / XP_MAX_TOTAL_PORTS);

            netdev = ops_xp_netdev_from_port_num(dev->id, port);

            /* Skip if no netdev, netdev is not initialized or netdev is disabled */
            if (!netdev || !netdev->intf_initialized || !netdev->pcfg.enable) {
                continue;
            }

            XP_LOCK();
            status = xpsMacGetLinkStatus(netdev->xpdev->id,
                                         netdev->port_num, &link);
            XP_UNLOCK();
            if (status != XP_NO_ERR) {
                VLOG_WARN("%s: could not get %u port link status. Err=%d",
                          __FUNCTION__, netdev->port_num, status);
            }

            if (netdev->link_status != !!link) {
                ops_xp_netdev_link_state_callback(netdev, link);
            }
        }
    }

    return NULL;
}
#else /* (XP_DEV_EVENT_MODE == XP_DEV_EVENT_MODE_INTERRUPT) */
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
    XP_STATUS xp_rc = XP_NO_ERR;
    xpsPortConfig_t port_config;
    uint8_t link = 0;
    int rc;

    VLOG_INFO("%s: apply config for netdev %s enable=%u", __FUNCTION__,
              netdev_get_name(&(netdev->up)), new_cfg->enable);

    /* Handle port enable change at the first stage */
    if (netdev->pcfg.enable != new_cfg->enable) {
        if (new_cfg->enable == false) {

            unlink_netdev(netdev);

            rc = ops_xp_port_set_enable(netdev->port_info, false);
            if (rc) {
                VLOG_WARN("%s: failed to disable port #%u. Err=%d",
                          __FUNCTION__, netdev->ifId, rc);
            }

            XP_LOCK();
            xp_rc = xpsPolicerEnablePortPolicing(netdev->port_num, 0);
            if (xp_rc != XP_NO_ERR) {
                VLOG_WARN("%s: unable to disable port policing for %s.",
                        __FUNCTION__, netdev_get_name(&(netdev->up)));
            }
#if (XP_DEV_EVENT_MODE == XP_DEV_EVENT_MODE_INTERRUPT)
            xp_rc = xpsMacEventHandlerDeRegister(netdev->xpdev->id,
                                                 netdev->port_num, LINK_UP);
            if (xp_rc != XP_NO_ERR) {
                VLOG_WARN("%s: unable to deregister LINK_UP event handler for %s.",
                          __FUNCTION__, netdev_get_name(&(netdev->up)));
            }

            xp_rc = xpsMacEventHandlerDeRegister(netdev->xpdev->id,
                                                 netdev->port_num, LINK_DOWN);
            if (xp_rc != XP_NO_ERR) {
                VLOG_WARN("Unable to deregister LINK_DOWN event handler for %s.",
                          __FUNCTION__, netdev_get_name(&(netdev->up)));
            }

            xp_rc = xpsMacLinkStatusInterruptEnableSet(netdev->xpdev->id,
                                                       netdev->port_num, false);
            if (xp_rc != XP_NO_ERR) {
                VLOG_WARN("%s: failed to disable interrupts on port #%u. Err=%d",
                          __FUNCTION__, netdev->ifId, xp_rc);
            }
#endif /* XP_DEV_EVENT_MODE */
            XP_UNLOCK();

            netdev->link_status = false;

        } else {

            link_netdev(netdev);

            XP_LOCK();
            xp_rc = xpsPortGetConfig(netdev->xpdev->id, netdev->ifId,
                                     &port_config);
            if (xp_rc != XP_NO_ERR) {
                XP_UNLOCK();
                VLOG_ERR("%s: could not set port config for port #%u. Err=%d",
                         __FUNCTION__, netdev->ifId, xp_rc);
                return EFAULT;
            }

            port_config.portState = SPAN_STATE_FORWARD;
            xp_rc = xpsPortSetConfig(netdev->xpdev->id, netdev->ifId, &port_config);
            if (xp_rc != XP_NO_ERR) {
                XP_UNLOCK();
                VLOG_ERR("%s: could not set port config for port #%u. Err=%d",
                         __FUNCTION__, netdev->ifId, xp_rc);
                return EFAULT;
            }

            xp_rc = xpsPolicerEnablePortPolicing(netdev->ifId, 1);
            if (xp_rc != XP_NO_ERR) {
                VLOG_WARN("%s: unable to enable port policing for port #%u. Err=%d",
                          __FUNCTION__, netdev->ifId, xp_rc);
            }
#if (XP_DEV_EVENT_MODE == XP_DEV_EVENT_MODE_INTERRUPT)
            xp_rc = xpsMacEventHandlerRegister(netdev->xpdev->id,
                                               netdev->port_num, LINK_UP,
                                               port_on_link_up_event);
            if (xp_rc != XP_NO_ERR) {
                VLOG_WARN("%s: unable to register LINK_UP event handler " \
                          "for port #%u. Err=%d",
                          __FUNCTION__, netdev->ifId, xp_rc);
            }

            xp_rc = xpsMacEventHandlerRegister(netdev->xpdev->id,
                                               netdev->port_num, LINK_DOWN,
                                               port_on_link_down_event);
            if (xp_rc != XP_NO_ERR) {
                VLOG_WARN("%s: unable to register LINK_DOWN event handler " \
                          "for port #%u. Err=%d",
                          __FUNCTION__, netdev->ifId, xp_rc);
            }

            xp_rc = xpsMacLinkStatusInterruptEnableSet(netdev->xpdev->id,
                                                       netdev->port_num, true);
            if (xp_rc != XP_NO_ERR) {
                VLOG_WARN("%s: failed to enable interrupts on port #%u. Err=%d",
                          __FUNCTION__, netdev->ifId, xp_rc);
            }
#endif /* XP_DEV_EVENT_MODE */
            XP_UNLOCK();

            rc = ops_xp_port_set_enable(netdev->port_info, true);
            if (rc) {
                VLOG_WARN("%s: failed to enable port #%u. Err=%d",
                          __FUNCTION__, netdev->ifId, rc);
            }
        }
    }

    /* Apply port configuration only if port is enabled */
    if (new_cfg->enable == true) {
        /* Tune serdes if go from disable to enable or
         * speed configuration changed */
        if ((netdev->pcfg.speed != new_cfg->speed) ||
                (netdev->pcfg.enable != new_cfg->enable)) {
            XP_LOCK();
            /* Tune serdes to new speed */
            VLOG_INFO("Serdes %u tuning started", netdev->port_num);
            xp_rc = xpsMacPortSerdesTune(netdev->xpdev->id, &netdev->port_num, 1,
                                         DFE_ICAL, 0);
            VLOG_INFO("Serdes %u tuning finished", netdev->port_num);
            if (xp_rc != XP_NO_ERR) {
                VLOG_WARN("Failed to tune serdes on Port %s. Err=%u ",
                          netdev_get_name(&(netdev->up)), xp_rc);
            }

            if (new_cfg->speed == 100000) {
                uint8_t mac_num = XP_MAX_PTGS;

                xp_rc = xpsMacGetMacNumForPortNum(netdev->xpdev->id,
                                                  netdev->port_num, &mac_num);
                if (xp_rc != XP_NO_ERR) {
                    VLOG_WARN("%s: unable to get MAC number for port %u. Err=%u",
                             __FUNCTION__, netdev->port_num, xp_rc);
                }

                xp_rc = xpsMacPortGroupDeInit(netdev->xpdev->id, mac_num);
                if (xp_rc != XP_NO_ERR) {
                    VLOG_WARN("Failed to deinit MAC for Port %u. Err=%u ",
                              netdev->port_num, xp_rc);
                }
                xp_rc = xpsMacPortGroupInit(netdev->xpdev->id, mac_num, MAC_MODE_1X100GB,
                                            SPEED_100GB, 0, 0, 0, RS_FEC_MODE, 1);
                if (xp_rc != XP_NO_ERR) {
                    VLOG_WARN("Failed to init MAC for Port %u. Err=%u ",
                              netdev->port_num, xp_rc);
                }
            }
            XP_UNLOCK();
        }
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
        XP_LOCK();
        xp_rc = xpsMacGetLinkStatus(netdev->xpdev->id, netdev->port_num, &link);
        if (xp_rc != XP_NO_ERR) {
            VLOG_WARN("%s: could not get %u port link status. Err=%d",
                      __FUNCTION__, netdev->port_num, xp_rc);
        }
        XP_UNLOCK();

        netdev->link_status = link ? true : false;
    }

    /* Update the netdev struct with new config. */
    port_update_config(&(netdev->pcfg), new_cfg);

    return 0;
}

int
ops_xp_port_get_enable(struct xp_port_info *port_info, bool *enable)
{
    *enable = port_info->hw_enable;
    return 0;
}

int
ops_xp_port_set_enable(struct xp_port_info *port_info, bool enable)
{
    XP_STATUS xp_rc = XP_NO_ERR;

    if (port_info->hw_enable != enable) {
        XP_LOCK();
        xp_rc = xpsMacPortEnable(port_info->id, port_info->port_num, enable);
        XP_UNLOCK();
        port_info->hw_enable = enable;
    }

    return (xp_rc == XP_NO_ERR) ? 0 : EPERM;
}
