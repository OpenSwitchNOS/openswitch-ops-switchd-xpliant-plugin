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
 * |Priority  | CPU Queue |  Description                    |
 * +--------------------------------------------------------+
 * |          |           |                                 |
 * |Critical  |   Q10     |  xSTP                           |
 * |          |           |                                 |
 * +--------------------------------------------------------+
 * |          |           |                                 |
 * |Important |   Q9      |  OSPF,BGP                       |
 * |          |           |                                 |
 * +--------------------------------------------------------+
 * |          |           |                                 |
 * |BPDU/LLDP |   Q8      |  LLDP, LACP                     |
 * |/LACP     |           |                                 |
 * |          |           |                                 |
 * +--------------------------------------------------------+
 * |          |           |For now no in-band management    |
 * |MANAGEMENT|   Q7      |traffic supported.               |
 * |          |           |                                 |
 * +--------------------------------------------------------+
 * |          |           |                                 |
 * |UNKNOWN_IP|   Q6      |Unknown destination IP address   |
 * |          |           |                                 |
 * +--------------------------------------------------------+
 * |          |           |Unicast ARP, Unicast ICMP,       |
 * |SW-PATH   |   Q5      |ICMPv6, IP options               |
 * |          |           |                                 |
 * +--------------------------------------------------------+
 * |          |           |                                 |
 * |NORMAL    |   Q4      |Broadcast ARP,Broadcast/Multicast|
 * |          |           |ICMP, DHCP                       |
 * +--------------------------------------------------------+
 * |          |           |                                 |
 * |sFlow     |   Q3      |  Sampled sFlow traffic          |
 * +--------------------------------------------------------+
 * |Snooping  |   Q2      |                                 |
 * |          |           |                                 |
 * +--------------------------------------------------------+
 * |Default   |   Q1      | Unclasssified Packets           |
 * |          |           |                                 |
 * +--------------------------------------------------------+
 * |Exceptions|           |                                 |
 * |/ ACL     |   Q0      | ACL Logging                     |
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
 * |               |                                |             |   (PPS)    |  (Packets) |
 * +---------------------------------------------------------------------------+------------+
 * | ACL_LOGGING   |  ACL Logging                   |  Q0         |  5         |  5         |
 * +---------------------------------------------------------------------------+------------+
 * | ARP_BC        |  Broadcast ARP Packets         |  Q4         |  1000      |  1000      |
 * +---------------------------------------------------------------------------+------------+
 * | ARP_UC        |  Unicast ARPs                  |  Q5         |  1000      |  1000      |
 * +---------------------------------------------------------------------------+------------+
 * | BGP           |  BGP packets                   |  Q9         |  5000      |  5000      |
 * +---------------------------------------------------------------------------+------------+
 * | DHCP          |  DHCP packets                  |  Q4         |  500       |  500       |
 * +---------------------------------------------------------------------------+------------+
 * | DHCPV6        |  IPv6 DHCP packets             |  Q4         |  500       |  500       |
 * +---------------------------------------------------------------------------+------------+
 * | ICMP_BC       |  IPv4 broadcast/multicast ICMP |  Q4         |  1000      |  1000      |
 * |               |  packets                       |             |            |            |
 * +---------------------------------------------------------------------------+------------+
 * | ICMP_UC       |  IPv4 unicast ICMP packets     |  Q5         |  1000      |  1000      |
 * +---------------------------------------------------------------------------+------------+
 * | ICMPV6_MC     |  IPv6 multicast ICMP packets   |  Q4         |  1000      |  1000      |
 * +---------------------------------------------------------------------------+------------+
 * | ICMPV6_UC     |  IPv6 unicast ICMP             |  Q5         |  1000      |  1000      |
 * +---------------------------------------------------------------------------+------------+
 * | LACP          |  LACP packets                  |  Q8         |  1000      |  1000      |
 * +---------------------------------------------------------------------------+------------+
 * | LLDP          |  LLDP packets                  |  Q8         |  500       |  500       |
 * +---------------------------------------------------------------------------+------------+
 * | OSPF_MC       |  Multicast OSPF packets        |  Q9         |  5000      |  5000      |
 * +---------------------------------------------------------------------------+------------+
 * | OSPF_UC       |  Unicast OSPF packets          |  Q9         |  5000      |  5000      |
 * +---------------------------------------------------------------------------+------------+
 * | sFlow         |  Sampled sFlow packets         |  Q3         |  20000     |  20000     |
 * +---------------------------------------------------------------------------+------------+
 * | STP           |  STP packets                   |  Q9         |  1000      |  1000      |
 * +---------------------------------------------------------------------------+------------+
 * | IPOPTION      |  Packets with IPv4 options     |  Q5         |  250       |  250       |
 * +---------------------------------------------------------------------------+------------+
 * | IPOPTIONV6    |  Packets with IPv6 options     |  Q5         |  250       |  250       |
 * +---------------------------------------------------------------------------+------------+
 * | UNKOWN_IP_DEST|  Packets with unknown IPv4/IPv6|  Q6         |  2500      |  2500      |
 * |               |  destination IPs               |             |            |            |
 * +---------------------------------------------------------------------------+------------+
 * | UNCLASSIFIED  |  Any Unclassified packets      |  Q1         |  5000      |  5000      |
 * +---------------+--------------------------------+-------------+------------+------------+
 *
 * Table containing QoS queue categories, packet rates and packet burst for different control
 *                         packets for AS7512.
 */

