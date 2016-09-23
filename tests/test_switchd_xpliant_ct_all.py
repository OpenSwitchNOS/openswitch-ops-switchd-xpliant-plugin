#!/usr/bin/python
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
from opsvsi.docker import *
from opsvsi.opsvsitest import *
from opsvsiutils.systemutil import *
import os.path
import time
import sys

def checkPing(pingOutput):
        '''Parse ping output and check to see if one of the pings succeeded or failed'''
        # Check for downed link
        if 'Destination Host Unreachable' in pingOutput:
            return False
        r = r'(\d+) packets transmitted, (\d+) received'
        m = re.search(r, pingOutput)
        if m is None:
            return False
        sent, received = int(m.group(1)), int(m.group(2))
        if sent >= 1 and received >=1:
            return True
        else:
            return False

class xpSimTest( OpsVsiTest ):

    def setupNet(self):
        host_opts = self.getHostOpts()
        switch_opts = self.getSwitchOpts()
        vlan_topo = SingleSwitchTopo(k=2, hopts=host_opts, sopts=switch_opts)
        self.net = Mininet(vlan_topo, switch=VsiOpenSwitch,
                           host=Host, link=OpsVsiLink,
                           controller=None, build=True)


    def vlan_normal(self):
        '''
            H1[h1-eth0]<--->[1]S1[2]<--->[h2-eth0]H2

            1.1 Add VLAN 200 to global VLAN table on Switch
            1.2 Add port 1 and 2 with tag 200 on Switch (access mode)
            1.3 Test Ping - should work
        '''
        s1 = self.net.switches[ 0 ]
        h1 = self.net.hosts[ 0 ]
        h2 = self.net.hosts[ 1 ]

        info("\n\n\n########## Test Case 1 - Adding VLAN and Ports ##########\n")
        info("### Adding VLAN200 and ports 1,2 to bridge br0.",
             "Port1:access (tag=200), Port2:access (tag=200) ###\n")
        info("### Running ping traffic between hosts: h1, h2 ###\n")

        # NOTE: Bridge "bridge_normal" is created by default on Docker container start
        s1.cmdCLI("configure terminal")
        s1.cmdCLI("vlan 200")
        s1.cmdCLI("no shutdown")
        s1.cmdCLI("exit")
        s1.cmdCLI("interface 1")
        s1.cmdCLI("no routing")
        s1.cmdCLI("vlan access 200")
        s1.cmdCLI("no shutdown")
        s1.cmdCLI("exit")
        s1.cmdCLI("interface 2")
        s1.cmdCLI("no routing")
        s1.cmdCLI("vlan access 200")
        s1.cmdCLI("no shutdown")
        s1.cmdCLI("exit")

        info("Sleep 7\n")
        sleep(7)
        
        out = h1.cmd("ping -c5 %s" % h2.IP())

        status = checkPing(out)
        assert status, "Ping Failed even though VLAN and ports were configured correctly"
        info("\n### Ping Success ###\n")

        #Cleanup before next test
        s1.ovscmd("/usr/bin/ovs-vsctl del-vlan bridge_normal 200")
        s1.ovscmd("/usr/bin/ovs-vsctl del-port bridge_normal 1")
        s1.ovscmd("/usr/bin/ovs-vsctl del-port bridge_normal 2")

class Test_switchd_xpliant_all:

    def setup_class(cls):
        Test_switchd_xpliant_all.test = xpSimTest()

    # TC_1
    def test_switchd_xpliant_vlan_normal(self):
        self.test.vlan_normal()

    def teardown_class(cls):
        # Stop the Docker containers, and
        # mininet topology
        Test_switchd_xpliant_all.test.net.stop()

    def __del__(self):
        del self.test
