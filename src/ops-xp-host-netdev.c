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
 * File: ops-xp-host-netdev.c
 *
 * Purpose: This file contains OpenSwitch CPU netdev interface related
 *          application code for the Cavium/XPliant SDK.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openvswitch/vlog.h>
#include <netinet/ether.h>

#include "ops-xp-util.h"
#include "ops-xp-host.h"
#include "ops-xp-dev.h"
#include "ops-xp-dev-init.h"
#include "openXpsPacketDrv.h"
#include "openXpsPort.h"
#include "openXpsReasonCodeTable.h"

VLOG_DEFINE_THIS_MODULE(xp_host_netdev);

static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(5, 20);

typedef struct knet_netlink_tlv_msg {
    uint32_t op_type;   /* xpNlMsgType                          */
    uint32_t msg_len;   /* Len of op_type + msg_len + payload[] */
    uint8_t  payload[]; /* Payload                              */
} knet_netlink_tlv_msg_t;

typedef enum xp_host_if_trap_channel {
    XP_HOST_IF_TRAP_CHANNEL_FD,     /* Receive packets via file desriptor */
    XP_HOST_IF_TRAP_CHANNEL_CB,     /* Receive packets via callback       */
    XP_HOST_IF_TRAP_CHANNEL_NETDEV, /* Receive packets via OS net device  */
    XP_HOST_IF_TRAP_CHANNEL_CUSTOM_RANGE_BASE = 0x10000000
} xp_host_if_trap_channel_t;

typedef struct xp_trap_cfg {
    uint32_t rc;                       /* Reason code the trap is linked with */
    uint32_t trap_id;                  /* Trap ID that is used as a key       */
    uint32_t socket_fd;                /* FD that is used for unknown traffic */
    xp_host_if_trap_channel_t channel; /* Type of channel                     */
} xp_trap_cfg_t;

static int netdev_init(xpsDevice_t unit);
static int netdev_trap_set(xpsDevice_t unit, const xp_trap_cfg_t *trap_cfg);
static int netdev_if_create(xpsDevice_t unit, char *name,
                            xpsInterfaceId_t xps_if_id,
                            struct ether_addr *mac, int *knet_if_id);
static int netdev_if_delete(xpsDevice_t unit, int knet_if_id);
static int knet_netlink_msg_send(xpsDevice_t unit, uint8_t *msg);
static int knet_netlink_buf_get(xpsDevice_t unit, uint8_t **msg_payload,
                                uint8_t **msg);
static void *netlink_netdev_create_msg_assign(knet_netlink_tlv_msg_t *msg,
                                              uint32_t **msg_len);
static void *netlink_netdev_remove_msg_assign(knet_netlink_tlv_msg_t *msg,
                                              uint32_t **msg_len);
static void *netlink_netdev_id_msg_assign(knet_netlink_tlv_msg_t *msg,
                                          uint32_t intf_id);
static void *netlink_netdev_name_msg_assign(knet_netlink_tlv_msg_t *msg,
                                            char *intf_name);
static void *netlink_netdev_tx_header_msg_assign(xpsDevice_t unit, xpsInterfaceId_t xps_if_id,
                                                 knet_netlink_tlv_msg_t *msg);
static void *netlink_netdev_trap_msg_assign(knet_netlink_tlv_msg_t *msg,
                                            const xp_trap_cfg_t *trap_cfg);

const struct xp_host_if_api xp_host_netdev_api = {
    netdev_init,
    NULL,
    netdev_if_create,
    netdev_if_delete,
    NULL,
    NULL,
};

static int
netdev_init(xpsDevice_t unit)
{
    uint32_t i = 0;
    uint32_t list_of_rc[] = {
        XP_IVIF_RC_BPDU,
        XP_BRIDGE_RC_IVIF_ARPIGMPICMP_CMD,
        XP_ROUTE_RC_HOST_TABLE_HIT,
        XP_ROUTE_RC_ROUTE_NOT_POSSIBLE
    };

    xp_trap_cfg_t trap_cfg = {
        .socket_fd = 0,
        .channel = XP_HOST_IF_TRAP_CHANNEL_NETDEV
    };

    for (i = 0; i < ARRAY_SIZE(list_of_rc); i++) {
        trap_cfg.rc = list_of_rc[i];
        trap_cfg.trap_id = i;

        if (netdev_trap_set(unit, &trap_cfg)) {
            VLOG_ERR("%s, Unable to install a trap.", __FUNCTION__);
            return EFAULT;
        }
    }

    return 0;
}

