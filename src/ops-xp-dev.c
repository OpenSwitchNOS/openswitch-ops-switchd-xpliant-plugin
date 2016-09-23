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
 * File: ops-xp-dev.c
 *
 * Purpose: This file contains OpenSwitch XPliant device related
 *          application code for the Cavium/XPliant SDK.
 */

#if !defined(OPS_AS7512) && !defined(OPS_XP_SIM)
#define OPS_XP_SIM
#endif

#include <errno.h>

#include "ops-xp-dev.h"
#include "ops-xp-mac-learning.h"
#include "ops-xp-vlan.h"
#include "ops-xp-lag.h"
#include "ops-xp-dev-init.h"
#include "ops-xp-routing.h"
#include "ops-xp-netdev.h"
#include <openvswitch/vlog.h>
#include <ofproto/bond.h>
#include "util.h"
#include "poll-loop.h"
#include "fatal-signal.h"
#include "ops-xp-util.h"
#include "openXpsSalInterface.h"
#include "openXpsPacketDrv.h"
#include "openXpsMac.h"
#include "openXpsPort.h"
#include "openXpsVlan.h"
#include "ofproto/ofproto.h"
#include "ovs-rcu.h"
#include "dummy.h"

VLOG_DEFINE_THIS_MODULE(xp_dev);

#define XP_CPU_PORT_NUM                 135

struct xp_if_id_to_name_entry {
    struct hmap_node hmap_node;       /* Node in if_id_to_name_map hmap. */
    char *intf_name;
};

#ifdef OPS_XP_SIM
typedef XP_STATUS (*xpSimulatorInitFuncPtr_t)(xpsDevice_t devId, void *arg);
typedef XP_STATUS (*xpSimulatorDeInitFuncPtr_t)(xpsDevice_t devId);

typedef enum
{
    XP_SIMULATOR_INIT_STAGE_0 = 0,  /*!< Called after establishing connection to sim */
    XP_SIMULATOR_INIT_STAGE_1,     /*!< Called after stage 0 */
    XP_SIMULATOR_INIT_STAGE_2,     /*!< Called after stage 1 */
    XP_SIMULATOR_INIT_STAGE_3,     /*!< Called after stage 2 */
    XP_SIMULATOR_INIT_STAGE_MAX    /*!< Number of stages */
} xpSimulatorInitStage_t;

XP_STATUS xpWmIpcSrvInit(int, int);
XP_STATUS xpWmIpcSrvUpdate(void *);
XP_STATUS xpWmIpcSrvDeviceInitRegister(xpSimulatorInitStage_t,
                                       xpSimulatorInitFuncPtr_t);
XP_STATUS xpWmIpcSrvDeviceDeInitRegister(xpSimulatorDeInitFuncPtr_t);
#endif

static struct ovs_mutex xpdev_mutex = OVS_MUTEX_INITIALIZER;


/* This is set pretty low because we probably won't learn anything from the
 * additional log messages. */
static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(5, 20);

static struct xpliant_dev *gXpDev[XP_MAX_DEVICES];

static xp_host_if_type_t host_if_type = XP_HOST_IF_TAP;
static xpPacketInterface packet_if_type = XP_DMA;

static const struct eth_addr eth_addr_lldp OVS_UNUSED
    = { { { 0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E } } };

static void *xp_dev_recv_handler(void *arg);
static pthread_t xp_dev_event_handler_create(struct xpliant_dev *dev);
static void *xp_dev_event_handler(void *arg);
static void pkt_available_handle(xpsDevice_t intrSrcDev);
static void cleanup_cb(void* aux);


/* Returns device instance by device ID value */
static struct xpliant_dev *
_xp_dev_by_id(xpsDevice_t id)
{
    return (id < XP_MAX_DEVICES) ? gXpDev[id] : NULL;
}

/* Returns device instance by device ID value.
 *
 * Increments device's reference counter on success. The caller
 * must free the returned xpliant_dev with xp_dev_free(). */
