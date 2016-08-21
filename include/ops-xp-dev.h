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
 * File: ops-xp-dev.h
 *
 * Purpose: This file provides public definitions for OpenSwitch XPliant device
 *          related application code for the Cavium/XPliant SDK.
 */

#ifndef OPS_XP_DEV_H
#define OPS_XP_DEV_H 1

#include <netdev-provider.h>
#include <latch.h>
#include <hmap.h>
#include <ovs-thread.h>
#include "ops-xp-port.h"

#include "ops-xp-host.h"
#include "openXpsInterface.h"

#define XP_DEV_EVENT_MODE_POLL          0
#define XP_DEV_EVENT_MODE_INTERRUPT     1

#define XP_DEV_EVENT_MODE               XP_DEV_EVENT_MODE_POLL

#define XP_MAX_CHAN_PER_MAC             XP_PORTS_PER_PORT_GROUP

#define XP_LOCK() \
        ops_xp_mutex_lock();

#define XP_UNLOCK() \
        ops_xp_mutex_unlock();

struct netdev_xpliant;
struct xp_l3_mgr;
struct xp_vlan_mgr;
struct xp_mac_learning;
struct xp_host_if_info;

struct xpliant_dev {
    xpsDevice_t id;                 /* Xpliant device ID. */
    xpRxConfigMode rx_mode;         /* Rx mode INTR/POLL */
    xpPacketInterface cpu_port_type;/* CPU interface type DMA/ETHER/NETDEV_DMA */
    pthread_t rxq_thread;           /* RxQ Thread ID. */
    pthread_t event_thread;         /* Event processing Thread ID. */
    struct latch exit_latch;        /* Tells child threads to exit. */
    struct latch rxq_latch;         /* Tells child threads to handle pkt Rx. */
    struct ovs_refcount ref_cnt;    /* Times this devices was opened. */
    bool init_done;

    struct xp_vlan_mgr *vlan_mgr;
    struct xp_mac_learning *ml;

    uint8_t router_mac[ETH_ADDR_LEN];
    struct ovs_rwlock odp_to_ofport_lock;
    struct hmap odp_to_ofport_map OVS_GUARDED; /* Contains "struct ofport"s. */

    struct xp_host_if_info *host_if_info;

    struct xp_port_info port_info[XP_MAX_TOTAL_PORTS];
    struct ovs_rwlock if_id_to_name_lock;
    struct hmap if_id_to_name_map; /* Holds interface ID to name mapping. */
};

int ops_xp_dev_srv_init(void);
struct xpliant_dev *ops_xp_dev_by_id(xpsDevice_t id);
struct xpliant_dev *ops_xp_dev_ref(const struct xpliant_dev *dev);
struct xpliant_dev *ops_xp_dev_alloc(xpsDevice_t id);
void ops_xp_dev_free(struct xpliant_dev * const dev);
int ops_xp_dev_init(struct xpliant_dev * dev);
bool ops_xp_dev_is_initialized(const struct xpliant_dev *dev);
void ops_xp_mutex_lock(void);
void ops_xp_mutex_unlock(void);
struct xp_port_info *ops_xp_dev_get_port_info(xpsDevice_t id,
                                              xpsPort_t port_num);
int ops_xp_dev_send(xpsDevice_t xp_dev_id, xpsInterfaceId_t dst_if_id,
                    void *buff, uint16_t buff_size);
xp_host_if_type_t ops_xp_host_if_type_get(void);
xpPacketInterface ops_xp_packet_if_type_get(void);
int ops_xp_dev_add_intf_entry(struct xpliant_dev *xpdev,
                              xpsInterfaceId_t intf_id,
                              char *intf_name, uint32_t vni);
void ops_xp_dev_remove_intf_entry(struct xpliant_dev *xpdev,
                                  xpsInterfaceId_t intf_id,
                                  uint32_t vni);
char* ops_xp_dev_get_intf_name(struct xpliant_dev *xpdev,
                               xpsInterfaceId_t intfId,
                               uint32_t vni);
#endif /* ops-xp-dev.h */
