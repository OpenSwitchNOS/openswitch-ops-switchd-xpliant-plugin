#!/usr/bin/env python
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
from opstestfw.switch.OVS import *
from random import *

# Topology definition


topoDict = {"topoTarget": "dut01",
            "topoDevices": "dut01 wrkston01 wrkston02 wrkston03 wrkston04",
            "topoLinks": "lnk01:dut01:wrkston01,\
                          lnk02:dut01:wrkston02,\
                          lnk03:dut01:wrkston03,\
                          lnk04:dut01:wrkston04",
            "topoFilters": "dut01:system-category:switch,\
                            wrkston01:system-category:workstation,\
                            wrkston02:system-category:workstation,\
                            wrkston03:system-category:workstation,\
                            wrkston04:system-category:workstation"}

intf1 = 1
intf2 = 2
intf3 = 3
intf4 = 4

SLEEP_TIME = 5

def base_switch_configuration(switch):

    returnStructure = switch.DeviceInteract(command="vtysh")
    if returnStructure.get('returnCode') != 0:
        LogOutput('error', "Failed to get into VtyshShell")
        assert(False)

    returnStructure = switch.DeviceInteract(command="configure terminal")
    if returnStructure.get('returnCode') != 0:
        LogOutput('error', "configure terminal")
        assert(False)

    switch.DeviceInteract(command="interface 1")
    switch.DeviceInteract(command="routing")
    switch.DeviceInteract(command="ip address 1.1.1.1/24")
    switch.DeviceInteract(command="no shutdown")
    switch.DeviceInteract(command="exit")

    switch.DeviceInteract(command="interface 2")
    switch.DeviceInteract(command="routing")
    switch.DeviceInteract(command="ip address 2.2.2.1/24")
    switch.DeviceInteract(command="no shutdown")
    switch.DeviceInteract(command="exit")
    switch.DeviceInteract(command="exit")
    switch.DeviceInteract(command="exit")


def add_and_delete_vid(switch):

    returnStructure = switch.DeviceInteract(command="vtysh")
    if returnStructure.get('returnCode') != 0:
        LogOutput('error', "Failed to get into VtyshShell")
        assert(False)

    returnStructure = switch.DeviceInteract(command="configure terminal")
    if returnStructure.get('returnCode') != 0:
        LogOutput('error', "configure terminal")
        assert(False)

    switch.DeviceInteract(command="vlan 10")
    switch.DeviceInteract(command="no shutdown")
    switch.DeviceInteract(command="exit")
    switch.DeviceInteract(command="no vlan 10")
    switch.DeviceInteract(command="exit")
    switch.DeviceInteract(command="exit")


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

def ipRange(start_ip, end_ip):
    start = list(map(int, start_ip.split(".")))
    end = list(map(int, end_ip.split(".")))
    temp = start
    ip_range = []

    ip_range.append(start_ip)
    while temp <= end:
        temp[2] +=1
        temp[3] = randint(1,254)
        for i in (2, 1):
            if temp[i] == 256:
                temp[i] = 0
                temp[i-1] += 1
        ip_range.append(".".join(map(str, temp)))

    return ip_range


