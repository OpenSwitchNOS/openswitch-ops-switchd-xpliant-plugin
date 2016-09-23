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
import re
from opstestfw.switch.CLI import *
from opstestfw.host import *

topoDict = {"topoTarget": "dut01",
            "topoDevices": "dut01 wrkston01 wrkston02 wrkston03",
            "topoLinks": "lnk01:dut01:wrkston01,\
                          lnk02:dut01:wrkston02,\
                          lnk03:dut01:wrkston03",            
            "topoFilters": "dut01:system-category:switch,\
                            wrkston01:system-category:workstation,\
                            wrkston02:system-category:workstation,\
                            wrkston03:system-category:workstation"}

topoDict2 = {"topoTarget": "dut01",
            "topoDevices": "dut01 wrkston01 wrkston02",
            "topoLinks": "lnk01:dut01:wrkston01,\
                          lnk02:dut01:wrkston02",            
            "topoFilters": "dut01:system-category:switch,\
                            wrkston01:system-category:workstation,\
                            wrkston02:system-category:workstation"}

          
intf1 = 1
intf2 = 2
intf3 = 3
intf4 = 4

SLEEP_TIME = 5

def sw_port_access(switch, port, vlan_id):

    switch.ConfigVtyShell(enter=True)
    switch.DeviceInteract(command="vlan " + vlan_id)
    switch.DeviceInteract(command="no shutdown")
    switch.DeviceInteract(command="exit")

    switch.DeviceInteract(command="interface " + port)
    switch.DeviceInteract(command="no routing")
    switch.DeviceInteract(command="vlan access " + vlan_id)
    switch.DeviceInteract(command="no shutdown")
    switch.DeviceInteract(command="exit")
    switch.ConfigVtyShell(enter=False)

def sw_ports_trunk_125(switch, port):

    switch.ConfigVtyShell(enter=True)
    switch.DeviceInteract(command="vlan 125")
    switch.DeviceInteract(command="no shutdown")
    switch.DeviceInteract(command="exit")

    switch.DeviceInteract(command="interface " + port)
    switch.DeviceInteract(command="no routing")    
    switch.DeviceInteract(command="vlan trunk allowed 125")
    switch.DeviceInteract(command="no shutdown")
    switch.DeviceInteract(command="exit")
    switch.ConfigVtyShell(enter=False)

def set_ws_intf_down(wrkston, interface):

    command = "ip link set dev " + interface + " down"
    returnStructure = wrkston.DeviceInteract(command=command)
    bufferout = returnStructure.get('buffer')
    retCode = returnStructure.get('returnCode')
    assert retCode == 0, "Unable to set interface up: " + command

def set_ws_intf_up(wrkston, interface):

    command = "ip link set dev " + interface + " up"
    returnStructure = wrkston.DeviceInteract(command=command)
    bufferout = returnStructure.get('buffer')
    retCode = returnStructure.get('returnCode')
    assert retCode == 0, "Unable to set interface up: " + command

def add_ws_intf_link(wrkston, interface, vlan_id):

    link_name="eth0." + vlan_id
    command = "ip link add link " + interface + " name " + link_name + " type vlan id " + vlan_id
    returnStructure = wrkston.DeviceInteract(command=command)
    bufferout = returnStructure.get('buffer')
    retCode = returnStructure.get('returnCode')
    assert retCode == 0, "Failed to execute command: " + command

def conf_ws_intf(wrkston, interface, ipAddr, mask, brd_ip):

    command = "ip addr add " + ipAddr + "/" + mask + " brd " + brd_ip + " dev " + interface
    returnStructure = wrkston.DeviceInteract(command=command)
    bufferout = returnStructure.get('buffer')
    retCode = returnStructure.get('returnCode')
    assert retCode == 0, "Unable to configure interface: " + command

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



