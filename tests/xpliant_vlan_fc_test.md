# Switchd Xpliant Plugin VLAN Functionality Test Cases

## Contents

- [Objective](#Objective)
- [Requirements](#Requirements)
- [Port in Trunk VLAN mode](#Port in trunk VLAN mode)    
    - [Setup topology diagram](#Setup topology diagram)
    - [Test Case 1: Trunk1 - Positive](#Test Case 1: Trunk1 - Positive)
        - [Description for Trunk1](#Description for Trunk1)
        - [Test pass criteria for Trunk1](#Test pass criteria for Trunk1)
    - [Test Case 2: Trunk2 - Negative](#Test Case 2: Trunk2 - Negative)
        - [Description for Trunk2](#Description for Trunk2)
        - [Test pass criteria for Trunk2](#Test pass criteria for Trunk2)
- [Port in Access VLAN mode](#Port in trunk Access mode)
    - [Test Case 1: Access1](#Test Case 1: Access1)
        - [Setup topology diagram](#Setup topology diagram)
        - [Description for Access1](#Description for Access1)
        - [Test pass criteria for Access1](#Test pass criteria for Access1)
    - [Test Case 2: Access2](#Test Case 2: Access2)
        - [Setup topology diagram](#Setup topology diagram)
        - [Description for Access2](#Description for Access2)
        - [Test pass criteria for Access2](#Test pass criteria for Access2)  
    - [Test Case 3: Access3](#Test Case 3: Access3)
        - [Setup topology diagram](#Setup topology diagram)
        - [Description for Access3](#Description for Access3)
        - [Test pass criteria for Access3](#Test pass criteria for Access3)

  
  
##  Objective
The test case checks whether OPS is able to set the configurations correctly on it's ports and drive the VLAN tagged trafic over those ports. The ports in Trunk and Access modes get configured only when corresponding VLAN is added. The following test cases verify ping traffic in different configurations.

## Requirements
- OPS docker image with Xpliant plugin.
- **CT Files**:
    - xp-ops-built/src/ops-switchd-xpliant-plugin/tests/test_swithd_xlpiant_fc_vlan_access_mode.py
    - xp-ops-built/src/ops-switchd-xpliant-plugin/tests/test_swithd_xlpiant_fc_vlan_trunk_mode.py

##  Port in Trunk VLAN mode

### Setup topology diagram
```ditaa
                 +-------------+      
               1 |             | 2
      +---------->   Switch    <----------+
 eth0 |          |             |          | eth0
+-----+------+   |             |   +------+-----+
|            |   +-------------+   |            |
|            |                     |            |
|    Host1   |                     |    Host2   |
|            |                     |            |
+------------+                     +------------+

```
### Test Case 1: Trunk1 - Positive

#### Description for Trunk1
 - #### Switch configuring
    1. Add `VLAN20` to the global VLAN table on Switch.
    2. Add `VLAN30` to the global VLAN table on Switch.
    3. Add `VLAN40` to the global VLAN table on Switch
    4. Add port 1 with trunks=20,30,40 on switch in trunk mode.
    5. Set port 1 up.
    6. Add port 2 with trunks=20,30,40 on switch1 in trunk mode.
    7. Set port 2 up.  
    
    To do that run the following commands in switch CLI:
    
```
        switch# configure terminal
        switch(config)# vlan 20
        switch(config-vlan)# no shutdown
        switch(config-vlan)# vlan 30
        switch(config-vlan)# no shutdown
        switch(config-vlan)# vlan 40
        switch(config-vlan)# no shutdown       
        switch(config-vlan)# exit        
        switch(config)# interface 1
        switch(config-if)# no routing
        switch(config-if)# vlan trunk allowed 20
        switch(config-if)# vlan trunk allowed 30
        switch(config-if)# vlan trunk allowed 40
        switch(config-if)# no shutdown
        switch(config-if)# exit        
        switch(config)# interface 2
        switch(config-if)# no routing
        switch(config-if)# vlan trunk allowed 20
        switch(config-if)# vlan trunk allowed 30
        switch(config-if)# vlan trunk allowed 40
        switch(config-if)# no shutdown
        switch(config-if)# exit
```
 - #### Host1 and Host2 configuring
    1. Delete IP-Addr on eth0.
    2. Add VLAN-type link to eth0 with VID 20.
    3. Set IP-Addr, mask and Bcast-Addr on link interface.
    4. Set link interface up.   
       
    To do that run the following commands (example for Host2):
    
```
        ifconfig eth0 0.0.0.0
        ip link add link eth0 name eth0.20 type vlan id 20
        ip addr add 1.1.1.2/24 brd 1.1.1.255 dev eth0.20
        ip link set dev eth0.20 up        
```    
After all configurations ping Host2 from Host1. All packets will be taggged by VID 20.  
Than reconfigure hosts to VID 30, then to VID 40.

 - #### Host1 and Host2 reconfiguring
    1. Set link interface with VID 20 down.
    2. Add new VLAN-type link to eth0 with VID 30.
    3. Set IP-Addr, mask and Bcast-Addr on new link interface.
    4. Set new link interface up.  
          
    To do that run the following commands (example for Host2):
```
        ip link set dev eth0.20 down
        ip link add link eth0 name eth0.30 type vlan id 30
        ip addr add 1.1.1.2/24 brd 1.1.1.255 dev eth0.30
        ip link set dev eth0.30 up        
```    
After hosts reconfigurations ping Host2 from Host1.

#### Test pass criteria for Trunk1
Ping from Host1 to Host2 should be successful and **only** when both the ports have **VID** = 20, 30 and 40.  

### Test Case 2: Trunk2 - Negative

#### Description for Trunk2
Switch configuration is the same like in Test Case 1. Hosts configurations are also like in Test Case 1, but instead VID 20 will be VID 21, VID 30 changes to VID 32 and VID 40 - to VID 43.

#### Test pass criteria for Trunk2
Ping from Host1 to Host2 should be unsuccessful, because all packets must be dropped by switch.   

##  Port in trunk Access mode

### Test Case 1: Access1
### Setup topology diagram
```ditaa
                     +-------------+      
                   1 |             | 3
      +-------------->   Switch    <--------------+
 eth0 |              |             |              | eth0
+-----+------+       |             |       +------+-----+
|            |       +------^------+       |            |
|            |              | 2            |            |
|    Host1   |              |              |    Host3   |
|            |              | eth0         |            |
+------------+        +-----+------+       +------------+
                      |            |
                      |            |
                      |   Host2    |
                      |            |
                      +------------+

```
#### Description for Access1  
- #### Switch configuring
    1. Add `VLAN125` to the global VLAN table on Switch.
    2. Set port 1 into access mode with tag 125.
    3. Set port 1 up
    4. Add `VLAN225` to the global VLAN table on Switch.
    5. Set port 2 into access mode with tag 225.
    6. Set port 2 up.
    7. Set port 3 into access mode with tag 125.
    8. Set port 3 up.  
    
    To do that run the following commands in switch CLI:

```
        switch# configure terminal
        switch(config)# vlan 125
        switch(config-vlan)# no shutdown
        switch(config-vlan)# exit              
        switch(config)# interface 1
        switch(config-if)# no routing
        switch(config-if)# vlan access 125        
        switch(config-if)# no shutdown
        switch(config-if)# exit
        switch(config)# vlan 225
        switch(config-vlan)# no shutdown
        switch(config-vlan)# exit              
        switch(config)# interface 2
        switch(config-if)# no routing
        switch(config-if)# vlan access 225        
        switch(config-if)# no shutdown
        switch(config-if)# exit        
        switch(config)# interface 3
        switch(config-if)# no routing
        switch(config-if)# vlan access 125       
        switch(config-if)# no shutdown
        switch(config-if)# exit
```
- #### Host1, Host2 and Host3 configuring
    Needed only to set IP-Addr, mask and Bcast-Addr on eth0 interface.
       
    To do that run the following command (example for Host1):
    
```        
        ip addr add 1.1.1.1/24 brd 1.1.1.255 dev eth0        
```
After configurations ping from Host1 Host2 and Host3 in turn.  

####Test pass criteria for Access1  
Host3 pinging must be successful, otherwise Host2 pinging must be failed.  

### Test Case 2: Access2
### Setup topology diagram  
```ditaa
                 +-------------+      
               1 |             | 2
      +---------->   Switch    <----------+
 eth0 |          |             |          | eth0
+-----+------+   |             |   +------+-----+
|            |   +-------------+   |            |
|            |                     |            |
|    Host1   |                     |    Host2   |
|            |                     |            |
+------------+                     +------------+

```
#### Description for Access2  
- #### Switch configuring
    1. Add `VLAN125` to the global VLAN table on Switch.
    2. Set port 1 into access mode with tag 125.
    3. Set port 1 up    
    4. Set port 3 into access mode with tag 125.
    5. Set port 3 up.  
    
    To do that run the following commands in switch CLI:

```
        switch# configure terminal
        switch(config)# vlan 125
        switch(config-vlan)# no shutdown
        switch(config-vlan)# exit              
        switch(config)# interface 1
        switch(config-if)# no routing
        switch(config-if)# vlan access 125        
        switch(config-if)# no shutdown
        switch(config-if)# exit        
        switch(config)# interface 2
        switch(config-if)# no routing
        switch(config-if)# vlan access 125       
        switch(config-if)# no shutdown
        switch(config-if)# exit
```
- #### Host1 configuring
    1. Delete IP-Addr on eth0.
    2. Add VLAN-type link to eth0 with VID 125.
    3. Set IP-Addr, mask and Bcast-Addr on link interface.
    4. Set link interface up.   
       
    To do that run the following commands:
    
```
        ifconfig eth0 0.0.0.0
        ip link add link eth0 name eth0.125 type vlan id 125
        ip addr add 1.1.1.1/24 brd 1.1.1.255 dev eth0.125
        ip link set dev eth0.125 up        
```
- #### Host2 configuring
    Needed only to set IP-Addr, mask and Bcast-Addr on eth0 interface.
       
    To do that run the following command (example for Host1):
    
```        
        ip addr add 1.1.1.2/24 brd 1.1.1.255 dev eth0        
```
Now ping Host2 from Host1 (all egress packets from Host1 will be tagged by VID 125). After that reconfigure Host1 for sending untagged traffic.  

- #### Host1 reconfiguring  
    1. Set link interface with VID 125 down.    
    2. Set IP-Addr, mask and Bcast-Addr on eth.0 interface.     
          
    To do that run the following commands:
```
        ip link set dev eth0.125 down        
        ip addr add 1.1.1.1/24 brd 1.1.1.255 dev eth0       
``` 
Now ping Host2 from Host1 again.

####Test pass criteria for Access2  
Host2 pinging must be successful when all packets from Host1 are untagged, otherwise Host2 pinging must be failed. In other words, first ping attempt must be failed and second attempt - successful.  

### Test Case 3: Access3
### Setup topology diagram  
```ditaa
                 +-------------+      
               1 |             | 2
      +---------->   Switch    <----------+
 eth0 |          |             |          | eth0
+-----+------+   |             |   +------+-----+
|            |   +-------------+   |            |
|            |                     |            |
|    Host1   |                     |    Host2   |
|            |                     |            |
+------------+                     +------------+

```
#### Description for Access3  
- #### Switch configuring
    1. Add `VLAN125` to the global VLAN table on Switch.
    2. Set port 1 with trunks=125 on switch in trunk mode.
    3. Set port 1 up    
    4. Set port 3 into access mode with tag 125.
    5. Set port 3 up.  
    
    To do that run the following commands in switch CLI:

```
        switch# configure terminal
        switch(config)# vlan 125
        switch(config-vlan)# no shutdown
        switch(config-vlan)# exit              
        switch(config)# interface 1
        switch(config-if)# no routing
        switch(config-if)# vlan trunk allowed 125        
        switch(config-if)# no shutdown
        switch(config-if)# exit        
        switch(config)# interface 2
        switch(config-if)# no routing
        switch(config-if)# vlan access 125       
        switch(config-if)# no shutdown
        switch(config-if)# exit
```
- #### Host1 configuring
    1. Delete IP-Addr on eth0.
    2. Add VLAN-type link to eth0 with VID 125.
    3. Set IP-Addr, mask and Bcast-Addr on link interface.
    4. Set link interface up.   
       
    To do that run the following commands:
    
```
        ifconfig eth0 0.0.0.0
        ip link add link eth0 name eth0.125 type vlan id 125
        ip addr add 1.1.1.1/24 brd 1.1.1.255 dev eth0.125
        ip link set dev eth0.125 up        
```
- #### Host2 configuring
    Needed only to set IP-Addr, mask and Bcast-Addr on eth0 interface.
       
    To do that run the following command (example for Host1):
    
```        
        ip addr add 1.1.1.2/24 brd 1.1.1.255 dev eth0        
```
Now ping Host2 from Host1 (all egress packets from Host1 will be tagged by VID 125).

####Test pass criteria for Access3  
Host2 pinging must be successful.