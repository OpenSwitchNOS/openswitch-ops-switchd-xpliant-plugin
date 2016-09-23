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

topoDict = {"topoTarget": "dut01",
            "topoDevices": "dut01 wrkston01 wrkston02",
            "topoLinks": "lnk01:dut01:wrkston01,\
                          lnk02:dut01:wrkston02",
            "topoFilters": "dut01:system-category:switch,\
                            wrkston01:system-category:workstation,\
                            wrkston02:system-category:workstation"}

topoDict2 = {"topoTarget": "dut01 dut02",
            "topoDevices": "dut01 dut02 wrkston01 wrkston02",
            "topoLinks": "lnk01:dut01:wrkston01,\
                          lnk02:dut01:dut02,\
                          lnk03:dut02:wrkston02",
            "topoFilters": "dut01:system-category:switch,\
                            dut02:system-category:switch,\
                            wrkston01:system-category:workstation,\
                            wrkston02:system-category:workstation"}


intf1 = 1
intf2 = 2
intf3 = 3
intf4 = 4

SLEEP_TIME = 5

def ping(result, wrkston, dst_ip):

    retStruct = wrkston.Ping(ipAddr=dst_ip, packetCount=5)
    if result == "positive":
        if retStruct.returnCode() == 1:
            LogOutput('error', "Failed to ping")
            assert retStruct.returnCode() == 0, "Failed to ping. TC failed"
        else:
            LogOutput('info', "IPv4 Ping Succeded")
            if retStruct.valueGet(key='packet_loss') != 0:
                LogOutput('info', "But %s percent of packets are lost" % retStruct.valueGet(key='packet_loss'))
    elif result == "negative":
        assert retStruct.returnCode() != 0, "Ping Succeded. TC failed"

def sw_conf(switch):
    switch.ConfigVtyShell(enter=True)
    switch.DeviceInteract(command="interface 1")
    switch.DeviceInteract(command="routing")
    switch.DeviceInteract(command="ip address 10.1.1.1/24")
    switch.DeviceInteract(command="no shutdown")
    switch.DeviceInteract(command="exit")

    switch.DeviceInteract(command="interface 2")
    switch.DeviceInteract(command="routing")
    switch.DeviceInteract(command="ip address 20.1.1.1/24")
    switch.DeviceInteract(command="no shutdown")
    switch.DeviceInteract(command="exit")
    switch.ConfigVtyShell(enter=False)

def sw_rout_conf(switch1, switch2):
    switch1.ConfigVtyShell(enter=True)
    switch1.DeviceInteract(command="interface 1")
    switch1.DeviceInteract(command="routing")
    switch1.DeviceInteract(command="ip address 1.1.1.1/24")
    switch1.DeviceInteract(command="no shutdown")
    switch1.DeviceInteract(command="exit")    

    switch1.DeviceInteract(command="interface 2")
    switch1.DeviceInteract(command="routing")
    switch1.DeviceInteract(command="ip address 2.2.2.2/24")
    switch1.DeviceInteract(command="no shutdown")
    switch1.DeviceInteract(command="exit")

    switch1.DeviceInteract(command="ip route 3.3.3.0/24 2.2.2.1")
    switch1.DeviceInteract(command="exit")
    switch1.ConfigVtyShell(enter=False)

    switch2.ConfigVtyShell(enter=True)
    switch2.DeviceInteract(command="interface 1")
    switch2.DeviceInteract(command="routing")
    switch2.DeviceInteract(command="ip address 2.2.2.1/24")
    switch2.DeviceInteract(command="no shutdown")
    switch2.DeviceInteract(command="exit")    

    switch2.DeviceInteract(command="interface 2")
    switch2.DeviceInteract(command="routing")
    switch2.DeviceInteract(command="ip address 3.3.3.1/24")
    switch2.DeviceInteract(command="no shutdown")
    switch2.DeviceInteract(command="exit")

    switch2.DeviceInteract(command="ip route 1.1.1.0/24 2.2.2.2")
    switch2.DeviceInteract(command="exit")
    switch2.ConfigVtyShell(enter=False)