struct xpliant_dev *
ops_xp_dev_by_id(xpsDevice_t id)
{
    struct xpliant_dev *dev = NULL;

    XP_LOCK();
    dev = _xp_dev_by_id(id);

    if (dev) {
        ovs_refcount_ref(&dev->ref_cnt);
    }
    XP_UNLOCK();

    return dev;
}

/* References xpliant_dev. */
struct xpliant_dev *
ops_xp_dev_ref(const struct xpliant_dev *dev_)
{
    struct xpliant_dev *dev = CONST_CAST(struct xpliant_dev *, dev_);
    if (dev) {
        ovs_refcount_ref(&dev->ref_cnt);
    }
    return dev;
}

void
ops_xp_mutex_lock(void)
{
    ovs_mutex_lock(&xpdev_mutex);
}

void
ops_xp_mutex_unlock(void)
{
    ovs_mutex_unlock(&xpdev_mutex);
}

struct xpliant_dev *
ops_xp_dev_alloc(xpsDevice_t id)
{
    struct xpliant_dev *dev = NULL;

    XP_TRACE();

    if (id >= XP_MAX_DEVICES) {
        VLOG_ERR("Invalid device ID %d", id);
        return NULL;
    }

    XP_LOCK();

    dev = _xp_dev_by_id(id);
    if (dev) {
        VLOG_WARN("Xpliant device #%d already exists", id);
        XP_UNLOCK();
        return dev;
    }

    dev = xzalloc(sizeof *dev);

    dev->id = id;
    ovs_refcount_init(&dev->ref_cnt);
    dev->init_done = false;
    latch_init(&dev->exit_latch);
    latch_init(&dev->rxq_latch);

    gXpDev[id] = dev;
    XP_UNLOCK();

    return dev;
}

bool
ops_xp_dev_is_initialized(const struct xpliant_dev *dev)
{
    return dev ? dev->init_done : false;
}

static void
ops_xp_dev_system_defaults_set(const struct xpliant_dev *dev)
{
    macAddr_t key_mac;
    int error;
    int i;
    XP_STATUS status;

    /* Set default port state. */
    for (i = 0; i < XP_MAX_TOTAL_PORTS; i++) {
        const struct xp_port_info *port_info = &dev->port_info[i];
        if (port_info->initialized) {
            status = xpsMacPortEnable(dev->id, i, port_info->hw_enable);
            if (status != XP_NO_ERR) {
                VLOG_ERR("%s: Error while disabling port: %d. "
                         "Error code: %d\n", __FUNCTION__, i, status);
            }
        }
    }

    error = ops_xp_vlan_create(dev->vlan_mgr, XP_DEFAULT_VLAN_ID);
    if (error) {
        VLOG_ERR("Unable to create default VLAN");
    }

    status = xpsVlanSetDefault(dev->id, XP_DEFAULT_VLAN_ID);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Error in setting default VLAN. "
                 "Error code: %d\n", __FUNCTION__, status);
    }

    /* Update pVid on CPU port so we can send VLAN untagged packets
     * from CPU (OFPP_LOCAL of OFPP_CONTROLLER) to start of pipeline */
    error = ops_xp_port_default_vlan_set(dev->id, XP_CPU_PORT_NUM,
                                         XP_DEFAULT_VLAN_ID);
    if (error) {
        VLOG_ERR("Unable to set default VLAN for CPU port");
    }

    error = ops_xp_lag_set_balance_mode(dev->id, BM_L3_SRC_DST_HASH);
    if (error != XP_NO_ERR) {
        VLOG_ERR("%s: Error in setting default LAG hash mode. "
                 "Error code: %d\n", __FUNCTION__, status);
    }

    /* Configure control MAC for LACP packets in hardware.*/

    /* XDK APIs use MAC in reverse order. */
    ops_xp_mac_copy_and_reverse(key_mac, eth_addr_lacp.ea);

    status = xpsVlanSetGlobalControlMac(dev->id, key_mac);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Error in inserting control MAC entry for LACP. "
                 "Error code: %d\n", __FUNCTION__, status);
    }

    /* Configure control MAC for LLDP packets in hardware.*/
    ops_xp_mac_copy_and_reverse(key_mac, eth_addr_lldp.ea);

    status = xpsVlanSetGlobalControlMac(dev->id, key_mac);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Error in inserting control MAC entry for LLDP. "
                 "Error code: %d\n", __FUNCTION__, status);
    }

    ops_xp_mac_copy_and_reverse(key_mac, eth_addr_stp.ea);

    status = xpsVlanSetGlobalControlMac(dev->id, key_mac);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s: Error in inserting control MAC entry for STP. "
                 "Error code: %d\n", __FUNCTION__, status);
    }
}