static int
netdev_trap_set(xpsDevice_t unit, const xp_trap_cfg_t *trap_cfg)
{
    uint8_t *nl_msg = NULL;
    knet_netlink_tlv_msg_t *nl_msg_payload = NULL;

    /* Allocate netlink buffer. */
    if (knet_netlink_buf_get(unit, (uint8_t **)&nl_msg_payload, &nl_msg)) {
        VLOG_ERR("%s, Unable to get netlink buffer.", __FUNCTION__);
        return EPERM;
    }

    netlink_netdev_trap_msg_assign(nl_msg_payload, trap_cfg);

    /* Send netlink message to kernel driver. */
    if (knet_netlink_msg_send(unit, nl_msg)) {
        VLOG_ERR("%s, Unable to send netlink "
                 "message to kernel driver.", __FUNCTION__);
        free(nl_msg); /* It is a workround to avoid a memory leak in the SDK. */
        return EFAULT;
    }

    free(nl_msg); /* It is a workround to avoid a memory leak in the SDK. */
    return 0;
}

static int
netdev_if_create(xpsDevice_t unit, char *intf_name,
                 xpsInterfaceId_t xps_if_id,
                 struct ether_addr *mac, int *knet_if_id)
{
    int knet_if = xps_if_id + 1;
    uint8_t *nl_msg = NULL;
    knet_netlink_tlv_msg_t *nl_buf = NULL;
    knet_netlink_tlv_msg_t *nl_msg_payload = NULL;
    uint32_t *nl_msg_payload_len = NULL;
    struct xpliant_dev *xp_dev = ops_xp_dev_by_id(unit);
    char devnet_if_name[IFNAMSIZ] = {0};

    snprintf(devnet_if_name, IFNAMSIZ, "%s", intf_name);

    /* Allocate netlink buffer. */
    if (knet_netlink_buf_get(unit, (uint8_t **)&nl_msg_payload, &nl_msg)) {
        VLOG_ERR("%s, Unable to get netlink buffer.", __FUNCTION__);
        ops_xp_dev_free(xp_dev);
        return EPERM;
    }

    /* Fill up a set of control netlink messages. */
    nl_buf = nl_msg_payload;
    nl_buf = netlink_netdev_create_msg_assign(nl_buf, &nl_msg_payload_len);
    nl_buf = netlink_netdev_id_msg_assign(nl_buf, xps_if_id);
    nl_buf = netlink_netdev_name_msg_assign(nl_buf, devnet_if_name);
    *nl_msg_payload_len = (uint8_t *)nl_buf - (uint8_t *)nl_msg_payload;

    /* Send netlink message to kernel driver. */
    if (knet_netlink_msg_send(unit, nl_msg)) {
        VLOG_ERR("%s, Unable to send netlink "
                 "message to kernel driver.", __FUNCTION__);
        ops_xp_dev_free(xp_dev);
        free(nl_msg); /* It is a workround to avoid a memory leak in the SDK. */
        return EFAULT;
    }

    free(nl_msg); /* It is a workround to avoid a memory leak in the SDK. */

#if 0
    /* Get HW TX header. */
    if (knet_if_tx_header_get(unit, xps_if_id, &tx_header)) {
        VLOG_ERR("Unable to set TX header for %s interface.", devnet_if_name);
        ops_xp_dev_free(xp_dev);
        netdev_if_delete(unit, knet_if);
        return EFAULT;
    }
#endif

    /* Allocate netlink buffer. */
    if (knet_netlink_buf_get(unit, (uint8_t **)&nl_msg_payload, &nl_msg)) {
        VLOG_ERR("%s, Unable to get netlink buffer.", __FUNCTION__);
        ops_xp_dev_free(xp_dev);
        netdev_if_delete(unit, knet_if);
        return EPERM;
    }

    /* Fill up a set of control netlink messages. */
    //netlink_netdev_tx_header_msg_assign(nl_msg_payload, &tx_header);
    if (netlink_netdev_tx_header_msg_assign(unit, xps_if_id, nl_msg_payload)) {
        VLOG_ERR("Unable to set TX header for %s interface.", devnet_if_name);
        ops_xp_dev_free(xp_dev);
        netdev_if_delete(unit, knet_if);
        return EFAULT;
    }

