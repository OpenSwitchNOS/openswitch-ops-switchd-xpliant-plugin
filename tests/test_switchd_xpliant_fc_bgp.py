#! /usr/bin/env python
#
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

import pytest
from opstestfw import *
from opstestfw.switch.CLI import *
from opstestfw.host import *


topoDict = {"topoTarget": "dut01 dut02",
            "topoDevices": "dut01 dut02",
            "topoLinks": "lnk01:dut01:dut02",
            "topoFilters": "dut01:system-category:switch,\
                            dut02:system-category:switch"}


intf1 = 1
intf2 = 2
intf3 = 3
intf4 = 4

EST_TIME = 150

def sw_bgp_conf(switch1, switch2):
    switch1.ConfigVtyShell(enter=True)
    switch1.DeviceInteract(command="hostname OPS-1")
    switch1.DeviceInteract(command="router bgp 1")
    switch1.DeviceInteract(command="bgp router-id 1.1.1.1")
    switch1.DeviceInteract(command="redistribute connected")
    switch1.DeviceInteract(command="neighbor 1.1.1.2 remote-as 2")
    switch1.DeviceInteract(command="interface 1")
    switch1.DeviceInteract(command="routing")
    switch1.DeviceInteract(command="ip address 1.1.1.1/24")
    switch1.DeviceInteract(command="no shutdown")
    switch1.DeviceInteract(command="exit")    
    switch1.ConfigVtyShell(enter=False)

    switch2.ConfigVtyShell(enter=True)
    switch2.DeviceInteract(command="hostname OPS-2")
    switch2.DeviceInteract(command="router bgp 2")
    switch2.DeviceInteract(command="bgp router-id 1.1.1.2")
    switch2.DeviceInteract(command="redistribute connected")
    switch2.DeviceInteract(command="neighbor 1.1.1.1 remote-as 1")
    switch2.DeviceInteract(command="interface 1")
    switch2.DeviceInteract(command="routing")
    switch2.DeviceInteract(command="ip address 1.1.1.2/24")
    switch2.DeviceInteract(command="no shutdown")
    switch2.DeviceInteract(command="exit")    
    switch2.ConfigVtyShell(enter=False)

@pytest.mark.timeout(500)
class Test_switchd_xpliant_fc_bgp:

    def setup_class(cls):
        pass

    def teardown_class(cls):
        Test_switchd_xpliant_fc_bgp.topoObj.terminate_nodes()

    def test_switchd_xpliant_fc_bgp(self):
        print "\n\n########################### test_switchd_xpliant_bgp ###########################\n"

        Test_switchd_xpliant_fc_bgp.testObj = testEnviron(topoDict=topoDict)
        Test_switchd_xpliant_fc_bgp.topoObj = \
            Test_switchd_xpliant_fc_bgp.testObj.topoObjGet()

        dut01Obj = self.topoObj.deviceObjGet(device="dut01")
        dut02Obj = self.topoObj.deviceObjGet(device="dut02")
        switches = [dut01Obj,dut02Obj]
        
        print "\nApplying configurations...\n"
        sw_bgp_conf(dut01Obj, dut02Obj)


        info("============================================================\n")
        info("Waiting upto %s sec for BGP session establishing...\n" % EST_TIME)
        info("============================================================\n")

        start = time.time()
        while (1):
            retStruct=dut01Obj.DeviceInteract(command="show ip bgp summary")
            status=retStruct['buffer'].find("Established")
            if status < 0:
                info("\rIn progres: %f" % (time.time() - start))
            elif status >= 0:
                info("\n============================================================")
                info("\n%s" % retStruct['buffer'])
                info("\n============================================================\n")
                break
            assert (time.time() - start) < EST_TIME, "Time's up!"