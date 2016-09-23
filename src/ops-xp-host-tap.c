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
 * File: ops-xp-host-tap.c
 *
 * Purpose: This file contains OpenSwitch CPU TAP interface related
 *          application code for the Cavium/XPliant SDK.
 */

#if !defined(OPS_AS7512) && !defined(OPS_XP_SIM)
#define OPS_XP_SIM
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openvswitch/vlog.h>
#include <netinet/ether.h>
#include <time.h>
#include <sys/time.h>

#include "socket-util.h"
#include "ops-xp-util.h"
#include "ops-xp-host.h"
#include "ops-xp-dev.h"
#include "ops-xp-dev-init.h"
#include "openXpsPacketDrv.h"
#include "openXpsPort.h"


VLOG_DEFINE_THIS_MODULE(xp_host_tap);

static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(5, 20);

struct tap_if_entry {
    /* Node in a if_id_to_tap_if_map. */
    struct hmap_node if_id_node;
    /* Node in a fd_to_tap_if_map. */
    struct hmap_node fd_node;
    xpsInterfaceId_t if_id;
    /* ID of the interface which will be used for sending. */
    xpsInterfaceId_t send_if_id;
    int fd;
    char *name;
    bool filter_created;
};

struct tap_info {
    xpsDevice_t dev_id;
    /* Pipe which will notify TAP listener thread about
     * new TAP interface added. */
    int if_upd_fds[2];
    /* Exit pipe for TAP listener thread. */
    int exit_fds[2];
    struct ovs_mutex mutex;
    fd_set read_fd_set;
    /* XPS intf ID to tap_if_entry map. */
    struct hmap if_id_to_tap_if_map;
    /* FD to tap_if_entry map. */
    struct hmap fd_to_tap_if_map;
    /* Listener Thread ID. */
    pthread_t listener_thread;
};

static struct tap_if_entry *tap_get_if_entry_by_fd(struct tap_info *tap_info,
                                                   int fd);

static struct tap_if_entry *tap_get_if_entry_by_if_id(struct tap_info *tap_info,
                                                      xpsInterfaceId_t if_id);

static void *tap_listener(void *arg);

static XP_STATUS tap_packet_driver_cb(xpsDevice_t devId, xpsPort_t portNum,
                                      void *buf, uint16_t buf_size,
                                      void *userData);

static int tap_init(struct xpliant_dev *xp_dev);
static void tap_deinit(struct xpliant_dev *xp_dev);
static int tap_if_create(struct xpliant_dev *xp_dev, char *name,
                         xpsInterfaceId_t xps_if_id,
                         struct ether_addr *mac, int *xpnet_if_id);
static int tap_if_delete(struct xpliant_dev *xp_dev, int xpnet_if_id);
static int tap_if_filter_create(char *name, struct xpliant_dev *xp_dev,
                                xpsInterfaceId_t xps_if_id,
                                int host_if_id, int *host_filter_id);
static int tap_if_filter_delete(struct xpliant_dev *xp_dev, int host_filter_id);
static int tap_if_control_id_set(struct xpliant_dev *xp_dev,
                                 xpsInterfaceId_t xps_if_id, 
                                 int host_if_id, bool set);

const struct xp_host_if_api xp_host_tap_api = {
    tap_init,
    tap_deinit,
    tap_if_create,
    tap_if_delete,
    tap_if_filter_create,
    tap_if_filter_delete,
    tap_if_control_id_set
};

static int
tap_init(struct xpliant_dev *xp_dev)
{
    struct tap_info *info;
    XP_STATUS ret;

    ovs_assert(xp_dev);
    ovs_assert(xp_dev->host_if_info);

    info = xzalloc(sizeof *info);
    info->dev_id = xp_dev->id;

    ret = xpsPacketDriverFeatureRxHndlr(XP_MAX_CPU_RX_HDLR,
                                        tap_packet_driver_cb,
                                        (void *)xp_dev);
    if (ret != XP_NO_ERR) {
        VLOG_ERR("%s, Unable to register handler of trapped packets",
                 __FUNCTION__);
        free(info);
        return EFAULT;
    }

    /* Create control pipes and add their read fds to info->read_fd_set. */
    xpipe(info->if_upd_fds);
    xpipe(info->exit_fds);

    FD_SET(info->if_upd_fds[0], &info->read_fd_set);
    FD_SET(info->exit_fds[0], &info->read_fd_set);

    ovs_mutex_init_recursive(&info->mutex);
    hmap_init(&info->if_id_to_tap_if_map);
    hmap_init(&info->fd_to_tap_if_map);

    info->listener_thread = ovs_thread_create("ops-xp-tap-listener",
                                              tap_listener,
                                              info);
    VLOG_INFO("TAP listener thread started");

    xp_dev->host_if_info->data = info;

    return 0;
}