    /* Send netlink message to kernel driver. */
    if (knet_netlink_msg_send(unit, nl_msg)) {
        VLOG_ERR("%s, Unable to send netlink "
                 "message to kernel driver.", __FUNCTION__);
        ops_xp_dev_free(xp_dev);
        netdev_if_delete(unit, knet_if);
        free(nl_msg); /* It is a workround to avoid a memory leak in the SDK. */
        return EFAULT;
    }

    free(nl_msg); /* It is a workround to avoid a memory leak in the SDK. */

    if (ops_xp_net_if_setup(devnet_if_name, mac)) {
        VLOG_ERR("Unable to setup %s interface.", devnet_if_name);
        ops_xp_dev_free(xp_dev);
        netdev_if_delete(unit, knet_if);
        return EFAULT;
    }

    ops_xp_dev_free(xp_dev);
    *knet_if_id = knet_if;
    return 0;
}

static int
netdev_if_delete(xpsDevice_t unit, int knet_if_id)
{
    uint8_t *nl_msg = NULL;
    knet_netlink_tlv_msg_t *nl_buf = NULL;
    knet_netlink_tlv_msg_t *nl_msg_payload = NULL;
    uint32_t *nl_msg_payload_len = NULL;
    xpsInterfaceId_t xps_if_id = knet_if_id - 1;
    struct xpliant_dev *xp_dev = ops_xp_dev_by_id(unit);

    /* Allocate netlink buffer. */
    if (knet_netlink_buf_get(unit, (uint8_t **)&nl_msg_payload, &nl_msg)) {
        VLOG_ERR("%s, Unable to get netlink buffer.", __FUNCTION__);
        ops_xp_dev_free(xp_dev);
        return EPERM;
    }

    /* Fill up a set of control netlink messages. */
    nl_buf = nl_msg_payload;
    nl_buf = netlink_netdev_remove_msg_assign(nl_buf, &nl_msg_payload_len);
    nl_buf = netlink_netdev_id_msg_assign(nl_buf, xps_if_id);
    *nl_msg_payload_len = (uint8_t *)nl_buf - (uint8_t *)nl_msg_payload;

    /* Send netlink message to kernel driver. */
    if (knet_netlink_msg_send(unit, nl_msg)) {
        VLOG_ERR("%s, Unable to send netlink "
                 "message to kernel driver.", __FUNCTION__);
        ops_xp_dev_free(xp_dev);
        free(nl_msg); /* It is a workround to avoid a memory leak in the SDK. */
        return EFAULT;
    }

    ops_xp_dev_free(xp_dev);
    free(nl_msg); /* It is a workround to avoid a memory leak in the SDK. */
    return 0;
}
#if 0
static int
knet_if_tx_header_get(xpsDevice_t unit, xpsInterfaceId_t xps_if_id,
                      xphTxHdr *tx_header)
{
    xpVif_t src_vif = 0;
    xpVif_t dst_vif = 0;
    xpsInterfaceId_t cpu_if_id = 0;

    if (XP_NO_ERR != xpsPortGetCPUPortIntfId(unit, &cpu_if_id)) {
        VLOG_ERR("%s, Unable to get CPU interface ID.", __FUNCTION__);
        return EPERM;
    }

    src_vif = XPS_INTF_MAP_INTFID_TO_VIF(cpu_if_id);
    dst_vif = XPS_INTF_MAP_INTFID_TO_VIF(xps_if_id);

    tx_header->nextEngine = 16;

    tx_header->egressVifLsbByte0 = (dst_vif >> 0) & 0xFF;
    tx_header->egressVifLsbByte1 = (dst_vif >> 8) & 0xFF;
    tx_header->egressVifLsbByte2 = (dst_vif >> 16) & 0xF;

    tx_header->ingressVifLsbByte0 = (src_vif >> 0) & 0xF;
    tx_header->ingressVifLsbByte1 = (src_vif >> 4) & 0xFF;
    tx_header->ingressVifLsbByte2 = (src_vif >> 12) & 0xFF;

    return 0;
}
#endif
static int
knet_netlink_msg_send(xpsDevice_t unit, uint8_t *msg)
{
    return (XP_NO_ERR == xpsNlSockSendMsg(msg)) ? 0 : EFAULT;
}

static int
knet_netlink_buf_get(xpsDevice_t unit, uint8_t **msg_payload, uint8_t **msg)
{
    return (XP_NO_ERR == xpsNlSockGetBuf(msg_payload, msg)) ? 0 : EFAULT;
}

static void *
netlink_netdev_create_msg_assign(knet_netlink_tlv_msg_t *msg,
                                 uint32_t **msg_len)
{
    msg->op_type = XPNL_CREATE_NETDEV_INTF_MSG;
    *msg_len = &msg->msg_len;
    return msg->payload;
}

