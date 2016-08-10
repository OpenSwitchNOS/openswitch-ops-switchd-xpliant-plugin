#  Copyright (C) 2016, Cavium, Inc.
#  All Rights Reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.

project (ovs_xpliant_plugin)
cmake_minimum_required (VERSION 2.8)
set (SRC_DIR src)
set (INCL_DIR include)

###
### Define sources
###
set (SOURCES ${SRC_DIR}/ops-xp-dev-init.c
             ${SRC_DIR}/ops-xp-dev.c
             ${SRC_DIR}/ops-xp-plugins.c
             ${SRC_DIR}/ops-xp-ofproto-provider.c
             ${SRC_DIR}/ops-xp-ofproto-datapath.c
             ${SRC_DIR}/ops-xp-netdev.c
             ${SRC_DIR}/ops-xp-util.c
             ${SRC_DIR}/ops-xp-vlan.c
             ${SRC_DIR}/ops-xp-vlan-bitmap.c
             ${SRC_DIR}/ops-xp-lag.c
             ${SRC_DIR}/ops-xp-mac-learning.c
             ${SRC_DIR}/ops-xp-host.c
             ${SRC_DIR}/ops-xp-routing.c
             ${SRC_DIR}/ops-xp-port.c
             ${SRC_DIR}/ops-xp-host-netdev.c
             ${SRC_DIR}/ops-xp-host-tap.c
             ${SRC_DIR}/ops-xp-stg.c
             ${SRC_DIR}/ops-xp-copp.c
             ${SRC_DIR}/ops-xp-qos.c
             ${SRC_DIR}/ops-xp-classifier.c
    )


###
### Define and locate needed libraries and includes
###
include(FindPkgConfig)
pkg_check_modules(OVSCOMMON REQUIRED libovscommon)
pkg_check_modules(OPENXPS REQUIRED openXps)

###
### Define compile flags
###
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OPENXPS_CFLAGS} -std=gnu99 -Werror")

include_directories(${CMAKE_SOURCE_DIR}/${INCL_DIR}
                    ${OVSCOMMON_INCLUDE_DIRS}
                    ${OPENXPS_INCLUDE_DIRS})

###
### Create our library
###

add_library (ovs_xpliant_plugin SHARED ${SOURCES})

target_link_libraries (ovs_xpliant_plugin ${OVSCOMMON_LIBRARIES} ${OPENXPS_LIBRARIES})

###
### Installation
###
install(TARGETS ovs_xpliant_plugin
        LIBRARY DESTINATION lib/openvswitch/plugins
    )
