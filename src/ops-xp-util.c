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
 * File: ops-xp-util.c
 *
 * Purpose: This file contains OpenSwitch utilities related application code
 *          for the Cavium/XPliant SDK.
 */

#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/ethernet.h>
#include <netinet/ether.h>
#include <netpacket/packet.h>
#include <sys/select.h>

#include "smap.h"
#include "socket-util.h"
#include "ops-xp-util.h"
#include "openXpsPort.h"


VLOG_DEFINE_THIS_MODULE(xp_util);


/* Returns the new bitmap as a result of XOR operation
 * of 'n' bits of bitmaps 'a' and 'b'. */
unsigned long *
ops_xp_bitmap_xor(const unsigned long *a, const unsigned long *b, size_t n)
{
    size_t i, longs;
    unsigned long *bitmap_xor;

    if (!a || !b) {
        bitmap_xor = a ? bitmap_clone(a, n) :
                        (b ? bitmap_clone(b, n) : NULL);
        return bitmap_xor;
    }

    bitmap_xor = bitmap_allocate(n);
    if (bitmap_xor == NULL) {
        return NULL;
    }

    longs = bitmap_n_longs(n);
    for (i = 0; i < longs; i++) {
        bitmap_xor[i] = a[i] ^ b[i];
    }

    return bitmap_xor;
}

/* Executes a command composed from @format string and va_list
 * by calling system() function from standard library.
 * Returns after the command has been completed. */
int
ops_xp_system(const char *format, ...)
{
    va_list arg;
    char cmd[256];
    int ret;

    va_start(arg, format);
    ret = vsnprintf(cmd, sizeof(cmd), format, arg);
    va_end(arg);

    if (ret < 0) {
        VLOG_ERR("Failed to parse command.",
                cmd, WEXITSTATUS(ret));
        return EFAULT;
    }

    cmd[sizeof(cmd) - 1] = '\0';
    ret = system(cmd);

    VLOG_INFO("Execute command \"%s\"", cmd);

    if (WIFEXITED(ret)) {
        if (!WEXITSTATUS(ret)) {
            return 0;
        }

        VLOG_ERR("Failed to execute \"%s\". Exit status (%d)",
                cmd, WEXITSTATUS(ret));
        return EFAULT;
    }

    if (WIFSIGNALED(ret)) {
        VLOG_ERR("Execution of \"%s\" has been terminated by signal %d",
                cmd, WTERMSIG(ret));
        return EFAULT;
    }

    VLOG_ERR("Failed to execute \"%s\". RC=%d", cmd, ret);
    return EFAULT;
}

/* Opens RAW (packet) socket and binds it to an interface with if_name.
 * Returns:
 * On success, a file descriptor for the new socket is returned. On error, -1
 * is returned, and errno is set appropriately. */
int
ops_xp_packet_sock_open(char *if_name)
{
    int ret = 0;
    int sock = -1;

    if (!if_name) {
        errno = EINVAL;
        return -1;
    }

    sock = socket(PF_PACKET, SOCK_RAW, ETH_P_ALL);
    if (sock == -1) {
        VLOG_ERR("socket() failed with error: %d - %s", errno, strerror(errno));
        return -1;
    }

    struct ifreq ifr;
    strncpy((char *)ifr.ifr_name, if_name, IFNAMSIZ);

    ret = ioctl(sock, SIOCGIFINDEX, &ifr);
    if (ret == -1) {
        VLOG_ERR("Failed to set SIOCGIFINDEX attribute for a packet socket."
                 "Error: %d - %s", errno, strerror(errno));
        close(sock);
        return -1;
    }

    struct sockaddr_ll sll;
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = ifr.ifr_ifindex;
    sll.sll_protocol = htons(ETH_P_ALL);

    ret = bind(sock, (struct sockaddr *)&sll, sizeof (sll));
    if (ret == -1) {
        VLOG_ERR("Failed to bind packet socket to interface %s. Error: %d - %s",
                  if_name, errno, strerror(errno));
        close(sock);
        return -1;
    }

    return sock;
}

/* Convert from string to ipv4/ipv6 prefix */
int
ops_xp_string_to_prefix(int family, const char *ip_address, void *prefix,
                        unsigned char *prefixlen)
{
    char *p;
    char *tmp_ip_addr;
    int maxlen = (family == AF_INET) ? 32 : 128;

    *prefixlen = maxlen;
    tmp_ip_addr = xstrdup(ip_address);

    if ((p = strchr(tmp_ip_addr, '/'))) {
        *p++ = '\0';
        *prefixlen = atoi(p);
    }

    if (*prefixlen > maxlen) {
        VLOG_DBG("Bad prefixlen %d > %d", *prefixlen, maxlen);
        free(tmp_ip_addr);
        return EINVAL;
    }

    if (family == AF_INET) {
        /* ipv4 address in host order */
        in_addr_t *addr = (in_addr_t *)prefix;
        *addr = inet_network(tmp_ip_addr);
        if (*addr == -1) {
            VLOG_ERR("Invalid ip address %s", ip_address);
            free(tmp_ip_addr);
            return EINVAL;
        }
    } else {
        /* ipv6 address */
        if (inet_pton(family, tmp_ip_addr, prefix) == 0) {
            VLOG_DBG("%d inet_pton failed with %s", family, strerror(errno));
            free(tmp_ip_addr);
            return EINVAL;
        }
    }

    free(tmp_ip_addr);
    return 0;
}

