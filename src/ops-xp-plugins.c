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

#define init libovs_xpliant_plugin_LTX_init
#define run libovs_xpliant_plugin_LTX_run
#define wait libovs_xpliant_plugin_LTX_wait
#define destroy libovs_xpliant_plugin_LTX_destroy
#define netdev_register libovs_xpliant_plugin_LTX_netdev_register
#define ofproto_register libovs_xpliant_plugin_LTX_ofproto_register

VLOG_DEFINE_THIS_MODULE(xpliant_plugin);

void
init(void)
{
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