static void
tap_deinit(struct xpliant_dev *xp_dev)
{
    struct tap_if_entry *e;
    struct tap_if_entry *next;
    struct tap_info *info;
    XP_STATUS ret;

    ovs_assert(xp_dev);
    ovs_assert(xp_dev->host_if_info);

    XP_LOCK();
    info = (struct tap_info *)xp_dev->host_if_info->data;
    xp_dev->host_if_info->data = NULL;
    XP_UNLOCK();

    if (!info) {
        return;
    }

    ret = xpsPacketDriverFeatureRxHndlrDeRegister(XP_MAX_CPU_RX_HDLR);
    if (ret != XP_NO_ERR) {
        VLOG_ERR("%s, Unable to deregister handler of trapped packets",
                 __FUNCTION__);
    }

    /* Stop VPORT listener thread. */
    ignore(write(info->exit_fds[1], "", 1));
    xpthread_join(info->listener_thread, NULL);

    close(info->if_upd_fds[0]);
    close(info->if_upd_fds[1]);
    close(info->exit_fds[0]);
    close(info->exit_fds[1]);

    /* Remove xpnet interfaces. */
    HMAP_FOR_EACH_SAFE (e, next, fd_node, &info->fd_to_tap_if_map) {
        tap_if_delete(xp_dev, e->fd);
    }

    hmap_destroy(&info->if_id_to_tap_if_map);
    hmap_destroy(&info->fd_to_tap_if_map);

    ovs_mutex_destroy(&info->mutex);

    free(info);

    VLOG_INFO("Host interface TAP-based instance deallocated.");
}

static int
tap_if_create(struct xpliant_dev *xp_dev, char *name,
              xpsInterfaceId_t xps_if_id,
              struct ether_addr *mac, int *host_if_id)
{
    int err;
    int fd;
    char tap_if_name[IFNAMSIZ];
    struct tap_info *info;
    struct tap_if_entry *if_entry;

    ovs_assert(xp_dev);
    ovs_assert(xp_dev->host_if_info);

    info = (struct tap_info *)xp_dev->host_if_info->data;
    if (!info) {
        VLOG_ERR("VPORT not initialized for %d device.", xp_dev->id);
        return EFAULT;
    }

    snprintf(tap_if_name, IFNAMSIZ, "%s", name);
    fd = ops_xp_tun_alloc(tap_if_name, (IFF_TAP | IFF_NO_PI));
    if (fd <= 0) {
        VLOG_ERR("Unable to create %s device.", tap_if_name);
        return EFAULT;
    }

    err = set_nonblocking(fd);
    if (err) {
        VLOG_ERR("Unable to set %s device into nonblocking mode.", tap_if_name);
        close(fd);
        return EFAULT;
    }

    if (0 != ops_xp_net_if_setup(tap_if_name, mac)) {
        VLOG_ERR("Unable to setup %s interface.", tap_if_name);
        close(fd);
        return EFAULT;
    }

    *host_if_id = fd;

    if_entry = xzalloc(sizeof(*if_entry));
    if_entry->if_id = xps_if_id;
    if_entry->send_if_id = xps_if_id;
    if_entry->fd = fd;
    if_entry->name = xstrdup(tap_if_name);
    if_entry->filter_created = false;

    ovs_mutex_lock(&info->mutex);

    hmap_insert(&info->fd_to_tap_if_map, &if_entry->fd_node, if_entry->fd);

    /* Register interface fd in TAP read_fd_set. */
    FD_SET(fd, &info->read_fd_set);

    ovs_mutex_unlock(&info->mutex);

    /* Notify listener thread about new TAP interface. */
    ignore(write(info->if_upd_fds[1], "", 1));