@pytest.mark.timeout(500)
class Test_vlan_functionality:
    
    def setup_class(cls):
        pass
        
    def teardown_class(cls):
        Test_vlan_functionality.topoObj.terminate_nodes()

    def test_vlan_functionality_access1(self):
        print "\n\n########################### test_vlan_functionality_access1 ###########################\n"
        
        Test_vlan_functionality.testObj = testEnviron(topoDict=topoDict)
        Test_vlan_functionality.topoObj = \
            Test_vlan_functionality.testObj.topoObjGet()

        dut01Obj = self.topoObj.deviceObjGet(device="dut01")
        switches = [dut01Obj]
        wrkston01Obj = self.topoObj.deviceObjGet(device="wrkston01")
        wrkston02Obj = self.topoObj.deviceObjGet(device="wrkston02")
        wrkston03Obj = self.topoObj.deviceObjGet(device="wrkston03")

        print "Applying configurations...\n"

        sw_port_access(dut01Obj, "1", "125")
        sw_port_access(dut01Obj, "2", "225")
        sw_port_access(dut01Obj, "3", "125")
        
        conf_ws_intf(wrkston01Obj, wrkston01Obj.linkPortMapping['lnk01'], "1.1.1.1", "24", "1.1.1.255")
        conf_ws_intf(wrkston02Obj, wrkston02Obj.linkPortMapping['lnk02'], "1.1.1.2", "24", "1.1.1.255")
        conf_ws_intf(wrkston03Obj, wrkston03Obj.linkPortMapping['lnk03'], "1.1.1.3", "24", "1.1.1.255")
        
        sleep(SLEEP_TIME)
        ping("negative", wrkston01Obj, "1.1.1.2")
              
        ping("positive", wrkston01Obj, "1.1.1.3")
        
        Test_vlan_functionality.topoObj.terminate_nodes()

    def test_vlan_functionality_access2(self):
        print "\n\n########################### test_vlan_functionality_access2 ###########################\n"

        Test_vlan_functionality.testObj = testEnviron(topoDict=topoDict2)
        Test_vlan_functionality.topoObj = \
            Test_vlan_functionality.testObj.topoObjGet()

        dut01Obj = self.topoObj.deviceObjGet(device="dut01")
        switches = [dut01Obj]
        wrkston01Obj = self.topoObj.deviceObjGet(device="wrkston01")
        wrkston02Obj = self.topoObj.deviceObjGet(device="wrkston02")

        print "Applying configurations...\n"

        sw_port_access(dut01Obj, "1", "125")
        sw_port_access(dut01Obj, "2", "125")

        command = "ifconfig " + wrkston01Obj.linkPortMapping['lnk01'] + " 0.0.0.0"  
        returnStructure = wrkston01Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add 0.0.0.0 ipAddr on WS1"

        add_ws_intf_link(wrkston01Obj, wrkston01Obj.linkPortMapping['lnk01'], "125")
        conf_ws_intf(wrkston01Obj, "eth0.125", "1.1.1.1", "24", "1.1.1.255")
        set_ws_intf_up(wrkston01Obj, "eth0.125")

        conf_ws_intf(wrkston02Obj, wrkston02Obj.linkPortMapping['lnk02'], "1.1.1.2", "24", "1.1.1.255")

        sleep(SLEEP_TIME)
        ping("negative", wrkston01Obj, "1.1.1.2")

        set_ws_intf_down(wrkston01Obj, "eth0.125")
        conf_ws_intf(wrkston01Obj, wrkston01Obj.linkPortMapping['lnk01'], "1.1.1.1", "24", "1.1.1.255")

        ping("positive", wrkston01Obj, "1.1.1.2")

        Test_vlan_functionality.topoObj.terminate_nodes()

    def test_vlan_functionality_access3(self):
        print "\n\n########################### test_vlan_functionality_access3 ###########################\n"

        Test_vlan_functionality.testObj = testEnviron(topoDict=topoDict2)
        Test_vlan_functionality.topoObj = \
            Test_vlan_functionality.testObj.topoObjGet()

        dut01Obj = self.topoObj.deviceObjGet(device="dut01")
        switches = [dut01Obj]
        wrkston01Obj = self.topoObj.deviceObjGet(device="wrkston01")
        wrkston02Obj = self.topoObj.deviceObjGet(device="wrkston02")

        print "Applying configurations...\n"

        sw_ports_trunk_125(dut01Obj, "1")
        sw_port_access(dut01Obj, "2", "125")

        command = "ifconfig " + wrkston01Obj.linkPortMapping['lnk01'] + " 0.0.0.0"
        returnStructure = wrkston01Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add 0.0.0.0 ipAddr on WS1"

        add_ws_intf_link(wrkston01Obj, wrkston01Obj.linkPortMapping['lnk01'], "125")
        conf_ws_intf(wrkston01Obj, "eth0.125", "1.1.1.1", "24", "1.1.1.255")
        set_ws_intf_up(wrkston01Obj, "eth0.125")

        conf_ws_intf(wrkston02Obj, wrkston02Obj.linkPortMapping['lnk02'], "1.1.1.2", "24", "1.1.1.255")

        sleep(SLEEP_TIME)
        ping("positive", wrkston01Obj, "1.1.1.2")