int
ops_xp_parse_ip_str(const char *ip_str, ovs_be32 *ip)
{
    int ret = 0;
    struct in_addr addr;

    ovs_assert(ip_str);
    ovs_assert(ip);

    ret = lookup_ip(ip_str, &addr);
    if (ret) {
        return ret;
    }

    if (ip_is_multicast(addr.s_addr)) {
        VLOG_ERR("multicast IP %s not allowed", ip_str);
        return EINVAL;
    }

    *ip = addr.s_addr;

    return 0;
}

int
ops_xp_parse_netmask_str(const char *netmask_str, ovs_be32 *mask)
{
    struct in_addr netmask;

    ovs_assert(netmask_str);
    ovs_assert(mask);

    if (lookup_ip(netmask_str, &netmask)) {
        VLOG_ERR("Invalid netmask %s", netmask_str);
        return EINVAL;
    }

    *mask = netmask.s_addr;

    return 0;
}

int
ops_xp_linux_route_update(bool route_add, const char *prefix,
                          const char *nh, bool nh_port)
{
    VLOG_DBG("%s: %s route %s nexthop %s",
             __FUNCTION__, route_add ? "Add" : "Delete", prefix, nh);

    return ops_xp_system("route %s -net %s %s %s",
                         (route_add ? "add" : "del"), prefix,
                         (nh_port ? "dev" : "gw"), nh);
}

bool
ops_xp_is_l3_packet(void* buf, uint16_t bufSize)
{
    struct ethhdr *eth = (struct ethhdr*)buf;

    if (!buf || bufSize < sizeof(struct ethhdr)){
        return false;
    }

    if ((ntohs(eth->h_proto) == ETH_TYPE_IP) ||
        (ntohs(eth->h_proto) == ETH_TYPE_ARP)) {
        return true;
    }

    return false;
}

bool
ops_xp_is_arp_packet(void* buf, uint16_t bufSize)
{
    struct ethhdr *eth = (struct ethhdr*)buf;

    if (!buf || 
        bufSize < (sizeof(struct ethhdr) + sizeof(struct arp_hdr))){
        return false;
    }

    if (ntohs(eth->h_proto) == ETH_TYPE_ARP) {
        return true;
    }

    return false;
}

bool
ops_xp_is_ip_packet(void* buf, uint16_t bufSize)
{
    struct ethhdr *eth = (struct ethhdr*)buf;

    if (!buf || bufSize < sizeof(struct ethhdr)){
        return false;
    }

    if (ntohs(eth->h_proto) == ETH_TYPE_IP) {
        return true;
    }

    return false;
}

/* Copies src mac into dst mac in reverse order. */
void
ops_xp_mac_copy_and_reverse(uint8_t *dst_mac, const uint8_t *src_mac)
{
    int i = 0;
    int j = 0;

    ovs_assert(dst_mac);
    ovs_assert(src_mac);

    for (i = 0, j = (ETH_ADDR_LEN - 1); i < ETH_ADDR_LEN; i++, j--) {
        dst_mac[i] = src_mac[j];
    }
}

void
ops_xp_ip_addr_copy_and_reverse(uint8_t* dst_ip, const uint8_t* src_ip,
                                bool is_ipv6_addr)
{
    int i = 0;
    int j = 0;
    int ip_addr_size;

    ovs_assert(dst_ip);
    ovs_assert(src_ip);

    ip_addr_size = is_ipv6_addr ? sizeof(ipv6Addr_t) : sizeof(ipv4Addr_t);

    for (i = 0, j = (ip_addr_size - 1); i < ip_addr_size; i++, j--) {
        dst_ip[i] = src_ip[j];
    }
}

uint8_t
ops_xp_netmask_len_get(ovs_be32 netmask)
{
    uint8_t i = 0;

    while (netmask > 0) {
        netmask = netmask >> 1;
        i++;
    }

    return i;
}

bool
ops_xp_is_tunnel_intf(xpsInterfaceId_t if_id)
{
    xpsInterfaceType_e if_type = 0;
    int ret = 0;

    ret = xpsInterfaceGetType(if_id, &if_type);
    if (ret != XP_NO_ERR) {
        return false;
    }

    switch (if_type) {
    case XPS_TUNNEL_MPLS:
    case XPS_TUNNEL_VXLAN:
    case XPS_TUNNEL_NVGRE:
    case XPS_TUNNEL_GENEVE:
    case XPS_TUNNEL_PBB:
    case XPS_TUNNEL_GRE:
    case XPS_TUNNEL_IP_IN_IP:
        return true;

    default:
        return false;
    }

    return false;
}

