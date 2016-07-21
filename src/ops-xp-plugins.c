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
 * File: ops-xp-plugins.c
 *
 * Purpose: This file contains OpenSwitch plugin related application code
 *          for the Cavium/XPliant SDK.
 */

#include <openvswitch/vlog.h>
#include <netdev-provider.h>

#include "ops-xp-netdev.h"
#include "ops-xp-ofproto-provider.h"
#include "ops-xp-mac-learning.h"
#include "ops-xp-stg.h"
#include "ops-xp-copp.h"
#include "ops-xp-qos.h"
#include "ops-xp-classifier.h"
#include "plugin-extensions.h"
#include "asic-plugin.h"
#include "copp-asic-provider.h"
#include "qos-asic-provider.h"
#include "ops-cls-asic-plugin.h"

#define init libovs_xpliant_plugin_LTX_init
#define run libovs_xpliant_plugin_LTX_run
#define wait libovs_xpliant_plugin_LTX_wait
#define destroy libovs_xpliant_plugin_LTX_destroy
#define netdev_register libovs_xpliant_plugin_LTX_netdev_register
#define ofproto_register libovs_xpliant_plugin_LTX_ofproto_register

VLOG_DEFINE_THIS_MODULE(xp_plugin);

struct asic_plugin_interface xpliant_interface = {
    /* The new functions that need to be exported, can be declared here */
    .create_stg = &ops_xp_create_stg,
    .delete_stg = &ops_xp_delete_stg,
    .add_stg_vlan = &ops_xp_add_stg_vlan,
    .remove_stg_vlan = &ops_xp_remove_stg_vlan,
    .set_stg_port_state = &ops_xp_set_stg_port_state,
    .get_stg_port_state = &ops_xp_get_stg_port_state,
    .get_stg_default = &ops_xp_get_stg_default,
    .get_mac_learning_hmap = &ops_xp_mac_learning_hmap_get,
};

static struct plugin_extension_interface xpliant_extension = {
    ASIC_PLUGIN_INTERFACE_NAME,
    ASIC_PLUGIN_INTERFACE_MAJOR,
    ASIC_PLUGIN_INTERFACE_MINOR,
    (void *)&xpliant_interface
};

static struct copp_asic_plugin_interface copp_xpliant_interface = {
    /*
     * The function pointers are set to the interfacing functions
     * implemented by copp module in the xpliant-plugin
     */
    .copp_stats_get = &ops_xp_copp_stats_get,
    .copp_hw_status_get = &ops_xp_copp_hw_status_get,
};

static struct plugin_extension_interface copp_xpliant_extension = {
    COPP_ASIC_PLUGIN_INTERFACE_NAME,
    COPP_ASIC_PLUGIN_INTERFACE_MAJOR,
    COPP_ASIC_PLUGIN_INTERFACE_MINOR,
    (void *)&copp_xpliant_interface
};

static struct qos_asic_plugin_interface qos_xpliant_interface = {
    /*
     * The function pointers are set to the interfacing functions
     * implemented by QoS module in the xpliant-plugin
     */
    .set_port_qos_cfg = &ops_xp_qos_set_port_qos_cfg,
    .set_cos_map = &ops_xp_qos_set_cos_map,
    .set_dscp_map = &ops_xp_qos_set_dscp_map,
    .apply_qos_profile = &ops_xp_qos_apply_qos_profile,
};

static struct plugin_extension_interface qos_xpliant_extension = {
    QOS_ASIC_PLUGIN_INTERFACE_NAME,
    QOS_ASIC_PLUGIN_INTERFACE_MAJOR,
    QOS_ASIC_PLUGIN_INTERFACE_MINOR,
    (void *)&qos_xpliant_interface
};

static struct ops_cls_plugin_interface cls_xpliant_interface = {
    /*
     * The function pointers are set to the interfacing functions
     * implemented by classifier module in the xpliant-plugin
     */
    .ofproto_ops_cls_apply = &ops_xp_cls_apply,
    .ofproto_ops_cls_remove = &ops_xp_cls_remove,
    .ofproto_ops_cls_replace = &ops_xp_cls_replace,
    .ofproto_ops_cls_list_update = &ops_xp_cls_list_update,
    .ofproto_ops_cls_statistics_get = &ops_xp_cls_stats_get,
    .ofproto_ops_cls_statistics_clear = &ops_xp_cls_stats_clear,
    .ofproto_ops_cls_statistics_clear_all = &ops_xp_cls_stats_clear_all,
    .ofproto_ops_cls_acl_log_pkt_register_cb = &ops_xp_cls_acl_log_pkt_register_cb,
};

static struct plugin_extension_interface cls_xpliant_extension = {
    OPS_CLS_ASIC_PLUGIN_INTERFACE_NAME,
    OPS_CLS_ASIC_PLUGIN_INTERFACE_MAJOR,
    OPS_CLS_ASIC_PLUGIN_INTERFACE_MINOR,
    (void *)&cls_xpliant_interface
};

void
init(void)
{
    register_plugin_extension(&xpliant_extension);
    VLOG_INFO("The %s asic plugin interface was registered",
              ASIC_PLUGIN_INTERFACE_NAME);

    register_plugin_extension(&copp_xpliant_extension);
    VLOG_INFO("The %s asic plugin interface was registered",
              COPP_ASIC_PLUGIN_INTERFACE_NAME);

    register_plugin_extension(&qos_xpliant_extension);
    VLOG_INFO("The %s asic plugin interface was registered",
              QOS_ASIC_PLUGIN_INTERFACE_NAME);

    register_plugin_extension(&cls_xpliant_extension);
    VLOG_INFO("The %s asic plugin interface was registered",
              OPS_CLS_ASIC_PLUGIN_INTERFACE_NAME);
}

void
run(void)
{
}

void
wait(void)
{
}

void
destroy(void)
{
}

void
netdev_register(void)
{
    ops_xp_netdev_register();
}

void
ofproto_register(void)
{
    ofproto_class_register(&ofproto_xpliant_class);
}