@pytest.mark.timeout(10000)
class Test_vlan_subinterface_testing:

    def setup_class(cls):
        Test_vlan_subinterface_testing.testObj = testEnviron(topoDict=topoDict)
        Test_vlan_subinterface_testing.topoObj = \
            Test_vlan_subinterface_testing.testObj.topoObjGet()


        dut01Obj = Test_vlan_subinterface_testing.topoObj.deviceObjGet(device="dut01")
        switches = [dut01Obj]

        print "Applying configurations...\n"

        base_switch_configuration(dut01Obj)
        sleep(SLEEP_TIME)


    def teardown_class(cls):
        Test_vlan_subinterface_testing.topoObj.terminate_nodes()

    def test_subinterface_creation_1(self):
        print "\n\n########################### test_subinterface_creation_1(positive) ###########################\n"
        print "Applying configurations...\n"
        dut01Obj = self.topoObj.deviceObjGet(device="dut01")

        returnStructure = dut01Obj.DeviceInteract(command="vtysh")
        if returnStructure.get('returnCode') != 0:
            LogOutput('error', "Failed to get into VtyshShell")
            assert(False)

        returnStructure = dut01Obj.DeviceInteract(command="configure terminal")
        if returnStructure.get('returnCode') != 0:
            LogOutput('error', "configure terminal")
            assert(False)

        returnStructure = dut01Obj.DeviceInteract(command="interface 1.10")
        if returnStructure.get('returnCode') != 0:
            LogOutput('error', "Failed to add subinterface")
            assert(False)

        returnStructure = dut01Obj.DeviceInteract(command="encapsulation dot1Q 10")
        if returnStructure.get('returnCode') != 0:
            LogOutput('error', "Failed to configure subinterface")
            assert(False)

        dut01Obj.DeviceInteract(command="ip address 5.5.5.1/24")
        if returnStructure.get('returnCode') != 0:
            LogOutput('error', "Failed to configure subinterface")
            assert(False)
        dut01Obj.DeviceInteract(command="no shutdown")
        dut01Obj.DeviceInteract(command="exit")
        dut01Obj.DeviceInteract(command="exit")
        dut01Obj.DeviceInteract(command="exit")

        print "Subinterface was successfully added and configured!"


    #[usecase based tests]

    def test_untagged_ping_to_switch_1(self):
        print "\n\n########################## test_untagged_ping_to_switch_1(positive) ##########################\n"
        print "Applying configurations...\n"
        wrkston01Obj = self.topoObj.deviceObjGet(device="wrkston01")

        command = "ifconfig " + wrkston01Obj.linkPortMapping['lnk01'] + " 1.1.1.2/24 up"
        returnStructure = wrkston01Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add 1.1.1.2 ipAddr on WS1"

        sleep(SLEEP_TIME)
        ping("positive", wrkston01Obj, "1.1.1.1")

    def test_untagged_ping_to_switch_2(self):
        print "\n\n########################## test_untagged_ping_to_switch_2(negative) ##########################\n"
        print "Executing ping command...\n"
        wrkston01Obj = self.topoObj.deviceObjGet(device="wrkston01")

        sleep(SLEEP_TIME)
        ping("negative", wrkston01Obj, "5.5.5.1")

    def test_untagged_ping_to_switch_3(self):
        print "\n\n########################## test_untagged_ping_to_switch_3(positive) ##########################\n"
        print "Applying configurations...\n"
        wrkston02Obj = self.topoObj.deviceObjGet(device="wrkston02")

        command = "ifconfig " + wrkston02Obj.linkPortMapping['lnk02'] + " 2.2.2.2/24 up"
        returnStructure = wrkston02Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add 2.2.2.2 ipAddr on WS2"

        sleep(SLEEP_TIME)
        ping("positive", wrkston02Obj, "2.2.2.1")

    def test_untagged_ping_to_switch_4(self):
        print "\n\n########################## test_untagged_ping_to_switch_4(negative) ##########################\n"
        print "Executing ping command...\n"
        wrkston02Obj = self.topoObj.deviceObjGet(device="wrkston02")

        sleep(SLEEP_TIME)
        ping("negative", wrkston02Obj, "5.5.5.1")

    def test_untagged_ping_through_switch(self):
        print "\n\n######################### test_untagged_ping_through_switch(positive) ########################\n"
        print "Applying configurations...\n"
        wrkston01Obj = self.topoObj.deviceObjGet(device="wrkston01")
        wrkston02Obj = self.topoObj.deviceObjGet(device="wrkston02")

        command = "ip route add 2.2.2.0/24 via 1.1.1.1 dev " +  wrkston01Obj.linkPortMapping['lnk01']
        returnStructure = wrkston01Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add route on WS1"

        command = "ip route add 1.1.1.0/24 via 2.2.2.1 dev " + wrkston02Obj.linkPortMapping['lnk02']
        returnStructure = wrkston02Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add route on WS2"

        sleep(SLEEP_TIME)
        ping("positive", wrkston01Obj, "2.2.2.2")

    def test_tagged_ping_to_switch_1(self):
        print "\n\n########################### test_tagged_ping_to_switch_1(positive) ###########################\n"
        print "Applying configurations...\n"
        wrkston01Obj = self.topoObj.deviceObjGet(device="wrkston01")

        command = "ifconfig " + wrkston01Obj.linkPortMapping['lnk01'] + " 0.0.0.0"
        returnStructure = wrkston01Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add 0.0.0.0 ipAddr on WS1"

        add_ws_intf_link(wrkston01Obj, wrkston01Obj.linkPortMapping['lnk01'], "10")
        conf_ws_intf(wrkston01Obj, "eth0.10", "5.5.5.2", "24", "5.5.5.255")
        set_ws_intf_up(wrkston01Obj, "eth0.10")

        sleep(SLEEP_TIME)
        ping("positive", wrkston01Obj, "5.5.5.1")

    def test_tagged_ping_to_switch_2(self):
        print "\n\n########################### test_tagged_ping_to_switch_2(negative) ###########################\n"
        print "Executing ping command...\n"
        wrkston01Obj = self.topoObj.deviceObjGet(device="wrkston01")

        sleep(SLEEP_TIME)
        ping("negative", wrkston01Obj, "1.1.1.1")

    def test_tagged_ping_through_switch_1(self):
        print "\n\n######################## test_tagged_ping_through_switch_1(positive) #########################\n"
        print "Applying configurations...\n"
        wrkston01Obj = self.topoObj.deviceObjGet(device="wrkston01")
        wrkston02Obj = self.topoObj.deviceObjGet(device="wrkston02")

        command = "ip route add 2.2.2.0/24 via 5.5.5.1 dev eth0.10"
        returnStructure = wrkston01Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add route on WS1"

        command = "ip route del 1.1.1.0/24 via 2.2.2.1 dev " + wrkston02Obj.linkPortMapping['lnk02']
        returnStructure = wrkston02Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to delete route on WS2"

        command = "ip route add 5.5.5.0/24 via 2.2.2.1 dev " + wrkston02Obj.linkPortMapping['lnk02']
        returnStructure = wrkston02Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add route on WS2"

        sleep(SLEEP_TIME)
        ping("positive", wrkston01Obj, "2.2.2.2")

    def test_checking_correct_vid_deletion(self):
        print "\n\n######################## test_checking_correct_vid_deletion(positive) ########################\n"
        print "Applying configurations...\n"
        dut01Obj = self.topoObj.deviceObjGet(device="dut01")
        wrkston01Obj = self.topoObj.deviceObjGet(device="wrkston01")

        add_and_delete_vid(dut01Obj)

        sleep(SLEEP_TIME)
        ping("positive", wrkston01Obj, "2.2.2.2")

    def test_subinterface_creation_2(self):
        print "\n\n########################### test_subinterface_creation_2(positive) ###########################\n"
        print "Applying configurations...\n"
        dut01Obj = self.topoObj.deviceObjGet(device="dut01")

        returnStructure = dut01Obj.DeviceInteract(command="vtysh")
        if returnStructure.get('returnCode') != 0:
            LogOutput('error', "Failed to get into VtyshShell")
            assert(False)

        returnStructure = dut01Obj.DeviceInteract(command="configure terminal")
        if returnStructure.get('returnCode') != 0:
            LogOutput('error', "configure terminal")
            assert(False)

        returnStructure = dut01Obj.DeviceInteract(command="interface 2.20")
        if returnStructure.get('returnCode') != 0:
            LogOutput('error', "Failed to add subinterface")
            assert(False)

        returnStructure = dut01Obj.DeviceInteract(command="encapsulation dot1Q 20")
        if returnStructure.get('returnCode') != 0:
            LogOutput('error', "Failed to configure subinterface")
            assert(False)

        dut01Obj.DeviceInteract(command="ip address 6.6.6.1/24")
        if returnStructure.get('returnCode') != 0:
            LogOutput('error', "Failed to configure subinterface")
            assert(False)

        dut01Obj.DeviceInteract(command="no shutdown")
        dut01Obj.DeviceInteract(command="exit")
        dut01Obj.DeviceInteract(command="exit")
        dut01Obj.DeviceInteract(command="exit")

        print "New subinterface was successfully added and configured!"

    def test_tagged_ping_through_switch_2(self):
        print "\n\n######################## test_tagged_ping_through_switch_2(positive) #########################\n"
        print "Applying configurations...\n"

        wrkston01Obj = self.topoObj.deviceObjGet(device="wrkston01")
        wrkston02Obj = self.topoObj.deviceObjGet(device="wrkston02")

        command = "ip route del 2.2.2.0/24 via 5.5.5.1 dev eth0.10"
        returnStructure = wrkston01Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to delete route on WS1"

        command = "ip route add 6.6.6.0/24 via 5.5.5.1 dev eth0.10"
        returnStructure = wrkston01Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add route on WS1"

        command = "ip route del 5.5.5.0/24 via 2.2.2.1 dev " + wrkston02Obj.linkPortMapping['lnk02']
        returnStructure = wrkston02Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to delete route on WS2"

        command = "ifconfig " + wrkston02Obj.linkPortMapping['lnk02'] + " 0.0.0.0"
        returnStructure = wrkston02Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add 0.0.0.0 ipAddr on WS2"

        add_ws_intf_link(wrkston02Obj, wrkston02Obj.linkPortMapping['lnk02'], "20")
        conf_ws_intf(wrkston02Obj, "eth0.20", "6.6.6.2", "24", "6.6.6.255")
        set_ws_intf_up(wrkston02Obj, "eth0.20")

        command = "ip route add 5.5.5.0/24 via 6.6.6.1 dev eth0.20"
        returnStructure = wrkston02Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add route on WS2"

        sleep(SLEEP_TIME)
        ping("positive", wrkston01Obj, "6.6.6.2")


    #[interaction tests]

    def test_checking_L2_connection(self):
        print "\n\n########################### test_checking_L2_connection(positive) ###########################\n"
        print "Applying configurations...\n"

        dut01Obj = self.topoObj.deviceObjGet(device="dut01")
        wrkston03Obj = self.topoObj.deviceObjGet(device="wrkston03")
        wrkston04Obj = self.topoObj.deviceObjGet(device="wrkston04")

        returnStructure = dut01Obj.DeviceInteract(command="vtysh")
        if returnStructure.get('returnCode') != 0:
            LogOutput('error', "Failed to get into VtyshShell")
            assert(False)

        returnStructure = dut01Obj.DeviceInteract(command="configure terminal")
        if returnStructure.get('returnCode') != 0:
            LogOutput('error', "configure terminal")
            assert(False)

        dut01Obj.DeviceInteract(command="vlan 20")
        dut01Obj.DeviceInteract(command="no shutdown")
        dut01Obj.DeviceInteract(command="exit")

        dut01Obj.DeviceInteract(command="interface 3")
        dut01Obj.DeviceInteract(command="no routing")
        dut01Obj.DeviceInteract(command="vlan access 20")
        dut01Obj.DeviceInteract(command="no shutdown")
        dut01Obj.DeviceInteract(command="exit")

        dut01Obj.DeviceInteract(command="interface 4")
        dut01Obj.DeviceInteract(command="no routing")
        dut01Obj.DeviceInteract(command="vlan access 20")
        dut01Obj.DeviceInteract(command="no shutdown")
        dut01Obj.DeviceInteract(command="exit")
        dut01Obj.DeviceInteract(command="exit")
        dut01Obj.DeviceInteract(command="exit")

        command = "ifconfig " + wrkston03Obj.linkPortMapping['lnk03'] + " 20.20.20.3/24 up"
        returnStructure = wrkston03Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add 20.20.20.3 ipAddr on WS3"

        command = "ifconfig " + wrkston04Obj.linkPortMapping['lnk04'] + " 20.20.20.4/24 up"
        returnStructure = wrkston04Obj.DeviceInteract(command=command)
        bufferout = returnStructure.get('buffer')
        retCode = returnStructure.get('returnCode')
        assert retCode == 0, "Unable to add 20.20.20.4 ipAddr on WS4"

        sleep(SLEEP_TIME)
        ping("positive", wrkston03Obj, "20.20.20.4")


    def test_checking_L3_connection(self):
        print "\n\n########################### test_checking_L3_connection(positive) ###########################\n"
        print "Executing ping command...\n"
        wrkston01Obj = self.topoObj.deviceObjGet(device="wrkston01")

        sleep(SLEEP_TIME)
        ping("positive", wrkston01Obj, "6.6.6.2")



    #[scalling test]

    def test_scalling_L3_subinterface(self):
        print "\n\n########################### test_scalling_L3_subinterface(positive) ##########################\n"
        print "Applying configurations...\n"
        dut01Obj = self.topoObj.deviceObjGet(device="dut01")

        returnStructure = dut01Obj.DeviceInteract(command="vtysh")
        if returnStructure.get('returnCode') != 0:
            LogOutput('error', "Failed to get into VtyshShell")
            assert(False)

        returnStructure = dut01Obj.DeviceInteract(command="configure terminal")
        if returnStructure.get('returnCode') != 0:
            LogOutput('error', "configure terminal")
            assert(False)

        ip = ipRange("192.168.0.1", "192.168.255.1")
        intf_count = 0
        for i in range(2, 102):

            command = "interface 1." + str(i)
            returnStructure = dut01Obj.DeviceInteract(command=command)
            if returnStructure.get('returnCode') != 0:
                LogOutput('error', "Failed to add subinterface")
                assert(False)

            command = "encapsulation dot1Q " + str(i)
            returnStructure = dut01Obj.DeviceInteract(command=command)
            if returnStructure.get('returnCode') != 0:
                LogOutput('error', "Failed to configure subinterface")
                assert(False)

            command = "ip address " + str(ip[i]) + "/24"
            returnStructure = dut01Obj.DeviceInteract(command=command)
            buff = returnStructure.get('buffer')
            if buff.find("Duplicate IP Address") >= 0:
                LogOutput('error', "Failed to configure subinterface")
                assert(False)

            returnStructure = dut01Obj.DeviceInteract(command="no shutdown")
            if returnStructure.get('returnCode') != 0:
                LogOutput('error', "Failed to configure subinterface")
                assert(False)
            dut01Obj.DeviceInteract(command="exit")
            intf_count += 1
            info("\r%d subinterfaces were successfully added and configured!" % intf_count)
        info("\n\n")
