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
 * File: ops-xp-copp-defaults.h
 *
 * Purpose: This file provides public definitions for OpenSwitch CoPP
 *          default configuration values for the Cavium/XPliant SDK.
 */

/*
 * +----------+-----------+---------------------------------+
 * |          |           |                                 |
 * |Priority  | CPU Queue | Description                     |
 * +--------------------------------------------------------+
 * |          |           |                                 |
 * |Critical  |   Q8      | xSTP                            |
 * |          |           |                                 |
 * +--------------------------------------------------------+
 * |          |           | For now no OSPF,BGP traffic     |
 * |Important |   Q7      | classification is supported     |
 * |          |           |                                 |
 * +--------------------------------------------------------+
 * |          |           |                                 |
 * |BPDU/LLDP |   Q6      | LLDP, LACP                      |
 * |/LACP     |           |                                 |
 * +--------------------------------------------------------+
 * |          |           |                                 |
 * |MY_IP     |   Q5      | Broadcast ARP, Unicast ARP,     |
 * |          |           | Unicast IP address              |
 * |          |           |                                 |
 * +--------------------------------------------------------+
 * |          |           |                                 |
 * |FDB       |   Q4      | Unknown source MAC address      |
 * |          |           |                                 |
 * +--------------------------------------------------------+
 * |          |           |                                 |
 * |UNKNOWN_IP|   Q3      | Unknown destination IP address  |
 * |          |           |                                 |
 * +--------------------------------------------------------+
 * |          |           |                                 |
 * |sFlow     |   Q2      | Sampled sFlow traffic           |
 * |          |           |                                 |
 * +--------------------------------------------------------+
 * |          |           |                                 |
 * |Default   |   Q1      | Unclassified Packets            |
 * |          |           |                                 |
 * +--------------------------------------------------------+
 * |Exceptions|           | For now no ACL logging is       |
 * |/ ACL     |   Q0      | supported                       |
 * |  Logging |           |                                 |
 * +----------+-----------+---------------------------------+
 *
 * Table containing the QoS categories for different control
 *        packet types for AS7512.
 */

/*
 * +---------------+--------------------------------+-------------+------------+------------+
 * |               |                                |             |            |            |
 * |Packet Class   |  Description                   | Queue       | Rate Limit | Burst Size |
 * |               |                                |             |   (Kbps)   |  (Kb)      |
 * +---------------------------------------------------------------------------+------------+
 * | ARP_BC        |  Broadcast ARP packets         |  Q5         |  16384     |  16384     |
 * +---------------------------------------------------------------------------+------------+
 * | ARP_UC        |  Unicast ARP packets           |  Q5         |  16384     |  16384     |
 * +---------------------------------------------------------------------------+------------+
 * | UNKNOWN_IP    |  Unknown unicast IP packets    |  Q3         |  8192      |  8192      |
 * +---------------------------------------------------------------------------+------------+
 * | MY_IP         |  IPv4/IPv6 packets for switch  |  Q5         |  16384     |  16384     |
 * +---------------------------------------------------------------------------+------------+
 * | LACP          |  LACP packets                  |  Q6         |  16384     |  16384     |
 * +---------------------------------------------------------------------------+------------+
 * | LLDP          |  LLDP packets                  |  Q6         |  8192      |  8192      |
 * +---------------------------------------------------------------------------+------------+
 * | STP           |  STP packets                   |  Q8         |  16384     |  16384     |
 * +---------------------------------------------------------------------------+------------+
 * | FDB           |  FDB MAC learning packets      |  Q4         |  98304     |  98304     |
 * +---------------+--------------------------------+-------------+------------+------------+
 * | UNCLASSIFIED  |  Any Unclassified packets      |  Q1         |  8192      |  8192      |
 * +---------------+--------------------------------+-------------+------------+------------+
 *
 * Table containing QoS queue categories, packet rates and packet burst for different control
 *                         packets for AS7512.
 */

/*
 * Macro for populating the structure ops_xp_copp_class_config_t with the
 * default values specified in the file ops-xp-copp-defaults.h.
 *
 * COPP_DEFAULT_UNKNOWN init must be called in prior to all others
 */
OPS_XP_COPP_CLASS(COPP_DEFAULT_UNKNOWN, "Unclassified packets", OPS_XP_COPP_QUEUE_DEFAULT, 8192, 8192, ops_xp_copp_default_init)
OPS_XP_COPP_CLASS(COPP_ARP_BROADCAST, "Broadcast ARPs packets", OPS_XP_COPP_QUEUE_MY_IP, 16384, 16384, ops_xp_copp_bcarp_init)
OPS_XP_COPP_CLASS(COPP_ARP_UNICAST, "Unicast ARPs packets", OPS_XP_COPP_QUEUE_MY_IP, 16384, 16384, ops_xp_copp_ucarp_init)
OPS_XP_COPP_CLASS(COPP_MY_IP, "MY IP unicast packets", OPS_XP_COPP_QUEUE_MY_IP, 16384, 16384, ops_xp_copp_myip_init)
OPS_XP_COPP_CLASS(COPP_LACP, "LACP packets", OPS_XP_COPP_QUEUE_BPDU, 16384, 16384, ops_xp_copp_lacp_init)
OPS_XP_COPP_CLASS(COPP_LLDP, "LLDP packets", OPS_XP_COPP_QUEUE_BPDU, 8192, 8192, ops_xp_copp_lldp_init)
OPS_XP_COPP_CLASS(COPP_STP_BPDU, "STP packets", OPS_XP_COPP_QUEUE_CRITICAL, 16384, 16384, ops_xp_copp_stp_init)
OPS_XP_COPP_CLASS(COPP_UNKNOWN_IP_UNICAST, "Unknown IP unicast packets", OPS_XP_COPP_QUEUE_UNKNOWN_IP, 8192, 8192, ops_xp_copp_unknownip_init)
OPS_XP_COPP_CLASS(COPP_FDB, "FDB learning packets", OPS_XP_COPP_QUEUE_FDB, 98304, 98304, ops_xp_copp_fdb_init)