@pytest.mark.timeout(500)
class Test_switchd_xpliant_fc_l3:

    def setup_class(cls):
        pass

    def teardown_class(cls):
        Test_switchd_xpliant_fc_l3.topoObj.terminate_nodes()

    def test_switchd_xpliant_fc_l3_1(self):
        print "\n\n########################### test_switchd_xpliant_fc_l3_1 ###########################\n"

        Test_switchd_xpliant_fc_l3.testObj = testEnviron(topoDict=topoDict)
        Test_switchd_xpliant_fc_l3.topoObj = \
            Test_switchd_xpliant_fc_l3.testObj.topoObjGet()

        dut01Obj = self.topoObj.deviceObjGet(device="dut01")
        switches = [dut01Obj]

        wrkston01Obj = self.topoObj.deviceObjGet(device="wrkston01")
        wrkston02Obj = self.topoObj.deviceObjGet(device="wrkston02")
        print "Applying configurations...\n"
        sw_conf(dut01Obj)
        sleep(SLEEP_TIME)

        command = "ifconfig " + wrkston01Obj.linkPortMapping['lnk01'] + " 10.1.1.2/24 up"
        returnStructure = wrkston01Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add ipAddr on WS1"

        command = "ip route add 20.1.1.0/24 via 10.1.1.1 dev " + wrkston01Obj.linkPortMapping['lnk01']
        returnStructure = wrkston01Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add route on WS1"


        command = "ifconfig " + wrkston02Obj.linkPortMapping['lnk02'] + " 20.1.1.2/24 up"
        returnStructure = wrkston02Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add ipAddr on WS2"

        command = "ip route add 10.1.1.0/24 via 20.1.1.1 dev " + wrkston02Obj.linkPortMapping['lnk02']
        returnStructure = wrkston02Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add route on WS2"

        sleep(SLEEP_TIME)
        print "Executing ping commands...\n"

        ping("positive", wrkston01Obj, "20.1.1.2")
        ping("positive", wrkston01Obj, "10.1.1.1")
        ping("positive", wrkston02Obj, "20.1.1.1")

        Test_switchd_xpliant_fc_l3.topoObj.terminate_nodes()

    def test_switchd_xpliant_fc_l3_2(self):
        print "\n\n########################### test_switchd_xpliant_fc_l3_2 ###########################\n"

        Test_switchd_xpliant_fc_l3.testObj = testEnviron(topoDict=topoDict2)
        Test_switchd_xpliant_fc_l3.topoObj = \
            Test_switchd_xpliant_fc_l3.testObj.topoObjGet()

        dut01Obj = self.topoObj.deviceObjGet(device="dut01")
        dut02Obj = self.topoObj.deviceObjGet(device="dut02")
        switches = [dut01Obj,dut02Obj]
        wrkston01Obj = self.topoObj.deviceObjGet(device="wrkston01")
        wrkston02Obj = self.topoObj.deviceObjGet(device="wrkston02")

        print "Applying configurations...\n"
        sw_rout_conf(dut01Obj, dut02Obj)
        sleep(SLEEP_TIME)

        command = "ifconfig " + wrkston01Obj.linkPortMapping['lnk01'] + " 1.1.1.2/24 up"
        returnStructure = wrkston01Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add ipAddr on WS1"

        command = "ip route add 3.3.3.0/24 via 1.1.1.1 dev " + wrkston01Obj.linkPortMapping['lnk01']
        returnStructure = wrkston01Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add route on WS1"

        command = "ip route add 2.2.2.0/24 via 1.1.1.1 dev " + wrkston01Obj.linkPortMapping['lnk01']
        returnStructure = wrkston01Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add route on WS1"

        command = "ifconfig " + wrkston02Obj.linkPortMapping['lnk03'] + " 3.3.3.2/24 up"
        returnStructure = wrkston02Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add ipAddr on WS2"

        command = "ip route add 2.2.2.0/24 via 3.3.3.1 dev " + wrkston02Obj.linkPortMapping['lnk03']
        returnStructure = wrkston02Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add route on WS2"

        command = "ip route add 1.1.1.0/24 via 3.3.3.1 dev " + wrkston02Obj.linkPortMapping['lnk03']
        returnStructure = wrkston02Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add default gw on WS2"

        sleep(SLEEP_TIME)
        print "Executing ping commands...\n"

        ping("positive", wrkston01Obj, "3.3.3.2")
        ping("positive", wrkston01Obj, "2.2.2.1")

        ping("positive", wrkston02Obj, "1.1.1.2")
        ping("positive", wrkston02Obj, "2.2.2.2")