int
ops_xp_dev_init(struct xpliant_dev *dev)
    OVS_EXCLUDED(xpdev_mutex)
{
    XP_STATUS ret = XP_NO_ERR;
    uint32_t i;

    XP_TRACE();

    if (!dev) {
        VLOG_ERR("Invalid device pointer");
        return EFAULT;
    }

    XP_LOCK();

    if (dev->init_done) {
        VLOG_WARN("Xpliant device #%d has already been initialized", dev->id);
        XP_UNLOCK();
        return 0;
    }

    hmap_init(&dev->odp_to_ofport_map);
    ovs_rwlock_init(&dev->odp_to_ofport_lock);

    hmap_init(&dev->if_id_to_name_map);
    ovs_rwlock_init(&dev->if_id_to_name_lock);

    ret = xpsPacketDriverRxConfigModeSet(dev->id, POLL);
    if (ret != XP_NO_ERR) {
        VLOG_ERR("Unable to set Rx mode of device #%d. RC = %u",
                dev->id, ret);
        goto error;
    }

    ret = xpsPacketDriverInterfaceGet(dev->id, &dev->cpu_port_type);
    if (ret != XP_NO_ERR) {
        VLOG_ERR("Unable to get CPU interface type of device #%d. RC = %u",
                dev->id, ret);
        goto error;
    }

    ret = xpsPacketDriverRxConfigModeGet(dev->id, &dev->rx_mode);
    if (ret != XP_NO_ERR) {
        VLOG_ERR("Unable to get Rx mode of device #%d. RC = %u",
                 dev->id, ret);
        goto error;
    }

    if (dev->rx_mode == INTR) {
        VLOG_INFO("XPliant device interrupt RX mode");
        goto error;
    } else { /* POLL */
        VLOG_INFO("XPliant device polling RX mode");
    }

    /* Initialize host interface. */
    ops_xp_host_init(dev, ops_xp_host_if_type_get());

    dev->vlan_mgr = ops_xp_vlan_mgr_create(dev);
    if (!dev->vlan_mgr) {
        VLOG_ERR("Unable to create VLAN manager");
        goto error;
    }

    dev->ml = ops_xp_mac_learning_create(dev,
                                         XP_ML_ENTRY_DEFAULT_IDLE_TIME);
    if (!dev->ml) {
        VLOG_ERR("Unable to create mac learning feature");
        goto error;
    }

    /* Register fatal signal handler. */
    fatal_signal_add_hook(cleanup_cb, NULL, &gXpDev[dev->id], true);

    /* Start rxq processing thread as a last initialization step. */
    dev->rxq_thread = ovs_thread_create("ops-xp-dev-recv", xp_dev_recv_handler,
                                        (void *)dev);
    VLOG_INFO("XPliant device's RXQ processing thread started");

    for (i = 0; i < XP_MAX_TOTAL_PORTS; i++) {
        struct xp_port_info *port_info = &dev->port_info[i];
        port_info->id = dev->id;
        port_info->port_num = i;
        port_info->hw_enable = false;
        port_info->port_mac_mode = MAC_MODE_4X10GB;
        port_info->serdes_tuned = false;
        port_info->initialized = \
                (xpsIsPortInited(dev->id, i) == XP_PORT_NOT_INITED) ? false : true;
    }

    /* Create event processing thread */
    dev->event_thread = xp_dev_event_handler_create(dev);
    VLOG_INFO("XPliant device's event processing thread started");

    ops_xp_dev_system_defaults_set(dev);

    dev->init_done = true;

    XP_UNLOCK();

    /* Register XP device on FS.
     * It means that XP device is ready for use. */
    ops_xp_system("touch /tmp/xpliant/%u/dev/dev%u.~lock~", getpid(), dev->id);

    return 0;

error:
    XP_UNLOCK();
    return EFAULT;
}

