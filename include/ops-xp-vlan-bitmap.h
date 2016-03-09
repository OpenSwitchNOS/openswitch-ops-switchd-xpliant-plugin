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
 * File: ops-xp-vlan-bitmap.h
 *
 * Purpose: This file provides public definitions for OpenSwitch VLAN bitmap
 *          related application code for the Cavium/XPliant SDK.
 */


#ifndef OPS_XP_VLAN_BITMAP_H
#define OPS_XP_VLAN_BITMAP_H 1

#include <stdbool.h>
#include <stdint.h>
#include "bitmap.h"
#include "ops-xp-vlan.h"

/* A "VLAN bitmap" is a 4096-bit bitmap that represents a set.  A 1-bit
 * indicates that the respective VLAN is a member of the set, a 0-bit indicates
 * that it is not.  There is one wrinkle: NULL is a valid value that indicates
 * either that all VLANs are or are not members, depending on the xp_vlan_bitmap.
 *
 * This is empirically a useful data structure. */

#define VLAN_BITMAP_FOR_EACH_1(VID, BITMAP) \
        BITMAP_FOR_EACH_1(VID, XP_VLAN_MAX_COUNT, BITMAP)

unsigned long *ops_xp_vlan_bitmap_from_array(const int64_t *vlans,
                                             size_t n_vlans);
int ops_xp_vlan_bitmap_from_array__(const int64_t *vlans, size_t n_vlans,
                                    unsigned long int *b);
bool ops_xp_vlan_bitmap_equal(const unsigned long *a, const unsigned long *b);
unsigned long *ops_xp_vlan_bitmap_xor(const unsigned long *a,
                                      const unsigned long *b);
unsigned long *ops_xp_vlan_bitmap_clone(const unsigned long *vlans);

#endif /* ops-xp-vlan-bitmap.h */