static void *
netlink_netdev_remove_msg_assign(knet_netlink_tlv_msg_t *msg,
                                 uint32_t **msg_len)
{
    msg->op_type = XPNL_DELETE_NETDEV_INTF_MSG;
    *msg_len = &msg->msg_len;
    return msg->payload;
}

static void *
netlink_netdev_id_msg_assign(knet_netlink_tlv_msg_t *msg, uint32_t intf_id)
{
    msg->op_type = XPNL_INTF_ID;
    msg->msg_len = sizeof(intf_id);
    memcpy(msg->payload, &intf_id, msg->msg_len);
    return msg->payload + msg->msg_len;
}

static void *
netlink_netdev_name_msg_assign(knet_netlink_tlv_msg_t *msg, char *intf_name)
{
    msg->op_type = XPNL_INTF_NAME;
    msg->msg_len = strnlen(intf_name, IFNAMSIZ);
    strncpy(msg->payload, intf_name, IFNAMSIZ);
    return msg->payload + msg->msg_len;
}

static void *
netlink_netdev_tx_header_msg_assign(xpsDevice_t unit, xpsInterfaceId_t xps_if_id,
                                    knet_netlink_tlv_msg_t *msg)
{
    size_t xph_tx_hdr_size;
    knet_netlink_tlv_msg_t *nested_msg = (knet_netlink_tlv_msg_t *)&msg->payload;

    xpsPacketDriverGetTxHdrSize(&xph_tx_hdr_size);

    msg->op_type = XPNL_TXHEADER_MSG;
    nested_msg->op_type = XPNL_TXHEADER;
    nested_msg->msg_len += xph_tx_hdr_size;

    xpsNlTxHeaderMsgAssign(unit, xps_if_id,(uint8_t *)&nested_msg->payload);

    msg->msg_len = sizeof(msg->op_type) + sizeof(nested_msg->msg_len) +
                   sizeof(nested_msg->op_type) + sizeof(nested_msg->msg_len) +
                   nested_msg->msg_len;

    return msg->payload + msg->msg_len;

}

static void *
netlink_netdev_trap_msg_assign(knet_netlink_tlv_msg_t *msg,
                               const xp_trap_cfg_t *trap_cfg)
{
    uint32_t tlv_len = 0;
    knet_netlink_tlv_msg_t *nested_msg = (knet_netlink_tlv_msg_t *)&msg->payload;

    msg->op_type = XPNL_TRAP_CONFIG_MSG;

    nested_msg->op_type = XPNL_TRAP_ID;
    nested_msg->msg_len = sizeof(uint32_t);
    memcpy(nested_msg->payload, &trap_cfg->trap_id, nested_msg->msg_len);
    tlv_len = sizeof(knet_netlink_tlv_msg_t) + nested_msg->msg_len;
    nested_msg = (knet_netlink_tlv_msg_t *) ((uint8_t *)nested_msg + tlv_len);

    nested_msg->op_type = XPNL_REASONCODE;
    nested_msg->msg_len = sizeof(uint32_t);
    memcpy(nested_msg->payload, &trap_cfg->rc, nested_msg->msg_len);
    tlv_len = sizeof(knet_netlink_tlv_msg_t) + nested_msg->msg_len;
    nested_msg = (knet_netlink_tlv_msg_t *) ((uint8_t *)nested_msg + tlv_len);

    nested_msg->op_type = XPNL_CHANNEL;
    nested_msg->msg_len = sizeof(uint32_t);
    memcpy(nested_msg->payload, &trap_cfg->channel, nested_msg->msg_len);
    tlv_len = sizeof(knet_netlink_tlv_msg_t) + nested_msg->msg_len;
    nested_msg = (knet_netlink_tlv_msg_t *) ((uint8_t *)nested_msg + tlv_len);

    nested_msg->op_type = XPNL_FD;
    nested_msg->msg_len = sizeof(uint32_t);
    memcpy(nested_msg->payload, &trap_cfg->socket_fd, nested_msg->msg_len);
    tlv_len = sizeof(knet_netlink_tlv_msg_t) + nested_msg->msg_len;
    nested_msg = (knet_netlink_tlv_msg_t *) ((uint8_t *)nested_msg + tlv_len);

    msg->msg_len = (uint8_t *)nested_msg - (uint8_t *)msg->payload +
                   sizeof(knet_netlink_tlv_msg_t);

    return nested_msg;
}