void
ops_xp_dev_free(struct xpliant_dev * const dev)
    OVS_EXCLUDED(xpdev_mutex)
{
    XP_STATUS ret = XP_NO_ERR;

    XP_TRACE();

    if (!dev) {
        return;
    }

    XP_LOCK();

    if (_xp_dev_by_id(dev->id) != dev) {
        VLOG_WARN("XPliant device #%d has already been deallocated.", dev->id);
        XP_UNLOCK();
        return;
    }

    if (ovs_refcount_unref(&dev->ref_cnt) == 1) {

        if (dev->rx_mode == INTR) {
            ret = xpsPacketDriverCompletionHndlrDeRegister(dev->id, PD_ALL);
            if (ret != XP_NO_ERR) {
                VLOG_WARN("Unable to de-register Xpliant device #%d handlers. "
                        "RC = %u", dev->id, ret);
            }
            /* TODO: How to be sure that SIGIO signal handler (event callbacks)
             * has finished its execution? Kind of spin lock or
             * atomic variable? */
        }

        ops_xp_mac_learning_unref(dev->ml);
        ops_xp_vlan_mgr_unref(dev->vlan_mgr);

        /* Stop rxq thread */
        latch_set(&dev->exit_latch);
        if (dev->rx_mode == POLL) {
            pthread_cancel(dev->rxq_thread);
        }
        xpthread_join(dev->rxq_thread, NULL);

        pthread_cancel(dev->event_thread);
        xpthread_join(dev->event_thread, NULL);

        latch_poll(&dev->exit_latch);
        latch_destroy(&dev->exit_latch);
        latch_poll(&dev->rxq_latch);
        latch_destroy(&dev->rxq_latch);

        ovs_rwlock_destroy(&dev->odp_to_ofport_lock);
        hmap_destroy(&dev->odp_to_ofport_map);

        ovs_rwlock_destroy(&dev->if_id_to_name_lock);
        hmap_destroy(&dev->if_id_to_name_map);

        gXpDev[dev->id] = NULL;

        free(dev);
        VLOG_INFO("XPliant device instance deallocated.");
    }
    XP_UNLOCK();
}

