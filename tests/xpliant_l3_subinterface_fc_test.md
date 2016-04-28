# Switchd Xpliant Plugin L3 VLAN-subinterfaces Test Cases  

## Contents  

+ [Objective](#Objective)
+ [Requirements](#Requirements)
+ [Setup topology diagram](#Setup topology diagram)
+ [Base switch configuration](#Base switch configuration)
+ [Test cases](#Test cases)
    - [Subinterface_creation_1](#Subinterface_creation_1)
    - [Untagged_ping_to_switch_1 (positive)](#Untagged_ping_to_switch_1 (positive))
    - [Untagged_ping_to_switch_2 (negative)](#Untagged_ping_to_switch_2 (negative))
    - [Untagged_ping_to_switch_3 (positive)](#Untagged_ping_to_switch_3 (positive))
    - [Untagged_ping_to_switch_4 (negative)](#Untagged_ping_to_switch_4 (negative))
    - [Untagged_ping_through_switch (positive)](#Untagged_ping_through_switch (positive))
    - [Tagged_ping_to_switch_1 (positive)](#Tagged_ping_to_switch_1 (positive))
    - [Tagged_ping_to_switch_2 (negative)](#Tagged_ping_to_switch_2 (negative))
    - [Tagged_ping_through_switch_1 (positive)](#Tagged_ping_through_switch_1 (positive))
    - [Checking_correct_vid_deletion (positive)](#Checking_correct_vid_deletion (positive))
    - [Subinterface_creation_2 (positive)](#Subinterface_creation_2 (positive))
    - [Tagged_ping_through_switch_2 (positive)](#Tagged_ping_through_switch_2 (positive))
    - [Checking_L2_connection (positive)](#Checking_L2_connection (positive))
    - [Checking_L3_connection (positive)](#Checking_L3_connection (positive))
    - [Scalling_test](#Scalling test)


## Objective
The test cases check whether OPS is able to set the configurations correctly on it's ports and drive the L3 VLAN-tagged and untagged traffic over those ports. Test verifies L3 VLAN-subinterface configurations and their influence on other features by executing ping tests.

## Requirements
- OPS docker image with Xpliant plugin.
- **CT File**: ops-built/src/ops-switchd-xpliant-plugin/tests/test_switchd_xpliant_fc_subinterface.py

##Setup topology diagram

```ditaa

  +------------+             +------------------+             +------------+
  |            |             |                  |             |            |
  |            |eth0       1 |                  | 2       eth0|            |
  |   Host1    +------------->      Switch      <-------------+    Host2   |
  |            |             |                  |             |            |
  |            |             |                  |             |            |
  +------------+             +---^----------^---+             +------------+
                               3 |        4 |
                                 |          |   
           +------------+        |          |        +------------+
           |            |        |          |        |            |
           |            |eth0    |          |    eth0|            |
           |   Host3    +--------+          +--------+   Host4    |
           |            |                            |            |
           |            |                            |            |
           +------------+                            +------------+
           
```

## Base Switch configuration:

    1. Enable routing on interface 1.
    2. Set IP address and mask (1.1.1.1/24) on interface 1.
    3. Set interface 1 up.
    4. Enable routing on interface 2.
    5. Set IP address and mask (2.2.2.1/24) on interface 2.
    6. Set interface 2 up.
        
To do that run the following commands in Switch CLI:
    
```
    switch# configure terminal               
    switch(config)# interface 1
    switch(config-if)# routing
    switch(config-if)# ip address 1.1.1.1/24        
    switch(config-if)# no shutdown
    switch(config-if)# exit        
    switch(config)# interface 2
    switch(config-if)# routing
    switch(config-if)# ip address 2.2.2.1/24        
    switch(config-if)# no shutdown
    switch(config-if)# exit    
```
# Test cases

## Subinterface_creation_1

### Description

#### Switch configuring:  

        1. Create subinterface 1.10.
        2. Set VLAN 10 encapsulation on 1.10. 
        3. Set IP address and mask (5.5.5.1/24) on 1.10.
        4. Set 1.10 up.        
            
To do that run the following commands in Switch CLI:  
 
```
    switch# configure terminal               
    switch(config)# interface 1.10
    switch(config-subif)# encapsulation dot1Q 10        
    switch(config-subif)# ip address 5.5.5.1/24           
    switch(config-subif)# no shutdown
    switch(config-subif)# exit    
``` 
#### Test pass criteria
All configuration has been applied successfully.  



## Untagged_ping_to_switch_1 (positive)  

### Description

**Switch configuration** is the previous.

#### Host1 configuring:

        1. Set IP address and mask on eth0 interface.
        2. Set eth0 up.
        
To do that on Host1 run the following command:
```     
    ifconfig eth0 1.1.1.2/24 up             
```  

Then ping Switch interface 1 from Host1. 

#### Test pass criteria
Ping succeeded.  



## Untagged_ping_to_switch_2 (negative)

### Description

**Switch and Host1 configurations** are the previous.
  
Ping Switch interface 1.10 from Host1.    
          
#### Test pass criteria
Ping failed.



## Untagged_ping_to_switch_3 (positive)  

### Description

**Switch configuration** is the previous.

#### Host2 configuring:

        1. Set IP address and mask on eth0 interface.
        2. Set eth0 up.
        
To do that on Host2 run the following command:
```  
    ifconfig eth0 2.2.2.2/24 up
```  

Then ping Switch interface 2 from Host1. 

#### Test pass criteria
Ping succeeded.



## Untagged_ping_to_switch_4 (negative)

### Description

**Switch and Host2 configuration** is the previous.
  
Ping Switch interface 1.10 from Host2.    
          
#### Test pass criteria
Ping failed.



## Untagged_ping_through_switch (positive)

### Description

**Switch configuration** is the previous.

#### Host1 and Host2 configuring:

        1. Set static routes on eth0 interface.    

To do that on Host1 run the following command:
```     
    ip route add 2.2.2.0/24 via 1.1.1.1 dev eth0             
```
To do that on Host2 run the following command:        
```    
    ip route add 1.1.1.0/24 via 2.2.2.1 dev eth0             
```
Then ping Host2 from Host1. 

#### Test pass criteria
Ping succeeded.



## Tagged_ping_to_switch_1 (positive)

### Description

**Switch configuration** is the previous.

#### Host1 configuring:

        1. Delete IP-Addr on eth0.
        2. Add VLAN-type link to eth0 with VID 10.
        3. Set IP-Addr, mask and Bcast-Addr on link interface.
        4. Set link interface up.
       
To do that run the following commands:    
```
    ifconfig eth0 0.0.0.0
    ip link add link eth0 name eth0.10 type vlan id 10
    ip addr add 5.5.5.2/24 brd 5.5.5.255 dev eth0.10
    ip link set dev eth0.10 up        
```      

Then ping Switch interface 1.10 from Host1. 

#### Test pass criteria
Ping succeeded.  



## Tagged_ping_to_switch_2 (negative)

### Description

**Switch and Host1 configuration** is the previous.
  
Ping Switch interface 1 from Host1.    
          
#### Test pass criteria
Ping failed.



## Tagged_ping_through_switch_1 (positive)

### Description

**Switch configuration** is the previous.

#### Host1 and Host2 configuring:

        1. Set static route on Host1 eth0.10 interface.
        2. Delete previous static route on Host2 eth0 interface.
        3. Set new static route on Host2 eth0 interface.    

To do that on Host1 run the following command:
```     
    ip route add 2.2.2.0/24 via 5.5.5.1 dev eth0.10             
```
To do that on Host2 run the following command:        
```    
    ip route del 1.1.1.0/24 via 2.2.2.1 dev eth0
    ip route add 5.5.5.0/24 via 2.2.2.1 dev eth0             
```
Then ping Host2 from Host1. 

#### Test pass criteria
Ping succeeded.



## Checking_correct_vid_deletion (positive)

### Description

This test verifies that deletion of user created VLANs (VLANs created for L2) does not affect VLANs, which has been created for subinterfaces or other features.

#### Switch configuring:  

        1. Add VLAN10 to the global VLAN table on Switch.
        2. Set it up. 
        3. Delete VLAN 10.        
            
To do that run the following commands in Switch CLI:  
 
```
    switch# configure terminal               
    switch(config)# vlan 10
    switch(config-vlan)# no shutdown        
    switch(config-vlan)# no vlan 10
    switch(config-vlan)# exit    
```

**Host1 and Host2 configurations** are the previous.

Then ping Host2 from Host1.  

#### Test pass criteria
Ping succeeded.




## Subinterface_creation_2 (positive)

### Description

#### Switch configuring:  

        1. Create subinterface 2.20.
        2. Set VLAN 20 encapsulation on 2.20. 
        3. Set IP address and mask (6.6.6.1/24) on 2.20.
        4. Set 2.20 up.        
            
To do that run the following commands in Switch CLI:  
 
```
    switch# configure terminal               
    switch(config)# interface 2.20
    switch(config-subif)# encapsulation dot1Q 20        
    switch(config-subif)# ip address 6.6.6.1/24           
    switch(config-subif)# no shutdown
    switch(config-subif)# exit    
``` 
#### Test pass criteria
All configurations has been applied successfully.  



## Tagged_ping_through_switch_2 (positive)  

### Description

**Switch configuration** is the previous.

#### Host1 and Host2 configuring:

        1. Delete previous static route on Host1 eth0.10 interface.
        2. Set new static route on Host1 eth0.10 interface.
        3. Delete previous static route on Host2 eth0 interface.
        4. Delete IP-Addr on Host2 eth0 interface.
        5. Add VLAN-type link to Host2 eth0 interface with VID 20.
        6. Set IP-Addr, mask and Bcast-Addr on Host2 link interface.
        7. Set Host2 link interface up.
        8. Set static route on Host2 eth0.20 interface.

To do that on Host1 run the following command:
```     
    ip route del 2.2.2.0/24 via 5.5.5.1 dev eth0.10
    ip route add 6.6.6.0/24 via 5.5.5.1 dev eth0.10             
```
To do that on Host2 run the following command:        
```    
    ip route del 5.5.5.0/24 via 2.2.2.1 dev eth0
    ifconfig eth0 0.0.0.0
    ip link add link eth0 name eth0.20 type vlan id 20
    ip addr add 6.6.6.2/24 brd 6.6.6.255 dev eth0.20
    ip link set dev eth0.20 up
    ip route add 5.5.5.0/24 via 6.6.6.1 dev eth0.20             
```
Then ping Host2 from Host1. 

#### Test pass criteria
Ping succeeded.  



## Checking_L2_connection (positive)  

### Description

This test verifies that L3 VLAN-subinterfaces do not change the logic of other features, in particular L2 logic.

#### Switch configuring:  

        1. Add VLAN 20 to the global VLAN table on Switch.
        2. Set VLAN 20 up.
        3. Set port 3 into access mode with tag 20.
        4. Set port 3 up.
        5. Set port 4 into access mode with tag 20.
        6. Set port 4 up.       
            
To do that run the following commands in Switch CLI:  
 
```
    switch# configure terminal               
    switch(config)# vlan 20
    switch(config-vlan)# no shutdown        
    switch(config-vlan)# exit
    switch(config)# interface 3
    switch(config-if)# no routing
    switch(config-if)# vlan access 20        
    switch(config-if)# no shutdown
    switch(config-if)# exit
    switch(config)# interface 4
    switch(config-if)# no routing
    switch(config-if)# vlan access 20        
    switch(config-if)# no shutdown
    switch(config-if)# exit    
```

#### Host3 and Host4 configurations:

        1. Set IP address and mask on eth0 interface.  
        
To do that on Host3 run the following command:
```     
    ifconfig eth0 20.20.20.3/24 up             
```
To do that on Host4 run the following command:        
```    
    ifconfig eth0 20.20.20.4/24 up             
```

Then ping Host4 from Host3.  

#### Test pass criteria
Ping succeeded.  


## Checking_L3_connection (positive)

### Description  

This test is converse to Checking_L2_connection test.  

**Switch and hosts configurations** are the previous.

Ping Host2 from Host1.  

#### Test pass criteria
Ping succeeded.  

## Scalling_test

### Description

This test scales VLAN-subinterfaces over one L3 switch interface.  

#### Switch configuring: 

        Do next steps in the loop:  
        
            1. Create subinterface 1.x (where 1 - is number of switch interface and x - is numder of subinteface).
            2. Set VLAN Y encapsulation on 1.x (where Y - is VID). 
            3. Set IP address and mask on 1.x.
            4. Set 1.x up.
            
To do that run the following commands in Switch CLI:  
 
```
        switch# configure terminal               
        switch(config)# interface 1.x
        switch(config-subif)# encapsulation dot1Q Y        
        switch(config-subif)# ip address ip_addr/mask           
        switch(config-subif)# no shutdown
        switch(config-subif)# exit    
``` 

#### Test pass criteria
All configuration has been applied successfully.
