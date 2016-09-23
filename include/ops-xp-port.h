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
 * File: ops-xp-port.h
 *
 * Purpose: This file provides public definitions for OpenSwitch port related
 *          application code for the Cavium/XPliant SDK.
 */

#ifndef OPS_XP_PORT_H
#define OPS_XP_PORT_H 1

#include <stdint.h>
#include <netinet/ether.h>

#include "openXpsTypes.h"
#include "openXpsEnums.h"

struct netdev_xpliant;

struct port_cfg {
    /* configured or intended state */
    int   enable;
    int   autoneg;
    int   speed;
    int   duplex;
    int   pause_rx;
    int   pause_tx;
    int   mtu;
};

struct xp_port_info {
    xpsDevice_t id;                 /* Xpliant device ID. */
    xpsPort_t port_num;             /* Device port number */
    bool hw_enable;
    bool serdes_tuned;
    bool initialized;

    /* ------- Subport/lane split config (e.g. QSFP+) -------
     * Subport count & lane split status. These are valid for
     * primary port only.  We currently only support port split
     * on QSFP ports, and each port can be split into 4 separate
     * ports.  By definition, the 4 lanes of a QSFP+ port must
     * be consecutively numbered.
     */
    uint32_t    split_port_count;
    xpMacConfigMode port_mac_mode;
};

int ops_xp_port_mac_mode_set(struct xp_port_info *p_info, xpMacConfigMode mac_mode);

int ops_xp_port_set_config(struct netdev_xpliant *netdev, const struct port_cfg *pcfg);

int ops_xp_port_get_enable(xpsDevice_t id, xpsPort_t port_num, bool *enable);

int ops_xp_port_set_enable(xpsDevice_t id, xpsPort_t port_num, bool enable);

void *ops_xp_port_event_handler(void *arg);

#endif /* ops-xp-port.h */