/* Sends a packet to interface. */
int
ops_xp_dev_send(xpsDevice_t xp_dev_id, xpsInterfaceId_t dst_if_id,
                void *buff, uint16_t buff_size)
{
    xpPacketInfo pktInfo;
    static uint8_t priority;
    int ret;
    xpsInterfaceId_t cpu_if_id;
    xpVif_t src_vif;
    xpVif_t dst_vif;
    uint16_t pkt_size;
    size_t xp_tx_hdr_size;

    if (!buff) {
        VLOG_ERR("%s, Invalid packet buffer.", __FUNCTION__);
        return EINVAL;
    }

    ret = xpsPortGetCPUPortIntfId(xp_dev_id, &cpu_if_id);
    if (ret) {
        VLOG_ERR("%s, Unable to get CPU interface ID. ERR%u",
                 __FUNCTION__, ret);
        return EPERM;
    }

    xpsPacketDriverGetTxHdrSize(&xp_tx_hdr_size);
    pkt_size = (buff_size < 64) ? 64 : buff_size;
    pkt_size += xp_tx_hdr_size;

    pktInfo.buf = xmalloc(pkt_size);
    pktInfo.bufSize = pkt_size;

    src_vif = XPS_INTF_MAP_INTFID_TO_VIF(cpu_if_id);
    dst_vif = XPS_INTF_MAP_INTFID_TO_VIF(dst_if_id);

    pktInfo.priority = priority = 0;
    priority = (priority + 1) % 64;     /* 64 - MAX_TX_QUEUES */

    /* Add Tx header to the packet. */
    xpsPacketDriverCreateHeader(xp_dev_id, &pktInfo, src_vif, dst_vif, true);

    /* Copy payload of the packet. */
    memcpy(pktInfo.buf + xp_tx_hdr_size , buff, buff_size);

    /* Pad the packet with zeroes to 64 byte length */
    if (buff_size < 64) {
        memset(pktInfo.buf + xp_tx_hdr_size + buff_size, 0, (64 - buff_size));
    }

    XP_LOCK();
    /* Send packet */
    ret = xpsPacketDriverSend(xp_dev_id, &pktInfo, SYNC_TX);
    XP_UNLOCK();

    /* Release packet buffer */
    free(pktInfo.buf);

    if (ret != XP_NO_ERR) {
        VLOG_WARN_RL(&rl, "error sending packet on %u. ERR%u", dst_if_id, ret);
        return EAGAIN;
    }

    return 0;
}

/* This handler thread receives mcpu/scpu incoming packets and
 * de-multiplex them into the appropriate UDS sockets. Each single UDS socket
 * corresponds to the single XP device's traffic port. Finally, the packet
 * will be collected from UDS socket by _rxq_recv() function
 * of the appropriate netdev instance. */
static void *
xp_dev_recv_handler(void *arg)
{
    struct xpliant_dev *dev = arg;
    XP_STATUS ret = XP_NO_ERR;
    uint16_t pkts_received = 0;
    struct xpPacketInfo *pkt_info;

    XP_TRACE();

    if (dev->rx_mode == POLL) {
        pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    }
    pkt_info = xzalloc(sizeof *pkt_info);
    pkt_info->buf = NULL;

    VLOG_INFO("Allocate buffer for CPU interface");
    pkt_info->buf = xmalloc(XP_MAX_PACKET_SIZE);

    while (!latch_is_set(&dev->exit_latch)) {

        if (dev->rx_mode == INTR) {
            latch_wait(&dev->exit_latch);
            latch_wait(&dev->rxq_latch);
            poll_block();

            if (!latch_poll(&dev->rxq_latch)) {
                XP_TRACE();
                continue;
            }
        }

        do {
            pkts_received = 1;
            pkt_info->bufSize = XP_MAX_PACKET_SIZE;

            if (XP_NETDEV_DMA != dev->cpu_port_type) {
                XP_LOCK();
            }

            ret = xpsPacketDriverReceive(dev->id, &pkt_info, &pkts_received);

            if (XP_NETDEV_DMA != dev->cpu_port_type) {
                XP_UNLOCK();
            }

            if ((ret == XP_ERR_PKT_NOT_AVAILABLE) || (ret == XP_ERR_TIMEOUT)) {
                /* Sleep for a while to release CPU for other tasks.. */
                /* TODO: How about async notification from WM through IPC? */
                poll(NULL, 0, 1 /* ms */);
            } else if ((ret != XP_NO_ERR) && (dev->rx_mode == POLL)) {
                VLOG_ERR_RL(&rl, "unable to receive packet. RC = %u", ret);
            }
        }
        while(pkts_received);

    } /* while (!latch_is_set(&dev->exit_latch)) */

    XP_TRACE();

    free(pkt_info->buf);
    free(pkt_info);
    return NULL;
}

