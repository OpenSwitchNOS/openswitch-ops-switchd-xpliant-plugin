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

from os.path import basename
import time
from opsvsi.docker import *
from opsvsi.opsvsitest import *
from opsvsiutils.systemutil import *

class myTopo( Topo ):
    '''
        Custom Topology Example

                       [1]S1[2]
                   ____/     \_____
                 /                 \
             [1]S2[2]           [1]S3[2]
             /     \            /     \
         [h1-eth0][h2-eth0] [h3-eth0] [h4-eth0]
    '''

    def build(self, hsts=6, sws=3, **_opts):
        self.hsts = hsts
        self.sws = sws

        # Add list of hosts
        for h in irange( 1, hsts):
            host = self.addHost( 'h%s' % h)

        # Add list of switches
        for s in irange(1, sws):
            switch = self.addSwitch( 's%s' %s)

        # Add links between nodes based on custom topo
        self.addLink('s1', 's2')
        self.addLink('s1', 's3')
        self.addLink('h1', 's2')
        self.addLink('h2', 's2')
        self.addLink('h3', 's3')
        self.addLink('h4', 's3')

def checkPing(pingOutput):
        '''Parse ping output and check to see if one of the pings succeeded or failed'''
        # Check for downed link

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
        tree_topo = myTopo(hsts=4, sws=3, hopts=host_opts, sopts=switch_opts)
        self.net = Mininet(tree_topo, switch=VsiOpenSwitch,
                           host=Host, link=OpsVsiLink,
                           controller=None, build=True)

        info("\n")
        info("                 [1]S1[2]                 \n")
        info("            _____/      \______           \n")
        info("           /                   \          \n")
        info("         [1]                   [1]        \n")
        info("      [2]S2[3]              [2]S3[3]      \n")
        info("      /     \               /     \       \n")
        info("[h1-eth0]  [h2-eth0]   [h3-eth0]  [h4-eth0]\n")

    def net_provision(self):
        s1 = self.net.switches[ 0 ]
        s2 = self.net.switches[ 1 ]
        s3 = self.net.switches[ 2 ]

        info("\n### Network provisioning... ###\n")

        # NOTE: Bridge "bridge_normal" is created by default on Docker container start

        info("[s1-1] trunk 200,300\n")
        info("[s1-2] trunk 200,300\n")
        s1.cmdCLI("configure terminal")
        s1.cmdCLI("vlan 200")
        s1.cmdCLI("no shutdown")
        s1.cmdCLI("vlan 300")
        s1.cmdCLI("no shutdown")
        s1.cmdCLI("exit")
        s1.cmdCLI("interface 1")
        s1.cmdCLI("no routing")
        s1.cmdCLI("vlan trunk allowed 200")
        s1.cmdCLI("vlan trunk allowed 300")
        s1.cmdCLI("no shutdown")
        s1.cmdCLI("exit")
        s1.cmdCLI("interface 2")
        s1.cmdCLI("no routing")
        s1.cmdCLI("vlan trunk allowed 200")
        s1.cmdCLI("vlan trunk allowed 300")
        s1.cmdCLI("no shutdown")
        s1.cmdCLI("exit")
        info("\n")

        info("[s2-1] trunk 200,300\n")
        info("[s2-2] access 200\n")
        info("[s2-3] access 300\n")
        s2.cmdCLI("configure terminal")
        s2.cmdCLI("vlan 200")
        s2.cmdCLI("no shutdown")
        s2.cmdCLI("vlan 300")
        s2.cmdCLI("no shutdown")
        s2.cmdCLI("exit")
        s2.cmdCLI("interface 1")
        s2.cmdCLI("no routing")
        s2.cmdCLI("vlan trunk allowed 200")
        s2.cmdCLI("vlan trunk allowed 300")
        s2.cmdCLI("no shutdown")
        s2.cmdCLI("exit")
        s2.cmdCLI("interface 2")
        s2.cmdCLI("no routing")
        s2.cmdCLI("vlan access 200")
        s2.cmdCLI("no shutdown")
        s2.cmdCLI("exit")
        s2.cmdCLI("interface 3")
        s2.cmdCLI("no routing")
        s2.cmdCLI("vlan access 300")
        s2.cmdCLI("no shutdown")
        s2.cmdCLI("exit")
        info("\n")

        info("[s3-1] trunk 200,300\n")
        info("[s3-2] access 200\n")
        info("[s3-3] access 300\n")
        s3.cmdCLI("configure terminal")
        s3.cmdCLI("vlan 200")
        s3.cmdCLI("no shutdown")
        s3.cmdCLI("vlan 300")
        s3.cmdCLI("no shutdown")
        s3.cmdCLI("exit")
        s3.cmdCLI("interface 1")
        s3.cmdCLI("no routing")
        s3.cmdCLI("vlan trunk allowed 200")
        s3.cmdCLI("vlan trunk allowed 300")
        s3.cmdCLI("no shutdown")
        s3.cmdCLI("exit")
        s3.cmdCLI("interface 2")
        s3.cmdCLI("no routing")
        s3.cmdCLI("vlan access 200")
        s3.cmdCLI("no shutdown")
        s3.cmdCLI("exit")
        s3.cmdCLI("interface 3")
        s3.cmdCLI("no routing")
        s3.cmdCLI("vlan access 300")
        s3.cmdCLI("no shutdown")
        s3.cmdCLI("exit")
        info("\n")

        info("Sleep 7\n")
        sleep(7)

    def net_deprovision(self):
        info("\n\n### Network de-provisioning... ###\n")
        s1 = self.net.switches[ 0 ]
        s2 = self.net.switches[ 1 ]
        s3 = self.net.switches[ 2 ]

        s1.ovscmd("/usr/bin/ovs-vsctl del-vlan bridge_normal 200")
        s1.ovscmd("/usr/bin/ovs-vsctl del-vlan bridge_normal 300")
        s1.ovscmd("/usr/bin/ovs-vsctl del-port bridge_normal 1")
        s1.ovscmd("/usr/bin/ovs-vsctl del-port bridge_normal 2")

        s2.ovscmd("/usr/bin/ovs-vsctl del-vlan bridge_normal 200")
        s2.ovscmd("/usr/bin/ovs-vsctl del-vlan bridge_normal 300")
        s2.ovscmd("/usr/bin/ovs-vsctl del-port bridge_normal 1")
        s2.ovscmd("/usr/bin/ovs-vsctl del-port bridge_normal 2")
        s2.ovscmd("/usr/bin/ovs-vsctl del-port bridge_normal 3")

        s3.ovscmd("/usr/bin/ovs-vsctl del-vlan bridge_normal 200")
        s3.ovscmd("/usr/bin/ovs-vsctl del-vlan bridge_normal 300")
        s3.ovscmd("/usr/bin/ovs-vsctl del-port bridge_normal 1")
        s3.ovscmd("/usr/bin/ovs-vsctl del-port bridge_normal 2")
        s3.ovscmd("/usr/bin/ovs-vsctl del-port bridge_normal 3")

    def vlan_200(self):
        h1 = self.net.hosts[ 0 ]
        h3 = self.net.hosts[ 2 ]

        info("### Ping h3 from h1 ###\n")
        out = h1.cmd("ping -c5 -i1.2 %s" % h3.IP())

        status = checkPing(out)
        assert status, "Ping Failed even though VLAN and ports were configured correctly"
        info("### Ping Success ###\n")

    def vlan_300(self):
        h2 = self.net.hosts[ 1 ]
        h4 = self.net.hosts[ 3 ]

        info("### Ping h4 from h2 ###\n")
        out = h2.cmd("ping -c5 -i1.2 %s" % h4.IP())

        status = checkPing(out)
        assert status, "Ping Failed even though VLAN and ports were configured correctly"
        info("### Ping Success ###\n")

    def vlan_200_300_negative(self):
        h1 = self.net.hosts[ 0 ]
        h2 = self.net.hosts[ 1 ]
        h3 = self.net.hosts[ 2 ]
        h4 = self.net.hosts[ 3 ]

        info("### Ping h2 from h1 ###\n")
        out = h1.cmd("ping -c5 -i1.2 %s" % h2.IP())

        status = checkPing(out)
        assert (status == False), "Ping unexpectedly passed"
        info("### Ping Failed ###\n")

        info("### Ping h4 from h1 ###\n")
        out = h1.cmd("ping -c5 -i1.2 %s" % h4.IP())

        status = checkPing(out)
        assert (status == False), "Ping unexpectedly passed"
        info("### Ping Failed ###\n")

class Test_switchd_xpliant_tree2:

    def setup_class(cls):
        Test_switchd_xpliant_tree2.test = xpSimTest()
        Test_switchd_xpliant_tree2.test.net_provision()

    # TC_1
    def test_switchd_xpliant_vlan_200(self):
        info("\n\n\n########## Test Case 1 ##########\n")
        self.test.vlan_200()

    # TC_2
    def test_switchd_xpliant_vlan_300(self):
        info("\n\n\n########## Test Case 2 ##########\n")
        self.test.vlan_300()

    # TC_3
    def test_switchd_xpliant_vlan_200_300_negative(self):
        info("\n\n\n########## Test Case 3 ##########\n")
        self.test.vlan_200_300_negative()

    def teardown_class(cls):
        # Deprovision switches
        Test_switchd_xpliant_tree2.test.net_deprovision()

        # Stop the Docker containers, and mininet topology
        Test_switchd_xpliant_tree2.test.net.stop()

    def __del__(self):
        del self.test