    return 0;
}

static int
tap_if_delete(struct xpliant_dev *xp_dev, int host_if_id)
{
    struct tap_info *info;
    struct tap_if_entry *if_entry;

    ovs_assert(xp_dev);
    ovs_assert(xp_dev->host_if_info);

    info = (struct tap_info *)xp_dev->host_if_info->data;
    if (!info) {
        VLOG_ERR("Host IF not initialized for %d device.", xp_dev->id);
        return EFAULT;
    }

    if (host_if_id <= 0) {
        VLOG_ERR("Invalid TAP interface ID %d specified.", host_if_id);
        return EINVAL;
    }

    ovs_mutex_lock(&info->mutex);

    if_entry = tap_get_if_entry_by_fd(info, host_if_id);
    if (!if_entry) {
        ovs_mutex_unlock(&info->mutex);
        return ENOENT;
    }

    tap_if_filter_delete(xp_dev, if_entry->if_id + 1);

    hmap_remove(&info->fd_to_tap_if_map, &if_entry->fd_node);

    /* Clear interface socket in TAP read_fd_set. */
    FD_CLR(if_entry->fd, &info->read_fd_set);

    ovs_mutex_unlock(&info->mutex);

    ops_xp_system("/sbin/ifconfig %s down", if_entry->name);

    free(if_entry->name);

    close(host_if_id);
    free(if_entry);

    return 0;
}

static int
tap_if_filter_create(char *name, struct xpliant_dev *xp_dev,
                     xpsInterfaceId_t xps_if_id,
                     int host_if_id, int *host_filter_id)
{
    struct tap_info *info;
    struct tap_if_entry *if_entry;
    XP_STATUS status = XP_NO_ERR;

    ovs_assert(xp_dev);
    ovs_assert(xp_dev->host_if_info);

    info = (struct tap_info *)xp_dev->host_if_info->data;
    if (!info) {
        VLOG_ERR("Host IF not initialized for %d device.", xp_dev->id);
        return EFAULT;
    }

    if (host_if_id <= 0) {
        VLOG_ERR("Invalid TAP interface ID %d specified.", host_if_id);
        return EINVAL;
    }

    ovs_mutex_lock(&info->mutex);

    if_entry = tap_get_if_entry_by_fd(info, host_if_id);
    if (!if_entry) {
        ovs_mutex_unlock(&info->mutex);
        return ENOENT;
    }

    if(if_entry->filter_created)
    {
        /* Filter already present */
        ovs_mutex_unlock(&info->mutex);
        return 0;
    }

    *host_filter_id = xps_if_id + 1;
    if_entry->filter_created = true;

    hmap_insert(&info->if_id_to_tap_if_map, &if_entry->if_id_node,
                if_entry->if_id);

    ovs_mutex_unlock(&info->mutex);

    return 0;
}

static int
tap_if_filter_delete(struct xpliant_dev *xp_dev, int host_filter_id)
{
    struct tap_info *info;
    struct tap_if_entry *if_entry;

    ovs_assert(xp_dev);
    ovs_assert(xp_dev->host_if_info);

    info = (struct tap_info *)xp_dev->host_if_info->data;
    if (!info) {
        VLOG_ERR("Host IF not initialized for %d device.", xp_dev->id);
        return EFAULT;
    }

    if (host_filter_id <= 0) {
        VLOG_ERR("Invalid TAP interface ID %d specified.", host_filter_id);
        return EINVAL;
    }

    ovs_mutex_lock(&info->mutex);

    if_entry = tap_get_if_entry_by_if_id(info, host_filter_id - 1);
    if (!if_entry) {
        ovs_mutex_unlock(&info->mutex);
        return ENOENT;
    }

    hmap_remove(&info->if_id_to_tap_if_map, &if_entry->if_id_node);

    if_entry->filter_created = false;

    ovs_mutex_unlock(&info->mutex);

    return 0;
}