/* The handler that will be executed in INTR mode (xpRxConfigMode)
 * to indicate that the packet is available.
 * See xpPacketDriverInterface::completionHndlrRegister() */
static void
pkt_available_handle(xpsDevice_t intrSrcDev)
{
    /* Kick rxq polling thread */
    latch_set(&_xp_dev_by_id(intrSrcDev)->rxq_latch);
}

#ifdef OPS_XP_SIM
static void *
ipc_handler(void *arg)
{
    XP_STATUS status = XP_NO_ERR;

    for (;;) {
        /* Handle HW/WM incoming requests. */
        status = xpWmIpcSrvUpdate(arg);
        if (status != XP_NO_ERR)
        {
            VLOG_ERR("XDK IPC update failed. RC = %u", status);
        }
    }

    return NULL;
}
#endif

static pthread_t
xp_dev_event_handler_create(struct xpliant_dev *dev)
{
    return ovs_thread_create("ops-xp-dev-event", xp_dev_event_handler, (void *)dev);
}

/* This handler thread handles XP device mics events like port link
 * status change, fault detection, etc.
 * 
 * NOTE: Currently only port event handling is supported */
static void *
xp_dev_event_handler(void *arg)
{
    return ops_xp_port_event_handler(arg);
}

static void
locker_init(void)
{
    ops_xp_system("rm -rf /tmp/xpliant/%u 2> /dev/null", getpid());
    ops_xp_system("mkdir -p /tmp/xpliant/%u/dev", getpid());
    ops_xp_system("chmod -fR 777 /tmp/xpliant");
}

static void
host_if_type_init(void)
{
    const char *p_mode = getenv("HOST_PACKET_IF_MODE");

    if (p_mode != NULL) {
        if (strcmp("XPNET_NETDEV", p_mode) == 0) {
            host_if_type = XP_HOST_IF_XPNET;
            packet_if_type = XP_NETDEV_DMA;
        } else if (strcmp("TAP_NETDEV", p_mode) == 0) {
            host_if_type = XP_HOST_IF_TAP;
            packet_if_type = XP_NETDEV_DMA;
        }
    }

    VLOG_INFO("Host interface type: %u.", host_if_type);
    VLOG_INFO("Packet interface type: %u.", packet_if_type);
}

xp_host_if_type_t
ops_xp_host_if_type_get(void)
{
    return host_if_type;
}

xpPacketInterface
ops_xp_packet_if_type_get(void)
{
    return packet_if_type;
}

int
ops_xp_dev_srv_init(void)
{
    XP_STATUS status = XP_NO_ERR;
    static struct ovsthread_once once = OVSTHREAD_ONCE_INITIALIZER;

    if (ovsthread_once_start(&once)) {
        static xpInitType_t initType = INIT_COLD;

        /* Create folder to register new XP devices */
        locker_init();

        /* Register dummy plugins */
        dummy_enable(false);

        /* Init host and packet interfaces working mode. */
        host_if_type_init();

        /* Perform XDK-specific initialization */
        status = ops_xp_sdk_init(initType);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Failed to initialize XDK. RC = %u", status);
            return EFAULT;
        }

#ifdef OPS_XP_SIM
        /* Setup XDK IPC server */
        xpWmIpcSrvDeviceInitRegister(XP_SIMULATOR_INIT_STAGE_0,
                                     ops_xp_dev_config);
        xpWmIpcSrvDeviceDeInitRegister(ops_xp_sdk_dev_remove);

        /* Use default IPC ports */
        status = xpWmIpcSrvInit(-1, -1);
        if (status != XP_NO_ERR) {
            VLOG_ERR("Failed to initialize XDK IPC. RC = %u", status);
            return EFAULT;
        }

        /* Handle XP devices specific background tasks */
        ovs_thread_create("ops-xp-dev-ipc-srv", ipc_handler, &initType);
#else
        xpsDevice_t deviceId = 0;

        status = ops_xp_dev_config(deviceId, &initType);
        if (status != XP_NO_ERR) {
            VLOG_ERR("xp_dev_config Failed.. Error #%1d\n", status);
            return EFAULT;
        }
#endif
        ovsthread_once_done(&once);
    }
    return 0;
}

