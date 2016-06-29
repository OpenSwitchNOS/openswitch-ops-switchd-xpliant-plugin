# Switchd Xpliant Plugin L3 Functionality Test Cases  

## Contents  

- [Objective](#Objective)
- [Requirements](#Requirements)  
- [L3 interface configuration test case](#L3 interface configuration test case)    
- [IPv4 static routes configuration test case](#IPv4 static routes configuration test case)
   
    
    


##  Objective
The test case checks whether OPS is able to set the configurations correctly on it's ports and drive the L3 trafic over those ports. Test verifies L3 interface configurations by executing ping tests.

## Requirements
- OPS docker image with Xpliant plugin.
- **CT File**: xp-ops-built/src/ops-switchd-xpliant-plugin/tests/test_switchd_xpliant_fc_l3.py

## L3 interface configuration test case  

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
### Description  
- #### Switch configuring
    1. Enable routing on port 1.
    2. Set IP address and mask (10.1.1.1/24) on port 1.
    3. Set port 1 up.
    4. Enable routing on port 2.
    5. Set IP address and mask (20.1.1.1/24) on port 2.
    6. Set port 2 up.  
    
    To do that run the following commands in switch CLI:
    
```
        switch# configure terminal               
        switch(config)# interface 1
        switch(config-if)# routing
        switch(config-if)# ip address 10.1.1.1/24        
        switch(config-if)# no shutdown
        switch(config-if)# exit        
        switch(config)# interface 2
        switch(config-if)# routing
        switch(config-if)# ip address 20.1.1.1/24        
        switch(config-if)# no shutdown
        switch(config-if)# exit
```
- #### Host1 and Host2 configuring
    1. Set IP address and mask on eth0 interface.
    2. Set default gateway on eth0 interface.
    
          
    To do that on Host1 run the following commands:
```
        ifconfig eth0 10.1.1.2/24
        route add default gw 10.1.1.1 eth0             
```
        To do that on Host2 run the following commands:        
```
        ifconfig eth0 20.1.1.2/24
        route add default gw 20.1.1.1 eth0             
```
After all configurations start ping between hosts, host and switch.

#### Test pass criteria
All pings pass.  

## IPv4 static routes configuration test case  

### Setup topology diagram  

```ditaa
                 +------------+         +------------+
               1 |            | 2     1 |            | 2
      +---------->   Switch1  <--------->  Switch2  <---------+
 eth0 |          |            |         |            |         | eth0
+-----+------+   |            |         |            |  +------+----+
|            |   +------------+         +------------+  |           |
|   Host1    |                                          |  Host2    |
|            |                                          |           |
|            |                                          |           |
+------------+                                          +-----------+


```  
### Description  
- #### Switch1 configuring  
    1. Enable routing on port 1.
    2. Set IP address and mask (1.1.1.1/24) on port 1.
    3. Set port 1 up.
    4. Enable routing on port 2.
    5. Set IP address and mask (2.2.2.2/24) on port 2.
    6. Set port 2 up.
    7. Add static route.  
    
    To do that run the following commands in Switch1 CLI:
    
```
        switch# configure terminal               
        switch(config)# interface 1
        switch(config-if)# routing
        switch(config-if)# ip address 1.1.1.1/24        
        switch(config-if)# no shutdown
        switch(config-if)# exit        
        switch(config)# interface 2
        switch(config-if)# routing
        switch(config-if)# ip address 2.2.2.2/24        
        switch(config-if)# no shutdown
        switch(config-if)# exit
        switch(config)# ip route 3.3.3.0/24 2.2.2.1
        switch(config)# exit
```

- #### Switch2 configuring  
    1. Enable routing on port 1.
    2. Set IP address and mask (2.2.2.1/24) on port 1.
    3. Set port 1 up.
    4. Enable routing on port 2.
    5. Set IP address and mask (3.3.3.1/24) on port 2.
    6. Set port 2 up.
    7. Add static route.  
    
    To do that run the following commands in Switch1 CLI:
    
```
        switch# configure terminal               
        switch(config)# interface 1
        switch(config-if)# routing
        switch(config-if)# ip address 2.2.2.1/24        
        switch(config-if)# no shutdown
        switch(config-if)# exit        
        switch(config)# interface 2
        switch(config-if)# routing
        switch(config-if)# ip address 3.3.3.1/24        
        switch(config-if)# no shutdown
        switch(config-if)# exit
        switch(config)# ip route 1.1.1.0/24 2.2.2.2
        switch(config)# exit
```

- #### Host1 and Host2 configuring
    1. Set IP address and mask on eth0 interface.
    2. Set default gateway on eth0 interface.
    
          
    To do that on Host1 run the following commands:
```
        ifconfig eth0 1.1.1.2/24
        route add default gw 1.1.1.1 eth0             
```
        To do that on Host2 run the following commands:        
```
        ifconfig eth0 3.3.3.2/24
        route add default gw 3.3.3.1 eth0             
```
After all configurations start ping between hosts, host and switch, and between switches.

#### Test pass criteria
All pings pass.