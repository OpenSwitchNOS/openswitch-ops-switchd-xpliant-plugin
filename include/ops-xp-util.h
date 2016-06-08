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
 * File: ops-xp-util.h
 *
 * Purpose: This file provides public definitions for OpenSwitch utilities
 *          related application code for the Cavium/XPliant SDK.
 */

#ifndef OPS_XP_UTIL_H
#define OPS_XP_UTIL_H 1

#include <time.h>
#include <sys/time.h>
#include <openvswitch/vlog.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <linux/if_tun.h>
#include <ovs/packets.h>
#include <ovs/dynamic-string.h>
#include <ovs/bitmap.h>
#include <unistd.h>
#include <fcntl.h>

#include "openXpsInterface.h"
#include "openXpsTypes.h"

#define STR_EQ(s1, s2) ((s1 != NULL) && (s2 != NULL) && \
                        (strlen((s1)) == strlen((s2))) && \
                        (!strncmp((s1), (s2), strlen((s2)))))

#define XP_TRACE()   \
     VLOG_DBG(OVS_SOURCE_LOCATOR " -- %s", __func__)

#define XP_TRACE_RL(rl)   \
     VLOG_DBG_RL(&rl, OVS_SOURCE_LOCATOR " -- %s", __func__)

#define EXEC_DELAY_BEGIN() \
{ \
    struct timeval t1,t2,r; \
    gettimeofday(&t1, NULL);

#define EXEC_DELAY_VLOG(level) \
    gettimeofday(&t2, NULL); \
    timersub(&t2, &t1, &r); \
    VLOG_##level("%s line %u DELAY %u.%06u\n", \
                 __FUNCTION__, __LINE__, r.tv_sec, r.tv_usec);

#define EXEC_DELAY_END() \
}

#define EXEC_DELAY_GET(func, sec, usec) \
{ \
    struct timeval t1,t2,r; \
    gettimeofday(&t1, NULL); \
    func; \
    gettimeofday(&t2, NULL); \
    timersub(&t2, &t1, &r); \
    sec = r.tv_sec; \
    usec = r.tv_usec; \
}

#define EXEC_DELAY_PRINT(level, func) \
{ \
    uint32_t sec, usec; \
    EXEC_DELAY_GET(func, sec, usec) \
    VLOG_##level(#func " API takes %u.%06u\n", sec, usec); \
}

/* Writes to the log with one of standard log 'level' (DBG, INFO, WARN, etc)
 * the 'size' bytes in 'buf' to 'string' as hex bytes arranged 16 per
 * line.  Numeric offsets are also included, starting at 'offset' for the first
 * byte in 'buf'.  If 'ascii' is true then the corresponding ASCII characters
 * are also rendered alongside. */
#define VLOG_MEM(level, buf, size, offset, ascii) \
{ \
    struct ds result; \
    char *save_ptr = NULL; \
    char *line; \
    ds_init(&result); \
    ds_put_hex_dump(&result, buf, size, offset, ascii); \
    for (line = strtok_r(ds_cstr(&result), "\n", &save_ptr); line; \
         line = strtok_r(NULL, "\n", &save_ptr)) { \
        VLOG_##level("%s", line); \
    } \
    ds_destroy(&result); \
}


#define XP_MAC_ADDR_SCAN_FMT ETH_ADDR_SCAN_FMT
#define XP_MAC_ADDR_SCAN_ARGS(mac) \
        &(mac)[5], &(mac)[4], &(mac)[3], &(mac)[2], &(mac)[1], &(mac)[0]

#define XP_ETH_ADDR_FMT ETH_ADDR_FMT
#define XP_ETH_ADDR_ARGS(mac) \
        (mac)[5], (mac)[4], (mac)[3], (mac)[2], (mac)[1], (mac)[0]

struct arp_hdr {
    ovs_be16 hw_type;               /* Hardware type */ 
    ovs_be16 pr_type;               /* Protocol type */ 
    uint8_t  hw_size;               /* Hardware size */ 
    uint8_t  pr_size;               /* Protocol size */ 
    ovs_be16 op_code;               /* Opcode */ 
    uint8_t  s_mac[ETH_ADDR_LEN];   /* Sender MAC address */ 
    ovs_be32 s_ip;                  /* Sender IP address  */ 
    uint8_t  t_mac[ETH_ADDR_LEN];   /* Target MAC address */ 
    ovs_be32 t_ip;                  /* Target IP address  */
}__attribute__((packed));

/* Max number for a frame(value taken from XDK) */
#define RX_MAX_FRM_LEN_MAX_VAL   16384

#define XP_IP_STR                "ip"
#define XP_NETMASK_STR           "netmask"

#define XP_STR_IP_BUF_LEN        20
#define XP_STR_MAC_BUF_LEN       20

static inline bool
ipv6_addr_is_zero(const struct in6_addr *addr)
{
    uint32_t i;
    for (i = 0; i < 16; i++) {
        if (((uint8_t *)addr)[i]) return false;
    }
    return true;
}

int ops_xp_system(const char *format, ...);
unsigned long *ops_xp_bitmap_xor(const unsigned long *a,
                                 const unsigned long *b, size_t n);
int ops_xp_packet_sock_open(char *if_name);
int ops_xp_string_to_prefix(int family, const char *ip_address, void *prefix,
                            unsigned char *prefixlen);
int ops_xp_parse_ip_str(const char *ip_str, ovs_be32 *ip);
int ops_xp_parse_netmask_str(const char *netmask_str, ovs_be32 *mask);
int ops_xp_linux_route_update(bool route_add, const char *prefix,
                              const char *nh, bool nh_port);
bool ops_xp_is_l3_packet(void *buf, uint16_t bufSize);
bool ops_xp_is_arp_packet(void *buf, uint16_t bufSize);
bool ops_xp_is_ip_packet(void *buf, uint16_t bufSize);
void ops_xp_mac_copy_and_reverse(uint8_t *dst, const uint8_t *src);
void ops_xp_ip_addr_copy_and_reverse(uint8_t *dst_ip, const uint8_t *src_ip,
                                     bool is_ipv6_addr);
uint8_t ops_xp_netmask_len_get(ovs_be32 netmask);
bool ops_xp_is_tunnel_intf(xpsInterfaceId_t if_id);
bool ops_xp_is_l2_tunnel_intf(xpsInterfaceId_t if_id);
bool ops_xp_is_l3_tunnel_intf(xpsInterfaceId_t if_id);
bool ops_xp_msleep(uint32_t msec);
int ops_xp_tun_alloc(char *name, int flags);
int ops_xp_net_if_setup(char *intf_name, struct ether_addr *mac);
int ops_xp_port_default_vlan_set(xpsDevice_t dev_id, xpsPort_t port_num,
                                 xpsVlan_t vlan_id);

#endif /* ops-xp-util.h */