static int
tap_if_control_id_set(struct xpliant_dev *xp_dev,
                      xpsInterfaceId_t xps_if_id, 
                      int host_if_id, bool set)
{
    xpsInterfaceType_e if_type;
    XP_STATUS status = XP_NO_ERR;
    struct tap_info *info;
    struct tap_if_entry *if_entry;

    ovs_assert(xp_dev);
    ovs_assert(xp_dev->host_if_info);

    info = (struct tap_info *)xp_dev->host_if_info->data;
    if (!info) {
        VLOG_ERR("Host IF not initialized for %d device.", xp_dev->id);
        return EFAULT;
    }

    if (host_if_id <= 0) {
        VLOG_ERR("Invalid TAP interface ID %d specified.", host_if_id);
        return EINVAL;
    }

    ovs_mutex_lock(&info->mutex);

    if_entry = tap_get_if_entry_by_fd(info, host_if_id);
    if (!if_entry) {
        ovs_mutex_unlock(&info->mutex);
        return ENOENT;
    }

    if (set) {
        status = xpsInterfaceGetType(xps_if_id, &if_type);
        if (status != XP_NO_ERR) {
            ovs_mutex_unlock(&info->mutex);
            VLOG_ERR("%s, Failed to get interface type. Error: %d", 
                     __FUNCTION__, status);
            return EPERM;
        }

        if (if_type != XPS_PORT) {
            ovs_mutex_unlock(&info->mutex);
            return 0;
        }

        status = xpsPortGetPortControlIntfId(xp_dev->id, xps_if_id,
                                             &if_entry->send_if_id);
        if (status) {
            ovs_mutex_unlock(&info->mutex);
            VLOG_ERR("%s, Unable to get port control interface ID for "
                     "interface: %u. Error: %d",
                     __FUNCTION__, xps_if_id, status);
            return EPERM;
        }
    } else {
        if_entry->send_if_id = xps_if_id;
    }

    ovs_mutex_unlock(&info->mutex);

    return 0;
}

/* Finds tap_if_entry using fd. */
static struct tap_if_entry *
tap_get_if_entry_by_fd(struct tap_info *tap_info, int fd)
{
    struct hmap_node *e;

    if (!tap_info || (fd <= 0)) {
        VLOG_ERR("%s, Invalid data.", __FUNCTION__);
        return NULL;
    }

    e = hmap_first_with_hash(&tap_info->fd_to_tap_if_map, fd);
    if (e) {
        return CONTAINER_OF(e, struct tap_if_entry, fd_node);
    }

    return NULL;
}

/* Finds tap_if_entry using xps interface id. */
static struct tap_if_entry *
tap_get_if_entry_by_if_id(struct tap_info *tap_info, xpsInterfaceId_t if_id)
{
    struct hmap_node *e;

    if (!tap_info || (if_id < 0)) {
        VLOG_ERR("%s, Invalid data.", __FUNCTION__);
        return NULL;
    }

    e = hmap_first_with_hash(&tap_info->if_id_to_tap_if_map, if_id);
    if (e) {
        return CONTAINER_OF(e, struct tap_if_entry, if_id_node);
    }

    return NULL;
}

