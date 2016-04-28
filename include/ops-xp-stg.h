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
 * File: ops-xp-stg.h
 *
 * Purpose: This file provides public definitions for OpenSwitch spanning tree
 *          groups related application code for the Cavium/XPliant SDK.
 */

#ifndef OPS_XP_STG_H
#define OPS_XP_STG_H 1

int ops_xp_create_stg(int *p_stg);

int ops_xp_delete_stg(int stg);

int ops_xp_add_stg_vlan(int stg, int vid);

int ops_xp_remove_stg_vlan(int stg, int vid);

int ops_xp_set_stg_port_state(char *port_name, int stg, int stp_state,
                              bool port_stp_set);

int ops_xp_get_stg_port_state(char *port_name, int stg, int *p_stp_state);

int ops_xp_get_stg_default(int *p_stg);

#endif /* ops-xp-stg.h */
