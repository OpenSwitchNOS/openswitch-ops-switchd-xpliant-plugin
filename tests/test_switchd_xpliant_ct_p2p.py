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

        [h1-eth0]---[3]
                      S1
                    [1][2]
                     |  |
                     |  |
                    [1][2]
                      S2
        [h2-eth0]---[3]
 
    '''

    def build(self, hsts=2, sws=2, **_opts):
        self.sws = sws
        self.hsts = hsts

        # Add list of hosts
        for h in irange( 1, hsts):
            host = self.addHost('h%s' %h)

        # Add list of switches
        for s in irange(1, sws):
            switch = self.addSwitch('s%s' %s)

        # Add links between nodes based on custom topo
        self.addLink('s1', 's2')
        self.addLink('s1', 's2')
        self.addLink('h1', 's1')
        self.addLink('h2', 's2')

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

def checkLldpNeighbour(output):
   buf = output.strip().split('\n')
   portId = ''
   nPortId = ''

   for i in buf:
       # Remove extra spaces between fields
       i = re.sub('\s+',' ', i)
       rx = i.split(' : ')

       if rx[0] == "Port":
           portId = rx[1]
       elif rx[0] == "Neighbor Port-ID":
           nPortId = rx[1]

   if (portId == '') or (portId != nPortId):
       return False

   return True

def checkLacpPartner(output):
   buf = output.strip().split('Partner details of all interfaces:\n')
   # Drop actor related data and get only useful information 
   # for a partner(i.e. skip table header as well).
   buf = buf[1].split('\n')[4:]
   # Last line contains switch name since it's taken when command returns
   # to command line so just remove it. 
   del buf[-1]

   for i in buf:
       # Remove extra spaces between fields
       i = re.sub('\s+',' ', i)
       rx = i.split(' ')

       if (rx[6] == "00:00:00:00:00:00") or (rx[7] == "0"):
           # If Partner's System ID and Priority are zero
           # then we haven't received LACP frame from it.  
           return False

   return True
      

class xpSimTest( OpsVsiTest ):

    def setupNet(self):
        host_opts = self.getHostOpts()
        switch_opts = self.getSwitchOpts()
        tree_topo = myTopo(hsts=2, sws=2, hopts=host_opts, sopts=switch_opts)
        self.net = Mininet(tree_topo, switch=VsiOpenSwitch,
                           host=Host, link=OpsVsiLink,
                           controller=None, build=True)

        info("\n")
        info("    [h1-eth0]---[3]        \n")
        info("                  S1       \n")
        info("                [1][2]     \n")
        info("                 |  |      \n")
        info("                 |  |      \n")
        info("                [1][2]     \n")
        info("                  S2       \n")
        info("    [h2-eth0]---[3]        \n")


    def net_provision(self):
        info("\n### Network provisioning... ###\n")

        # NOTE: Bridge "bridge_normal" is created by default on Docker container start

        i = 1 
        for s in self.net.switches:
            s.cmdCLI("configure terminal")
            for intf in range (1, 4):
                vlanId = intf * 100
                s.cmdCLI("vlan %d" %vlanId)
                s.cmdCLI("no shutdown")
                s.cmdCLI("exit")

                info("[s%d-%d] access %d\n" % (i, intf, vlanId))

                s.cmdCLI("interface %d" %intf)
                s.cmdCLI("no routing")
                s.cmdCLI("vlan access %d" %vlanId)
                s.cmdCLI("no shutdown")
                s.cmdCLI("exit")

            s.cmdCLI("exit")
            info("\n")
            i += 1

        info("Sleep 5\n")
        sleep(5)

    def net_deprovision(self):
        info("\n\n### Network de-provisioning... ###\n")

        for s in self.net.switches:
            s.cmdCLI("configure terminal")
            s.cmdCLI("no interface lag 1")

            for intf in range (1, 4):
                vlanId = intf * 100
                s.cmdCLI("no vlan %d" %vlanId)
                s.cmdCLI("no interface %d" %intf)

            s.cmdCLI("exit")


    def lldp(self):
        i = 1
        for s in self.net.switches:
            info("### Configuring LLDP on switch %d... ###\n" %i)
        
            s.cmdCLI("configure terminal")
        
            # Configure global lldp settings
            s.cmdCLI("lldp enable")
            s.cmdCLI("lldp timer 5")

            # Configure per-interface lldp settings
            s.cmdCLI("interface 1")
            s.cmdCLI("lldp transmission")
            s.cmdCLI("lldp reception")
            s.cmdCLI("exit")

            s.cmdCLI("interface 2")
            s.cmdCLI("lldp transmission")
            s.cmdCLI("lldp reception")
            s.cmdCLI("exit")

            s.cmdCLI("exit")
            i += 1

        # Sleep for a while. This should be enough for successful 
        # exchange of LLDP frames.
        info("Sleep 20\n")
        time.sleep(20)

        i = 1
        
        for s in self.net.switches:
            # Check lldp status 
            out = s.cmdCLI("show lldp neighbor-info 1")
            status = checkLldpNeighbour(out)
            assert (status == True), "LLDP neigbour not discovered on port 1 of switch %d" %i
            info("### LLDP neighbour discovered on port 1 of switch %d ###\n" %i)

            out = s.cmdCLI("show lldp neighbor-info 2")
            status = checkLldpNeighbour(out)
            assert (status == True), "LLDP neigbour not found on port 2 of switch %d" %i
            info("### LLDP neighbour discovered on port 2 of switch %d ###\n" %i)
            i += 1

    def static_lag(self):
        h1 = self.net.hosts[ 0 ]
        h2 = self.net.hosts[ 1 ]

        info("### Ping h2 from h1 ###\n")
        out = h1.cmd("ping -c5 -i1.2 %s" % h2.IP())

        status = checkPing(out)
        assert (status == False), "Ping unexpectedly passed"
        info("### Ping Failed ###\n")

        i = 1 
        for s in self.net.switches:
            info("### Configuring static LAG with ports 1,2 on switch %d... ###\n" %i)
            s.cmdCLI("configure terminal")
            s.cmdCLI("interface lag 1")
            s.cmdCLI("no routing")
            s.cmdCLI("vlan access 300")
            s.cmdCLI("exit")

            for intf in range (1, 3):
                s.cmdCLI("interface %d" %intf)
                s.cmdCLI("lag 1")
                s.cmdCLI("exit")

            s.cmdCLI("exit")
            i += 1

        info("### Ping h2 from h1 ###\n")
        out = h2.cmd("ping -c5 -i1.2 %s" % h2.IP())

        status = checkPing(out)
        assert status, "Ping Failed even though VLAN and ports were configured correctly"
        info("### Ping Success ###\n")

    def dynamic_lag(self):
        h1 = self.net.hosts[ 0 ]
        h2 = self.net.hosts[ 1 ]

        i = 1
        for s in self.net.switches:
            info("### Configuring dynamic LAG with ports 1,2 on switch %d... ###\n" %i)
            s.cmdCLI("configure terminal")
            s.cmdCLI("interface lag 1")
            s.cmdCLI("lacp mode active")
            s.cmdCLI("exit")
            s.cmdCLI("exit")
            i += 1

        # Sleep for a while. This should be enough for successful 
        # exchange of LACP frames.
        info("Sleep 40\n")
        time.sleep(40)

        i = 1     
        for s in self.net.switches:
            # Check lacp status 
            out = s.cmdCLI("show lacp interfaces")
            status = checkLacpPartner(out)
            assert (status == True), "LACP partner not discovered on switch %d" %i
            info("### LACP partner discovered on switch %d ###\n" %i)
            i += 1

        info("### Ping h2 from h1 ###\n")
        out = h2.cmd("ping -c5 -i1.2 %s" % h2.IP())

        status = checkPing(out)
        assert status, "Ping Failed even though VLAN and ports were configured correctly"
        info("### Ping Success ###\n")


class Test_switchd_xpliant_p2p:

    def setup_class(cls):
        Test_switchd_xpliant_p2p.test = xpSimTest()
        Test_switchd_xpliant_p2p.test.net_provision()

    # TC_1
    def test_switchd_xpliant_lldp(self):
        info("\n\n########## Test Case 1 ##########\n")
        self.test.lldp()

    # TC_2
    def test_switchd_xpliant_static_lag(self):
        info("\n\n########## Test Case 2 ##########\n")
        self.test.static_lag()

    # TC_3
    def test_switchd_xpliant_dynamic_lag(self):
        info("\n\n########## Test Case 3 ##########\n")
        self.test.dynamic_lag()

    def teardown_class(cls):
        # Deprovision switches
        Test_switchd_xpliant_p2p.test.net_deprovision()

        # Stop the Docker containers, and mininet topology
        Test_switchd_xpliant_p2p.test.net.stop()
        return

    def __del__(self):
        del self.test