/* Fatal signal handler for xp_dev. */
static void
cleanup_cb(void* aux)
{
    struct xpliant_dev *dev = *((struct xpliant_dev**)aux);

    if (aux == NULL) {  /* xp_dev is already removed. */
        return;
    }
}

/* Get storage for port related information */
struct xp_port_info *
ops_xp_dev_get_port_info(xpsDevice_t id, xpsPort_t port_num)
{
    struct xpliant_dev *dev;

    dev = _xp_dev_by_id(id);
    if (dev) {
        if (port_num < XP_MAX_TOTAL_PORTS) {
            return &dev->port_info[port_num];
        }
    }

    return NULL;
}

/* Creates interface ID to name mapping. */
int
ops_xp_dev_add_intf_entry(struct xpliant_dev *xpdev, xpsInterfaceId_t intf_id,
                          char *intf_name, uint32_t vni)
{
    if (xpdev) {
        struct xp_if_id_to_name_entry *e;
        struct hmap_node *node;

        ovs_rwlock_rdlock(&xpdev->if_id_to_name_lock);

        node = hmap_first_with_hash(&xpdev->if_id_to_name_map, intf_id);
        if (node) {
            ovs_rwlock_unlock(&xpdev->if_id_to_name_lock);
            return EEXIST;
        }

        ovs_rwlock_unlock(&xpdev->if_id_to_name_lock);

        e = xzalloc(sizeof(*e));
        e->intf_name = xstrdup(intf_name);

        ovs_rwlock_wrlock(&xpdev->if_id_to_name_lock);
        hmap_insert(&xpdev->if_id_to_name_map, &e->hmap_node, intf_id);
        ovs_rwlock_unlock(&xpdev->if_id_to_name_lock);
    }

    return 0;
}

/* Removes interface ID to name mapping. */
void
ops_xp_dev_remove_intf_entry(struct xpliant_dev *xpdev, xpsInterfaceId_t intf_id,
                             uint32_t vni)
{
    if (xpdev) {
        struct hmap_node *node;
        struct xp_if_id_to_name_entry *e;

        ovs_rwlock_wrlock(&xpdev->if_id_to_name_lock);

        node = hmap_first_with_hash(&xpdev->if_id_to_name_map, intf_id);
        if (node) {
            hmap_remove(&xpdev->if_id_to_name_map, node);

            ovs_rwlock_unlock(&xpdev->if_id_to_name_lock);

            e = CONTAINER_OF(node, struct xp_if_id_to_name_entry,
                             hmap_node);
            free(e->intf_name);
            free(e);

            return;
        }

        ovs_rwlock_unlock(&xpdev->if_id_to_name_lock);
    }
}

/* Returns name of interface if it exists. Otherwise NULL.
 * Dynamically allocates memory for interface name. */
char *
ops_xp_dev_get_intf_name(struct xpliant_dev *xpdev, xpsInterfaceId_t intfId,
                         uint32_t vni)
{
    XP_STATUS status = XP_NO_ERR;
    char *name = NULL;

    if (xpdev) {
        struct xp_if_id_to_name_entry *e;
        struct hmap_node *node;

        ovs_rwlock_rdlock(&xpdev->if_id_to_name_lock);

        node = hmap_first_with_hash(&xpdev->if_id_to_name_map, intfId);
        if (node) {
            e = CONTAINER_OF(node, struct xp_if_id_to_name_entry, hmap_node);
            name = xstrdup(e->intf_name);
        }

        ovs_rwlock_unlock(&xpdev->if_id_to_name_lock);
    }

    return name;
}