bool
ops_xp_is_l2_tunnel_intf(xpsInterfaceId_t if_id)
{
    xpsInterfaceType_e if_type = 0;
    int ret = 0;

    ret = xpsInterfaceGetType(if_id, &if_type);
    if (ret != XP_NO_ERR) {
        return false;
    }

    switch (if_type) {
    case XPS_TUNNEL_VXLAN:
    case XPS_TUNNEL_NVGRE:
    case XPS_TUNNEL_GENEVE:
        return true;

    default:
        return false;
    }

    return false;
}

bool 
ops_xp_is_l3_tunnel_intf(xpsInterfaceId_t if_id)
{
    xpsInterfaceType_e if_type = 0;
    int ret = 0;

    ret = xpsInterfaceGetType(if_id, &if_type);
    if (ret != XP_NO_ERR) {
        return false;
    }

    switch (if_type) {
    case XPS_TUNNEL_GRE:
    case XPS_TUNNEL_IP_IN_IP:
        return true;

    default:
        return false;
    }

    return false;
}

bool
ops_xp_msleep(uint32_t msec)
{
    struct timeval tv = {msec / 1000, (msec % 1000) * 1000};
    select(0, NULL, NULL, NULL, &tv);
}

/* Creates Linux tun/tap interface.
 *
 * Arguments taken by the function:
 * name:  The name of an interface (or '\0'). MUST have enough
 *        space to hold the interface name if '\0' is passed.
 * flags: IFF_TUN   - TUN device (no Ethernet headers)
 *        IFF_TAP   - TAP device
 *
 *        IFF_NO_PI - Do not provide packet information
 */
int
ops_xp_tun_alloc(char *name, int flags)
{

    struct ifreq ifr;
    int fd, err;
    static const char tun_dev[] = "/dev/net/tun";

    /* Open the clone device */
    fd = open(tun_dev, O_RDWR);
    if (fd < 0) {
        VLOG_WARN("opening \"%s\" failed: %s", tun_dev, ovs_strerror(errno));
        return -errno;
    }

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = flags;
    if (*name) {
        /* if a device name was specified, put it in the structure; otherwise,
         * the kernel will try to allocate the "next" device of the
         * specified type */
        ovs_strzcpy(ifr.ifr_name, name, sizeof ifr.ifr_name);
    }

    /* try to create the device */
    err = ioctl(fd, TUNSETIFF, &ifr);
    if (err < 0) {
        VLOG_WARN("%s: creating tap device failed: %s", name,
                  ovs_strerror(errno));
        close(fd);
        return -errno;
    }

    /* Write back the name of the interface */
    strcpy(name, ifr.ifr_name);
    return fd;
}

int
ops_xp_net_if_setup(char *intf_name, struct ether_addr *mac)
{
    int  rc = 0;
    char buf[32] = {0};

    /* Bring the Ethernet interface DOWN. */
    rc = ops_xp_system("/sbin/ifconfig %s down", intf_name);
    if (rc != 0) {
        VLOG_ERR("Failed to bring down %s interface. (rc=%d)",
                 intf_name, rc);
        return EFAULT;
    }

    /* Set MAC address for the Ethernet interface. */
    rc = ops_xp_system("/sbin/ip link set %s address %s",
                       intf_name, ether_ntoa_r(mac, buf));
    if (rc != 0) {
        VLOG_ERR("Failed to set MAC address for %s interface. (rc=%d)",
                 intf_name, rc);
        return EFAULT;
    }

    VLOG_INFO("Set MAC address for %s to %s", intf_name, buf);

    /* Bring the Ethernet interface UP. */
    rc = ops_xp_system("/sbin/ifconfig %s up", intf_name);
    if (rc != 0) {
        VLOG_ERR("Failed to bring up %s interface. (rc=%d)",
                 intf_name, rc);
        return EFAULT;
    }

    return 0;
}

int
ops_xp_port_default_vlan_set(xpsDevice_t dev_id, xpsPort_t port_num,
                             xpsVlan_t vlan_id)
{
    XP_STATUS status = XP_NO_ERR;
    xpsPortConfig_t port_cfg;

    VLOG_INFO("set_port_vlan_id: port#%u, vid#%u",
              port_num, vlan_id);

    status = xpsPortGetConfig(dev_id, port_num, &port_cfg);
    if (status) {
        VLOG_ERR("Could not get current config "
                 "for the port: %u. Error code: %d",
                 port_num, status);
        return EPERM;
    }

    if ((port_cfg.pvid != vlan_id) ||
        (port_cfg.pvidModeAllPkt != 1)) {

        port_cfg.pvidModeAllPkt = 1;
        port_cfg.pvid = vlan_id;

        status = xpsPortSetConfig(dev_id, port_num, &port_cfg);
        if (status) {
            VLOG_ERR("Could not set default VLAN "
                     "for the port: %u. Error code: %d",
                     port_num, status);
            return EPERM;
        }
    }

    return 0;
}