/*
 * Macro for populating the structure ops_copp_fp_rule_t with the default
 * values specified in the file ops-xp-copp-defaults.h.
 *
 * The following values need to be entered into the strcuture entry:
 * 1. The packet name is set to the name of the control packet.
 * 2. Ingress CPU queue number is set to platform packet class queue default
 *    value.
 * 3. Egress rate is set to platform packet default value.
 * 4. Egress burst is set to platform packet default value.
 * 5. Global control packet rule programming function pointer is pointed to
 *    the control packet class programming function.
 * 6. Egress FP entry programming function pointer is pointed to the
 *    egress implementation function.
 * 7. Ingress FP entry  programming function pointer array is pointed to the
 *    ingress implementation function pointers.
 */
#define OPS_XP_COPP_CLASS(protocol_class,packet_name,\
                          queue,rate,burst,init_func)

//OPS_XP_COPP_CLASS(ACL_LOGGING, "ACL Logging packets", 0, 5, 5)
//OPS_XP_COPP_CLASS(ARP_BROADCAST, "Broadcast ARPs packets", 0, 1000, 1000)
//OPS_XP_COPP_CLASS(BGP, "BGP packets", 0, 5000, 5000)
//OPS_XP_COPP_CLASS(DEFAULT_UNKNOWN, "Unclassified packets", 0, 5000, 5000)
//OPS_XP_COPP_CLASS(DHCPv4, "DHCPv4 packets", 0, 500, 500)
//OPS_XP_COPP_CLASS(DHCPv6, "DHCPv6 packets", 0, 500, 500)
//OPS_XP_COPP_CLASS(ICMPv4_MULTIDEST, "ICMPV4 multicast packets", 0, 500, 500)
//OPS_XP_COPP_CLASS(ICMPv4_UNICAST, "ICMPV4 unicast packets", 0, 500, 500)
//OPS_XP_COPP_CLASS(ICMPv6_MULTICAST, "ICMPV6 multicast packets", 0, 1000, 1000)
//OPS_XP_COPP_CLASS(ICMPv6_UNICAST, "ICMPV6 unicast packets", 0, 500, 500)
OPS_XP_COPP_CLASS(COPP_LACP, "LACP packets", OPS_XP_COPP_QUEUE_BPDU, 500, 500, ops_xp_copp_lacp_init)
OPS_XP_COPP_CLASS(COPP_LLDP, "LLDP packets", OPS_XP_COPP_QUEUE_BPDU, 500, 500, ops_xp_copp_lldp_init)
//OPS_XP_COPP_CLASS(OSPFv2_MULTICAST, "OSPFv2 multicast packets", 0, 500, 500)
//OPS_XP_COPP_CLASS(OSPFv2_UNICAST, "OSPFv2 unicast packets", 0, 500, 500)
//OPS_XP_COPP_CLASS(sFLOW_SAMPLES, "Sflow packets", 0, 500, 500)
OPS_XP_COPP_CLASS(COPP_STP_BPDU, "STP packets", OPS_XP_COPP_QUEUE_CRITICAL, 500, 500, ops_xp_copp_stp_init)
//OPS_XP_COPP_CLASS(UNKNOWN_IP_UNICAST, "Unknown IP unicast packets", 0, 500, 500)