/* Handles packets received from TAP interfaces. */
static void *
tap_listener(void *arg)
{
    char *buf;
    int ret;
    struct tap_info *info = arg;
    struct timeval init_time;
    const uint8_t INIT_DRAIN_TIME = 5;
    bool time_initialized = false;

    if (!info) {
        VLOG_ERR("No VPORT specified.");
        return NULL;
    }

    buf = xzalloc(RX_MAX_FRM_LEN_MAX_VAL);

    /* Handling loop. */
    while (1) {
        int bytes_recv;
        int i;

        ovs_mutex_lock(&info->mutex);
        fd_set read_fd_set = info->read_fd_set;
        ovs_mutex_unlock(&info->mutex);

        if (select(FD_SETSIZE, &read_fd_set, NULL, NULL, NULL) < 0) {
            VLOG_ERR("%s: Select failed. Error(%d) - %s",
                     __FUNCTION__, errno, strerror(errno));
            free(buf);
            return NULL;
        }

        for (i = 0; i < FD_SETSIZE; i++) {
            if (FD_ISSET (i, &read_fd_set)) {

                struct tap_if_entry *if_entry;
                xpsInterfaceId_t egress_if_id;
                struct timeval cur_time, delta_time;

                do {
                    bytes_recv = read(i, buf, RX_MAX_FRM_LEN_MAX_VAL);
                } while ((bytes_recv < 0) && (errno == EINTR));

                if ((bytes_recv < 0) && (errno != EWOULDBLOCK)) {
                    VLOG_WARN("%s, Read from recv socket failed. Error(%d) - %s",
                              __FUNCTION__, errno, strerror(errno));
                    continue;
                }

                if (i == info->exit_fds[0]) {
                    VLOG_INFO("TAP listener thread finished.");
                    free(buf);
                    return NULL;
                } else if (i == info->if_upd_fds[0]) {
                    /* New TAP interface has been added so
                     * need to listen to it as well. */
                    VLOG_INFO("%s, New TAP interface added for listening.",
                              __FUNCTION__);
                    continue;
                }

                ovs_mutex_lock(&info->mutex);
                /* Figure out egress interface ID. */
                if_entry = tap_get_if_entry_by_fd(info, i);
                if (!if_entry || !if_entry->filter_created) {
                    ovs_mutex_unlock(&info->mutex);
                    continue;
                }
                egress_if_id = if_entry->send_if_id;

                ovs_mutex_unlock(&info->mutex);

                if (!time_initialized) {
                    gettimeofday(&init_time, NULL);
                    time_initialized = true;
                }

                gettimeofday(&cur_time, NULL);
                timersub(&cur_time, &init_time, &delta_time);

                if (delta_time.tv_sec < INIT_DRAIN_TIME) {
                    VLOG_WARN_RL(&rl, "%s, Drain %u bytes from TAP interface %u",
                                 __FUNCTION__, bytes_recv, egress_if_id);
                    continue;
                }

                VLOG_DBG("%s, Received packet of %d bytes (dst MAC: "
                         ETH_ADDR_FMT") on TAP interface %u.",
                         __FUNCTION__, bytes_recv,
                         ETH_ADDR_BYTES_ARGS((uint8_t *)buf),
                         egress_if_id);

                /* Send packet to host. */
                ops_xp_dev_send(info->dev_id, egress_if_id, buf, bytes_recv);

            } /* if (FD_ISSET (i, &read_fd_set)) */
        } /* for (i = 0; i < FD_SETSIZE; i++) */
    } /* while (1) */

    return NULL;
}

/* Packet driver callback which sends packets received from host interface
 * to corresponding TAP interfaces. */
static XP_STATUS
tap_packet_driver_cb(xpsDevice_t devId, xpsPort_t portNum,
                     void *buf, uint16_t buf_size, void *userData)
{
    XP_STATUS status;
    int ret;
    int fd;
    struct tap_if_entry *if_entry;
    xpsInterfaceId_t if_id;
    struct xpliant_dev *dev = (struct xpliant_dev *)userData;
    struct tap_info *info;

    if (!dev || !dev->host_if_info) {
        return XP_ERR_INVALID_PARAMS;
    }

    info = (struct tap_info *)dev->host_if_info->data;

    status = xpsPortGetPortIntfId(devId, portNum, &if_id);
    if (status != XP_NO_ERR) {
        VLOG_ERR("%s, Unable to get interface ID for a port: %u",
                 __FUNCTION__, portNum);
        return status;
    }

    ovs_mutex_lock(&info->mutex);

    /* Figure out fd for this interface ID. */
    if_entry = tap_get_if_entry_by_if_id(info, if_id);
    if (!if_entry) {
        /* If no fd is attached then silently ignore the packet */
        VLOG_INFO("%s, Unable to get TAP interface entry for a port: %u",
                  __FUNCTION__, portNum);
        ovs_mutex_unlock(&info->mutex);
        return XP_NO_ERR;
    }
    fd = if_entry->fd;

    VLOG_DBG("%s, Sending packet of %d bytes to TAP interface %u.",
             __FUNCTION__, buf_size, if_id);

    ovs_mutex_unlock(&info->mutex);

    /* Send a packet to xpnet interface. */
    do {
        ret = write(fd, buf, buf_size);
    } while ((ret < 0) && (errno == EINTR));

    if (ret < 0) {
        VLOG_ERR_RL(&rl, "%s, Unable to send packet to TAP interface %u. "
                    "Error(%d) - %s",
                    __FUNCTION__, if_entry->if_id, errno, strerror(errno));
        return XP_ERR_SOCKET_SEND;
    }

    return XP_NO_ERR;
}
