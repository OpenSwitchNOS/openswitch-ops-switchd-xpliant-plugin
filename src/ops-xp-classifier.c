/*
 * Copyright (C) 2016, Cavium, Inc.
 * All Rights Reserved.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License"); you may
 *   not use this file except in compliance with the License. You may obtain
 *   a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *   WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 *   License for the specific language governing permissions and limitations
 *   under the License.
 *
 * File: ops-xp-classifier.c
 *
 * Purpose: This file contains OpenSwitch classifier related
 *          application code for the Cavium/XPliant SDK.
 */

#include "ops-xp-classifier.h"
#include <unistd.h>
#include <uuid.h>
#include "openvswitch/vlog.h"
#include "ofproto/ofproto-provider.h"
#include "openXpsAclIdMgr.h"
#include "openXpsAcm.h"
#include "openXpsIacl.h"
#include "openXpsL3.h"
#include "openXpsPort.h"
#include "openXpsTcamMgr.h"
#include "openXpsTypes.h"
#include "ops-cls-asic-plugin.h"
#include "ops-xp-ofproto-provider.h"

VLOG_DEFINE_THIS_MODULE(xp_classifier);


/* hmap data structure stores classifier and its entries */
struct hmap classifier_hmap_pacl;
struct hmap classifier_hmap_racl;
struct hmap classifier_hmap_bacl;

int key_failed_pacl = 0;
int key_failed_racl = 0;
int key_failed_bacl = 0;

typedef enum 
{
    OPS_XP_IACL_KEY_TYPE_V4,
    OPS_XP_IACL_ID,
    OPS_XP_IACL_MAC_DA,
    OPS_XP_IACL_MAC_SA,
    OPS_XP_IACL_V4_ETHER_TYPE,
    OPS_XP_IACL_DIP_V4,
    OPS_XP_IACL_SIP_V4,
    OPS_XP_IACL_L4_DEST_PORT,
    OPS_XP_IACL_L4_SRC_PORT,
    OPS_XP_IACL_IVIF,
    OPS_XP_IACL_ICMP_CODE,
    OPS_XP_IACL_ICMP_MSG_TYPE,
    OPS_XP_IACL_PROTOCOL,
    OPS_XP_IACL_DSCP_ECN,
    OPS_XP_IACL_BD,
//    OPS_XP_IACL_TCP_FLAGS,
    OPS_XP_IACL_V4_MAX_FIELDS
} ops_v4_key_fields;

iacl_v4_key_t iacl_key_v4[OPS_XP_IACL_V4_MAX_FIELDS] = 
{
    { XP_IACL_KEY_TYPE_V4, 8},
    { XP_IACL_ID, 8},
    { XP_IACL_MAC_DA, 48},
    { XP_IACL_MAC_SA, 48},
    { XP_IACL_V4_ETHER_TYPE, 16},
    { XP_IACL_DIP_V4, 32},
    { XP_IACL_SIP_V4, 32},
    { XP_IACL_L4_DEST_PORT, 16},
    { XP_IACL_L4_SRC_PORT, 16},
    { XP_IACL_IVIF, 16},
    { XP_IACL_ICMP_CODE, 8},
    { XP_IACL_ICMP_MSG_TYPE, 8},
    { XP_IACL_PROTOCOL, 8},
    { XP_IACL_DSCP_ECN, 8},
    { XP_IACL_BD, 16},
  //  { XP_IACL_TCP_FLAGS, 8},
};


uint32_t iaclV4KeyByteMask[] =
{
    0x1,//keyType
    0x1,//aclId
    0x3f,//DMAC
    0x3f,//SMAC
    0x3,//EtherType
    0x3,//ctag
    0x3,//sTag
    0xf,//DIP
    0xf,//SIP
    0x3,//srcport
    0x3,//destport
    0x3,//ivif
    0x1,//icmpCode
    0x1,//icmpType
    0x1,//protocol
    0x1,//dscpEcn
    0x3,//bd
    0x1,//ttl
    0x1,//pktLen
    0x1,//tcpflag
    0x3,//vrfId
    0x1//tagfragment info
};


/*
 * reset field list to install/delete from hw
 */
void
ops_xp_alloc_reset_field_data(xpsIaclkeyFieldList_t **field_new,
                              xpsIaclData_t **iaclData_new)
{
    /* allocate memory for field list */
    xpsIaclkeyFieldList_t   *field;
    xpsIaclData_t           *iaclData;
    uint32_t                byteMask[OPS_XP_IACL_V4_MAX_FIELDS] = {0,};
    uint8_t                 field_size;
    uint8_t                 rem;

    field = malloc(sizeof(xpsIaclkeyFieldList_t));
    memset(field, 0x0, sizeof(xpsIaclkeyFieldList_t));
    field->fldList =  malloc(sizeof(xpIaclkeyField_t) * OPS_XP_IACL_V4_MAX_FIELDS);
    memset(field->fldList, 0x0, sizeof(xpIaclkeyField_t) * OPS_XP_IACL_V4_MAX_FIELDS);

    /* allocate memory for data */
    iaclData = malloc(sizeof(xpsIaclData_t));
    memset(iaclData, 0x0, sizeof(xpsIaclData_t));
       
    field->numFlds = OPS_XP_IACL_V4_MAX_FIELDS;
    field->isValid = 0x0;
    field->type = XP_IACL_V4_TYPE;
    for (int i= 0; i <OPS_XP_IACL_V4_MAX_FIELDS; i++) {

        field->fldList[i].fld.v4Fld = iacl_key_v4[i].v4_field;
        byteMask[i] = iaclV4KeyByteMask[iacl_key_v4[i].v4_field];

        field_size = 0;
        rem = 0;

        while (byteMask[i]) {
            if ((rem = (byteMask[i] & 1)) == 1) {
                ++field_size;
            }
            byteMask[i] >>= 1;
        }

        field->fldList[i].value = malloc(field_size);
        field->fldList[i].mask = malloc(field_size);
        memset(field->fldList[i].value, 0x0, field_size);
        memset(field->fldList[i].mask, 0xff, field_size);

        iacl_key_v4[i].size = field_size;
    }

    *field_new = field;
    *iaclData_new = iaclData;
}

void
ops_xp_free_field_data(xpsIaclkeyFieldList_t *field,
                       xpsIaclData_t *iaclData)
{
    VLOG_DBG("%s", __FUNCTION__);
    for (int i= 0; i < OPS_XP_IACL_V4_MAX_FIELDS; i++) {
        free(field->fldList[i].value);
        free(field->fldList[i].mask);
    }
    free(field->fldList);
    free(field);
    free(iaclData);
}

int
ops_xp_acl_table_init(xpsDevice_t devId)
{
    xpIaclTableProfile_t tableProfile;

    xpsIaclkeyFieldList_t   *fields_pacl, *fields_bacl, *fields_racl;
    xpsIaclData_t           *iaclData_pacl, *iaclData_bacl, *iaclData_racl;
    XP_STATUS               status = XP_NO_ERR;

    VLOG_DBG("%s", __FUNCTION__);

    VLOG_DBG("Initializing ACL tables %s", __FUNCTION__);


    /* Populate table profiles data */
    tableProfile.numTables = XP_IACL_TOTAL_TYPE;

    /* Populate PACL config */
    tableProfile.tableProfile[XP_ACL_IACL0].tblType = XP_ACL_IACL0; 
    tableProfile.tableProfile[XP_ACL_IACL0].keySize = 390; 
    tableProfile.tableProfile[XP_ACL_IACL0].numDb = 1; 

    /* Populate BACL config */
    tableProfile.tableProfile[XP_ACL_IACL1].tblType = XP_ACL_IACL1; 
    tableProfile.tableProfile[XP_ACL_IACL1].keySize = 390; 
    tableProfile.tableProfile[XP_ACL_IACL1].numDb = 1;

    /* Populate RACL config */
    tableProfile.tableProfile[XP_ACL_IACL2].tblType = XP_ACL_IACL2; 
    tableProfile.tableProfile[XP_ACL_IACL2].keySize = 390; 
    tableProfile.tableProfile[XP_ACL_IACL2].numDb = 1; 

    xpsIaclCreateTable(devId, tableProfile);

    /*define keys for IACL tables*/
    ops_xp_alloc_reset_field_data(&fields_pacl, &iaclData_pacl);
    fields_pacl->isValid = 0x1;
    status = xpsIaclDefinePaclKey(devId, XP_IACL_V4_TYPE, fields_pacl);
    
    if (status != XP_NO_ERR) {
        key_failed_pacl = 1;
    }
    ops_xp_free_field_data(fields_pacl, iaclData_pacl);

    ops_xp_alloc_reset_field_data(&fields_bacl, &iaclData_bacl);
    fields_bacl->isValid = 0x1;
    xpsIaclDefineBaclKey(devId, XP_IACL_V4_TYPE, fields_bacl);
    ops_xp_free_field_data(fields_bacl, iaclData_bacl);

    ops_xp_alloc_reset_field_data(&fields_racl, &iaclData_racl);
    fields_racl->isValid = 0x1;
    status = xpsIaclDefineRaclKey(devId, XP_IACL_V4_TYPE, fields_racl);

    if (status != XP_NO_ERR) {
        key_failed_pacl = 1;
    }
    ops_xp_free_field_data(fields_racl, iaclData_racl);

    VLOG_DBG("Init completed ACL %s", __FUNCTION__);
}

int
ops_xp_cls_init(xpsDevice_t dev)
{
    xpsDevice_t devId; 
    XP_STATUS   status;

    devId = dev;

    VLOG_DBG("%s", __FUNCTION__);

    /* Classifier related initializations */
    /* Creating and configuring TCAM Manager tables for IACLs and EACL */
    VLOG_DBG("Initializing TCAM Manager and hmap %s", __FUNCTION__);
    status = XP_NO_ERR;

    /* Port IACL */
    status = xpsTcamMgrAddTable(devId, XP_ACL_IACL0, XPS_TCAM_LIST_ALGORITHM);
    if (status != XP_NO_ERR) {
        VLOG_INFO("xpsTcamMgrAddTable failed with error code %d, "
                  "for table type  %s", status, XP_ACL_IACL0);
    }
    
    status = xpsTcamMgrConfigTable(devId, XP_ACL_IACL0, &xpsTcamMgrRuleMoveAcl, 512, 16);
    if (status != XP_NO_ERR) {
        VLOG_INFO("xpsTcamMgrConfigTable failed with error code %d, "
                  "for table type  %s", status, XP_ACL_IACL0);
    }

    /* Bridge IACL */
    status = xpsTcamMgrAddTable(devId, XP_ACL_IACL1, XPS_TCAM_LIST_ALGORITHM);
    if (status != XP_NO_ERR) {
        VLOG_INFO("xpsTcamMgrAddTable failed with error code %d, "
                  "for table type  %s", status, XP_ACL_IACL1);
    }

    status = xpsTcamMgrConfigTable(devId, XP_ACL_IACL1, &xpsTcamMgrRuleMoveAcl, 512, 16);
    if (status != XP_NO_ERR) {
        VLOG_INFO("xpsTcamMgrConfigTable failed with error code %d, "
                  "for table type  %s", status, XP_ACL_IACL1);
    }

    /* Router IACL */
    status = xpsTcamMgrAddTable(devId, XP_ACL_IACL2, XPS_TCAM_LIST_ALGORITHM);
    if (status != XP_NO_ERR) {
        VLOG_INFO("xpsTcamMgrAddTable failed with error code %d, "
                  "for table type  %s", status, XP_ACL_IACL2);
    }

    status = xpsTcamMgrConfigTable(devId, XP_ACL_IACL2, &xpsTcamMgrRuleMoveAcl, 512, 16);
    if (status != XP_NO_ERR) {
        VLOG_INFO("xpsTcamMgrConfigTable failed with error code %d, "
                  "for table type  %s", status, XP_ACL_IACL2);
    }

    /* EACL */
    status = xpsTcamMgrAddTable(devId, XP_ACL_EACL,  XPS_TCAM_LIST_ALGORITHM);
    if (status != XP_NO_ERR) {
        VLOG_INFO("xpsTcamMgrAddTable failed with error code %d, "
                  "for table type  %s", status, XP_ACL_EACL);
    }

    status = xpsTcamMgrConfigTable(devId, XP_ACL_EACL, &xpsTcamMgrRuleMoveAcl, 512, 16);
    if (status != XP_NO_ERR) {
        VLOG_INFO("xpsTcamMgrConfigTable failed with error code %d, "
                  "for table type  %s", status, XP_ACL_EACL);
    }

    /* Init IACL */
    ops_xp_acl_table_init(devId);

    /* Classifier hmap initialization */
    hmap_init(&classifier_hmap_pacl);
    hmap_init(&classifier_hmap_bacl);
    hmap_init(&classifier_hmap_racl);
    VLOG_DBG("Init complete TCAM Manager and hmap %s", __FUNCTION__);
}

/*
 * Get type of ACL from the interface info and direction
 * and eturn it to tableType
 */
xpAclType_e
ops_xp_cls_get_type(struct ops_cls_interface_info *interface_info,
                    enum ops_cls_direction direction)
{
    xpAclType_e tableType;

    VLOG_DBG("%s", __FUNCTION__);

    /* Decide type ACL */
    if (direction == OPS_CLS_DIRECTION_IN) {

        switch (interface_info->interface) {
        case OPS_CLS_INTERFACE_PORT:
            tableType =  XP_ACL_IACL0;
            break;

        case OPS_CLS_INTERFACE_VLAN:
            if (interface_info->flags & OPS_CLS_INTERFACE_L3ONLY) {
                tableType = XP_ACL_IACL2;
            } else {
                tableType = XP_ACL_IACL1;
            }
            break;

        default:
            tableType = XP_ACML_TOTAL_TYPE;
        }

    } else if (direction == OPS_CLS_DIRECTION_OUT) {
        tableType = XP_ACL_EACL;
    } else {
        tableType = XP_ACML_TOTAL_TYPE;
    }

    return tableType;
}

/*
 * Search for the classifier in hmap by interface info and uid
 */
struct xp_acl_entry*
ops_xp_cls_hmap_lookup(const struct uuid *cls_uid, 
                       struct ops_cls_interface_info *intf_info,
                       enum ops_cls_direction cls_direction)
{
    uint32_t                hash_id;
    struct xp_acl_entry     *classifier = NULL;
    xpAclType_e             acl_type;

    /* Get hash id from uid */
    hash_id = uuid_hash(cls_uid);

    VLOG_DBG("%s", __FUNCTION__);

    acl_type = ops_xp_cls_get_type(intf_info, cls_direction);

    /* Search hmap for the hashid matching entries */

    if (acl_type == XP_ACL_IACL0) {
        /* Search in PACL */
        HMAP_FOR_EACH_WITH_HASH(classifier, hnode, hash_id, &classifier_hmap_pacl) {
            if (uuid_equals(&classifier->list_uid, cls_uid)) {
                return classifier;
            }
        }
    } else if (acl_type == XP_ACL_IACL1) {
        /* Search in BACL */
        HMAP_FOR_EACH_WITH_HASH(classifier, hnode, hash_id, &classifier_hmap_bacl) {
            if (uuid_equals(&classifier->list_uid, cls_uid)) {
                return classifier;
            }
        }
    } else if(acl_type == XP_ACL_IACL2) {
        /* Search in RACL */
        HMAP_FOR_EACH_WITH_HASH(classifier, hnode, hash_id, &classifier_hmap_racl) {
            if (uuid_equals(&classifier->list_uid, cls_uid)) {
                return classifier;
            }
        }
    }
    return NULL;
}

/*
 * Search for the classifier from specific hmap provided in arguments
 */
struct xp_acl_entry*
ops_xp_cls_lookup_from_hmap_type(const struct uuid *cls_uid, 
                       struct hmap *classifier_hmap)

{
    uint32_t                hash_id;
    struct xp_acl_entry     *classifier = NULL;

    /* Get hash id from uid */
    hash_id = uuid_hash(cls_uid);

    VLOG_DBG("%s", __FUNCTION__);

    HMAP_FOR_EACH_WITH_HASH(classifier, hnode, hash_id, classifier_hmap) {
        if (uuid_equals(&classifier->list_uid, cls_uid)) {
            return classifier;
        }
    }
    return NULL;
}

/*
 * API to reverse byte ordering to resolve Little Endian and Big Endian issues
 */
uint32_t
ops_xp_cls_reverse_byte_order(uint32_t src)
{
    uint32_t dst;
    dst = ((src >> 24) & 0xff) |        // byte 3 to byte 0
          ((src << 8) & 0xff0000) |     // byte 1 to byte 2
          ((src >> 8) & 0xff00) |       // byte 2 to byte 1
          ((src << 24) & 0xff000000);   // byte 0 to byte 3
    return dst;
}

/*
 * populate Iacl Entries
 */
void
ops_xp_cls_populate_iacl_entires(struct ops_cls_list_entry *entry,
                                 xpsIaclkeyFieldList_t *field,
                                 xpsIaclData_t *iaclData)
{
    uint32_t sip_mask, dip_mask, sip_mask_in, dip_mask_in, sip, dip;

    /* Populate key */
    VLOG_DBG("%s", __FUNCTION__);

    /* Poupulate SRC IP and DST IP */
    if (entry->entry_fields.entry_flags & OPS_CLS_SRC_IPADDR_VALID) {

        field->fldList[OPS_XP_IACL_SIP_V4].fld.v4Fld = XP_IACL_SIP_V4;

        memcpy(&sip, &entry->entry_fields.src_ip_address.v4, sizeof(uint32_t));

        sip = ops_xp_cls_reverse_byte_order(sip);

        memcpy(field->fldList[OPS_XP_IACL_SIP_V4].value,
               &sip, iacl_key_v4[OPS_XP_IACL_SIP_V4].size);

        VLOG_DBG("SIP %#x", sip);

        memcpy(&sip_mask_in, &entry->entry_fields.src_ip_address_mask.v4,
               iacl_key_v4[OPS_XP_IACL_SIP_V4].size);

        sip_mask_in = ops_xp_cls_reverse_byte_order(sip_mask_in);

        /* Set all 32 bits to 1 and do xor with it to toggle */
        sip_mask = 0xffffffff;

        /* Toggle the mask as per xdk requirement
         * we mask the matching fields with zero in xdk
         */
        sip_mask = sip_mask ^ sip_mask_in;

        memcpy(field->fldList[OPS_XP_IACL_SIP_V4].mask, 
               &sip_mask, iacl_key_v4[OPS_XP_IACL_SIP_V4].size);
    }

    if (entry->entry_fields.entry_flags & OPS_CLS_DEST_IPADDR_VALID) {

        field->fldList[OPS_XP_IACL_DIP_V4].fld.v4Fld = XP_IACL_DIP_V4;

        memcpy(&dip, &entry->entry_fields.dst_ip_address.v4, sizeof(uint32_t));

        dip = ops_xp_cls_reverse_byte_order(dip);

        memcpy(field->fldList[OPS_XP_IACL_DIP_V4].value, &dip,
               iacl_key_v4[OPS_XP_IACL_DIP_V4].size);

        VLOG_DBG("DIP %#x", dip);

        memcpy(&dip_mask_in, &entry->entry_fields.dst_ip_address_mask.v4,
               iacl_key_v4[OPS_XP_IACL_DIP_V4].size);

        dip_mask_in = ops_xp_cls_reverse_byte_order(dip_mask_in);

        /* Set all 32 bits to 1 and do xor with it to toggle */
        dip_mask = 0xffffffff;

        /* Toggle the mask as per xdk requirement
         * we mask the matching fields with zero in xdk
         */
        dip_mask = dip_mask ^ dip_mask_in;

        memcpy(field->fldList[OPS_XP_IACL_DIP_V4].mask, 
               &dip_mask, iacl_key_v4[OPS_XP_IACL_DIP_V4].size);
        }

    /* Populate L4 SRC DST PORT */

    if (entry->entry_fields.entry_flags & OPS_CLS_L4_SRC_PORT_VALID) {

        field->fldList[OPS_XP_IACL_L4_SRC_PORT].fld.v4Fld = XP_IACL_L4_SRC_PORT;

        memcpy(field->fldList[OPS_XP_IACL_L4_SRC_PORT].value, 
               &entry->entry_fields.L4_src_port_min,
               iacl_key_v4[OPS_XP_IACL_L4_SRC_PORT].size);

        memset(field->fldList[OPS_XP_IACL_L4_SRC_PORT].mask, 0x0,
               iacl_key_v4[OPS_XP_IACL_L4_SRC_PORT].size);
    }

    if (entry->entry_fields.entry_flags & OPS_CLS_L4_DEST_PORT_VALID) {

        field->fldList[OPS_XP_IACL_L4_DEST_PORT].fld.v4Fld = XP_IACL_L4_DEST_PORT;

        memcpy(field->fldList[OPS_XP_IACL_L4_DEST_PORT].value, 
               &entry->entry_fields.L4_dst_port_min,
               iacl_key_v4[OPS_XP_IACL_L4_DEST_PORT].size);

        memset(field->fldList[OPS_XP_IACL_L4_DEST_PORT].mask, 0x0,
               iacl_key_v4[OPS_XP_IACL_L4_DEST_PORT].size);
    }

    if (entry->entry_fields.entry_flags & OPS_CLS_PROTOCOL_VALID) {

        field->fldList[OPS_XP_IACL_PROTOCOL].fld.v4Fld = XP_IACL_PROTOCOL;

        memcpy(field->fldList[OPS_XP_IACL_PROTOCOL].value, 
               &entry->entry_fields.protocol,
               iacl_key_v4[OPS_XP_IACL_PROTOCOL].size);

        memset(field->fldList[OPS_XP_IACL_PROTOCOL].mask, 0x0,
               iacl_key_v4[OPS_XP_IACL_PROTOCOL].size);
    }

    if (entry->entry_fields.entry_flags & OPS_CLS_TOS_VALID) {

        field->fldList[OPS_XP_IACL_DSCP_ECN].fld.v4Fld = XP_IACL_DSCP_ECN;

        memcpy(field->fldList[OPS_XP_IACL_DSCP_ECN].value, 
               &entry->entry_fields.tos,
               iacl_key_v4[OPS_XP_IACL_DSCP_ECN].size);

        memcpy(field->fldList[OPS_XP_IACL_DSCP_ECN].mask, 
               &entry->entry_fields.tos_mask,
               iacl_key_v4[OPS_XP_IACL_DSCP_ECN].size);
    }

   /* if(entry->entry_fields.entry_flags & OPS_CLS_TCP_FLAGS_VALID) {

        field->fldList[OPS_XP_IACL_TCP_FLAGS].fld.v4Fld = XP_IACL_TCP_FLAGS;

        memcpy(field->fldList[OPS_XP_IACL_TCP_FLAGS].value, 
               &entry->entry_fields.tcp_flags, sizeof(uint8_t));

        memcpy(field->fldList[OPS_XP_IACL_TCP_FLAGS].mask, 
               &entry->entry_fields.tcp_flags_mask, sizeof(uint8_t));
    }*/

    if (entry->entry_fields.entry_flags & OPS_CLS_ICMP_CODE_VALID) {

        field->fldList[OPS_XP_IACL_ICMP_CODE].fld.v4Fld = XP_IACL_ICMP_CODE;

        memcpy(field->fldList[OPS_XP_IACL_ICMP_CODE].value, 
               &entry->entry_fields.icmp_code,
               iacl_key_v4[OPS_XP_IACL_ICMP_CODE].size);

        memset(field->fldList[OPS_XP_IACL_ICMP_CODE].mask, 0x0,
               iacl_key_v4[OPS_XP_IACL_ICMP_CODE].size);
    }

    if (entry->entry_fields.entry_flags & OPS_CLS_ICMP_TYPE_VALID) {

        field->fldList[OPS_XP_IACL_ICMP_MSG_TYPE].fld.v4Fld = XP_IACL_ICMP_MSG_TYPE;

        memcpy(field->fldList[OPS_XP_IACL_ICMP_MSG_TYPE].value, 
               &entry->entry_fields.icmp_type,
               iacl_key_v4[OPS_XP_IACL_ICMP_MSG_TYPE].size);

        memset(field->fldList[OPS_XP_IACL_ICMP_MSG_TYPE].mask, 0x0,
               iacl_key_v4[OPS_XP_IACL_ICMP_MSG_TYPE].size);
    }

    if (entry->entry_fields.entry_flags & OPS_CLS_VLAN_VALID) {

        field->fldList[OPS_XP_IACL_BD].fld.v4Fld = XP_IACL_BD;

        memcpy(field->fldList[OPS_XP_IACL_BD].value, 
               &entry->entry_fields.vlan, iacl_key_v4[OPS_XP_IACL_BD].size);

        memset(field->fldList[OPS_XP_IACL_BD].mask, 0x0,
               iacl_key_v4[OPS_XP_IACL_BD].size);
    }

    /* Populate SRC MAC and DST MAC */

    if (entry->entry_fields.entry_flags & OPS_CLS_L2_ETHERTYPE_VALID) {

        field->fldList[OPS_XP_IACL_V4_ETHER_TYPE].fld.v4Fld = XP_IACL_V4_ETHER_TYPE;

        memcpy(field->fldList[OPS_XP_IACL_V4_ETHER_TYPE].value, 
               &entry->entry_fields.L2_ethertype,
               iacl_key_v4[OPS_XP_IACL_V4_ETHER_TYPE].size);

        memset(field->fldList[OPS_XP_IACL_V4_ETHER_TYPE].mask, 0x0,
               iacl_key_v4[OPS_XP_IACL_V4_ETHER_TYPE].size);
    }

    /* Check L2_cos is not supported */

    /* Populate data */
    iaclData->isTerminal = 1;
    iaclData->enPktCmdUpd = 1;
    if (entry->entry_actions.action_flags & OPS_CLS_ACTION_PERMIT) {
        iaclData->pktCmd = XP_PKTCMD_FWD;
    } else if (entry->entry_actions.action_flags & OPS_CLS_ACTION_DENY) {
        iaclData->pktCmd = XP_PKTCMD_DROP;
    }
}

/*
 * create rule id list for a classifier
 */
struct ops_cls_pd_status *
ops_xp_cls_create_rule_entry_list(struct xp_acl_entry *classifier,
                                  struct ops_cls_list *cls_list,
                                  struct ovs_list *list)
{
    uint32_t                 tableId;
    uint32_t                 tcamId;
    xpAclType_e              tableType;
    xpDevice_t               devId;
    XP_STATUS                status;
    struct ops_cls_pd_status *ops_status;
    xpsIaclData_t            *iaclData;
    xpsIaclkeyFieldList_t    *field;

    devId = 0;

    VLOG_DBG("%s", __FUNCTION__);

    /* Get type ACL */
    tableType = ops_xp_cls_get_type(&classifier->intf_info, classifier->cls_direction);

    /* Get tableId from tableType */

    tableId = tableType;

    /* For each entry in the classifier entries list, create a local copy and
     * store its link in local list
     */

    classifier->num_rules = cls_list->num_entries;

    VLOG_DBG("Number of entries needs to be programmed %d", cls_list->num_entries);

    for (int i = 0; i < cls_list->num_entries; i++) {
        struct xp_acl_rule  *entry;
        uint32_t            rule_index;
        uint32_t            priority;

        entry = malloc(sizeof(struct xp_acl_rule));
        priority = cls_list->num_entries - i;

        /* Get tcam index from tcam manager and add it to the rule list. */
        status = xpsTcamMgrAllocEntry(devId, tableId, priority, &rule_index);

        entry->rule_id = rule_index;
        entry->counter_en = 0;
        
        /* Update count enable filed */
        if (cls_list->entries[i].entry_actions.action_flags & OPS_CLS_ACTION_COUNT) {
            entry->counter_en = 1;
            entry->count = 0;
        }

        list_push_back(list, &entry->list_node);

        /* Get hw tcamId from rule entry index */
        status = xpsTcamMgrTcamIdFromEntryGet(devId, tableId, rule_index, &tcamId);
        VLOG_DBG("allocated tcam RuleId %d, HW tcam Index %d", rule_index, tcamId);

        /* Populate entry in filed list to program in hw */
        ops_xp_alloc_reset_field_data(&field, &iaclData);
        field->numFlds = OPS_XP_IACL_V4_MAX_FIELDS;
        field->isValid = 0x1;
        field->type = XP_IACL_V4_TYPE;
        ops_xp_cls_populate_iacl_entires(&cls_list->entries[i], field, iaclData);

        /* Update ACL ID to fields list */
        field->fldList[OPS_XP_IACL_ID].fld.v4Fld = XP_IACL_ID;

        memset(field->fldList[OPS_XP_IACL_ID].value, classifier->acl_id, sizeof(uint8_t));
        memset(field->fldList[OPS_XP_IACL_ID].mask, 0x0, sizeof(uint8_t));

        VLOG_DBG("Installing in HW table %d, HWID: %d", tableId, tcamId);

        switch (tableId) {

        case XP_ACL_IACL0:
            status =  xpsIaclWritePaclKey(devId, tcamId, field);
            if (status != XP_NO_ERR) {
                VLOG_DBG("xpsIaclWritePaclKey failed with error %d", status);
            }

            status = xpsIaclWritePaclData(devId, tcamId, iaclData);
            if (status != XP_NO_ERR) {
                VLOG_DBG("xpsIaclWritePaclData failed with error %d", status);
            }
            break;

        case XP_ACL_IACL1:
            status = xpsIaclWriteBaclKey(devId, tcamId, field);
            if (status != XP_NO_ERR) {
                VLOG_DBG("xpsIaclWriteBaclKey failed with error %d", status);
            }

            status = xpsIaclWriteBaclData(devId, tcamId, iaclData);
            if (status != XP_NO_ERR) {
                VLOG_DBG("xpsIaclWriteBaclData failed with error %d", status);
            }
            break;

        case XP_ACL_IACL2:
            status = xpsIaclWriteRaclKey(devId, tcamId, field);
            if (status != XP_NO_ERR) {
                VLOG_DBG("xpsIaclRritePaclKey failed with error %d", status);
            }

            xpsIaclWriteRaclData(devId, tcamId, iaclData);
            if (status != XP_NO_ERR) {
                VLOG_DBG("xpsIaclRritePaclData failed with error %d", status);
            }
            break;

        default:
            break;
        }

        /* memset field and data to zero and release memory allocated
         * by populate entries
         */
        ops_xp_free_field_data(field, iaclData);
    }
}


/*
 * Delete ruleid list for a classifier
 */
void
ops_xp_cls_destroy_rule_entry_list(struct xp_acl_entry *classifier)
{
    uint32_t                tableId;
    uint32_t                tcamId;
    xpAclType_e             tableType;
    struct xp_acl_rule      *entry, *next_entry;
    struct ovs_list         *list;
    xpDevice_t              devId;
    xpsIaclData_t           *iaclData;
    xpsIaclkeyFieldList_t   *field;
    XP_STATUS               status;

    VLOG_DBG("%s", __FUNCTION__);

    list = &classifier->rule_list;
    devId = 0;

    /* Get type ACL */
    tableType = ops_xp_cls_get_type(&classifier->intf_info, classifier->cls_direction);

    /* Get tableId from tableType */
    tableId = tableType;

    LIST_FOR_EACH_SAFE (entry, next_entry, list_node, list) {

        /* Get hw tcamId from rule entry index */
        status = xpsTcamMgrTcamIdFromEntryGet(devId, tableId, entry->rule_id, &tcamId);

        ops_xp_alloc_reset_field_data(&field, &iaclData);

        switch (tableId) {
        case XP_ACL_IACL0:
            xpsIaclWritePaclKey(devId, tcamId, field);
            xpsIaclWritePaclData(devId, tcamId, iaclData);
            break;

        case XP_ACL_IACL1:
            xpsIaclWriteBaclKey(devId, tcamId, field);
            xpsIaclWriteBaclData(devId, tcamId, iaclData);
            break;

        case XP_ACL_IACL2:
            xpsIaclWriteRaclKey(devId, tcamId, field);
            xpsIaclWriteRaclData(devId, tcamId, iaclData);
            break;

        default:
            break;
        }

        /* Free entry from tcam manager */
        xpsTcamMgrFreeEntry(devId, tableId, entry->rule_id);
        list_remove(&entry->list_node);
        free(entry);

    }
}


/*
 * Add the acl rules meta data to HMAP
 */
struct ops_cls_pd_status *
ops_xp_cls_acl_add(struct ops_cls_list *cls,
                   struct xp_acl_entry* acl_entry)
{
    uint32_t                    hash_index;
    uint8_t                     acl_id;
    struct ops_cls_pd_status    *status; 
    xpAclType_e                 acl_type;
    xpsDevice_t                 devId;
    uint8_t                     allocated=0;
    uint32_t                    hash_id;
    struct xp_acl_entry         *classifier;

    VLOG_DBG("%s", __FUNCTION__);

    if (cls == NULL) {
        return NULL;
    }

    /* Set status to default */
    status = malloc(sizeof(struct ops_cls_pd_status));
    if (!status) {
        return NULL;
    }

    status->status_code = OPS_CLS_STATUS_SUCCESS;
    status->entry_id = 0;

    /* Populate acl entry to store it in HMAP */
    acl_entry->list_uid = cls->list_id;

    /* Allocate aclid and maintain it in acl_id this aclid
     * should not be reallocated if already allocated to
     * PACL or RACL or BACL for same UUID then reuse it
     */
    devId = 0;

    /* Get hash id from uid */
    hash_id = uuid_hash(&cls->list_id);

    /* Search in PACL */
    HMAP_FOR_EACH_WITH_HASH(classifier, hnode, hash_id, &classifier_hmap_pacl) {
        if (uuid_equals(&classifier->list_uid, &cls->list_id)) {
            allocated =1;
            acl_id = classifier->acl_id;
            VLOG_DBG("Allocated ACLID %d in PACL HMAP", acl_id);
        }
    }

    /* Search in BACL*/
    HMAP_FOR_EACH_WITH_HASH(classifier, hnode, hash_id, &classifier_hmap_bacl) {
        if (uuid_equals(&classifier->list_uid, &cls->list_id)) {
            allocated = 1;
            acl_id = classifier->acl_id;
            VLOG_DBG("Allocated ACLID %d in BACL HMAP", acl_id);
        }
    }

    /* Search in RACL */
    HMAP_FOR_EACH_WITH_HASH(classifier, hnode, hash_id, &classifier_hmap_racl) {
        if (uuid_equals(&classifier->list_uid, &cls->list_id)) {
            allocated = 1;
            acl_id = classifier->acl_id;
            VLOG_DBG("Allocated ACLID %d in RACL HMAP", acl_id);
        }
    }

    /* From above if allocated is set means we already allocated ACLID reuseit
     * else allocate it for this UUID
     */
    if (allocated) {
        acl_entry->acl_id = acl_id;
    } else {
        if (xpsAclIdAllocEntry(devId, &acl_id) != XP_NO_ERR) {
            status->status_code = OPS_CLS_STATUS_HW_RESOURCE_ERR;
        }
        acl_entry->acl_id = acl_id;
    }

    VLOG_DBG("Allocated ACLID %d", acl_id);

    list_init(&acl_entry->rule_list);

    if (cls->entries != NULL) {
        VLOG_DBG("creating list for rules and interfaces for classifier %s",
                 cls->list_name);

        ops_xp_cls_create_rule_entry_list(acl_entry, cls, &acl_entry->rule_list);
    }

    VLOG_DBG("Adding metadata of classifier %s to HMAP", cls->list_name);

    /* Get hash index from UUID */
    hash_index = uuid_hash(&cls->list_id);

    acl_type = ops_xp_cls_get_type(&acl_entry->intf_info, acl_entry->cls_direction);
    if (acl_type == XP_ACL_IACL0) {

        hmap_insert(&classifier_hmap_pacl, &acl_entry->hnode, hash_index);

    } else if (acl_type == XP_ACL_IACL1) {

        hmap_insert(&classifier_hmap_bacl, &acl_entry->hnode, hash_index);

    } else if (acl_type == XP_ACL_IACL2) {

        hmap_insert(&classifier_hmap_racl, &acl_entry->hnode, hash_index);

    }

    return status;
}

void
ops_xp_cls_acl_delete(struct xp_acl_entry *acl_entry, 
                      char *list_name,
                      struct hmap *classifier_hmap)
{
    uint8_t             allocated=0;
    uint8_t             acl_id;
    struct xp_acl_entry *classifier;
    uint32_t            hash_id;
    struct uuid         list_uid;
    xpsDevice_t         devId=0;

    VLOG_DBG("%s", __FUNCTION__);
    if (acl_entry == NULL) {
        return;
    }

    /* Destroy rule and interface list */
    VLOG_DBG("deleting rules and interface list for classifier %s", list_name);

    ops_xp_cls_destroy_rule_entry_list(acl_entry);

    /* Keep it for allocator reference */
    acl_id = acl_entry->acl_id;
    list_uid = acl_entry->list_uid;

    VLOG_DBG("Deleting HMAP metadata for classifier %s", list_name);
    hmap_remove(classifier_hmap, &acl_entry->hnode);

    /* Deallocate memory */
    free(acl_entry);

    /* Deleting ACLID allocation if not being used by any of the tables */

    /* Get hash id from uid */
    hash_id = uuid_hash(&list_uid);

    /* Search in PACL */
    HMAP_FOR_EACH_WITH_HASH(classifier, hnode, hash_id, &classifier_hmap_pacl) {
        if (uuid_equals(&classifier->list_uid, &list_uid)) {
            if (acl_id == classifier->acl_id) {
                allocated = 1;
                VLOG_DBG("Allocated ACLID %d in PACL HMAP, hence not deleting",
                         acl_id);
            }
        }
    }

    /* Search in BACL*/
    HMAP_FOR_EACH_WITH_HASH(classifier, hnode, hash_id, &classifier_hmap_bacl) {
        if (uuid_equals(&classifier->list_uid, &list_uid)) {
            if (acl_id == classifier->acl_id) {
                allocated = 1;
                VLOG_DBG("Allocated ACLID %d in BACL HMAP, hence not deleting",
                         acl_id);
            }
        }
    }

    /* Search in RACL */
    HMAP_FOR_EACH_WITH_HASH(classifier, hnode, hash_id, &classifier_hmap_racl) {
        if (uuid_equals(&classifier->list_uid, &list_uid)) {
            if (acl_id == classifier->acl_id) {
                allocated = 1;
                VLOG_DBG("Allocated ACLID %d in RACL HMAP, hence not deleting",
                         acl_id);
            }
        }
    }

    if (!allocated) {
        xpsAclIdFreeEntry(devId, acl_id);
    }

}

void
ops_xp_cls_validate_entries(struct ops_cls_list *list,
                            struct ops_cls_pd_status **pd_status)
{
    for (int i = 0; i < list->num_entries; i++) {
        struct ops_cls_list_entry *entry;

        entry = &list->entries[i];
        if ((entry->entry_fields.L4_src_port_op == OPS_CLS_L4_PORT_OP_NEQ) ||
            (entry->entry_fields.L4_src_port_op == OPS_CLS_L4_PORT_OP_LT) ||
            (entry->entry_fields.L4_src_port_op == OPS_CLS_L4_PORT_OP_GT) ||
            (entry->entry_fields.L4_src_port_op == OPS_CLS_L4_PORT_OP_RANGE)) {

            (*pd_status)->status_code = OPS_CLS_STATUS_HW_UNSUPPORTED_ERR;
            (*pd_status)->entry_id = i;
        }

        if ((entry->entry_fields.L4_dst_port_op == OPS_CLS_L4_PORT_OP_NEQ) ||
            (entry->entry_fields.L4_dst_port_op  == OPS_CLS_L4_PORT_OP_LT) ||
            (entry->entry_fields.L4_dst_port_op  == OPS_CLS_L4_PORT_OP_GT) ||
            (entry->entry_fields.L4_dst_port_op  == OPS_CLS_L4_PORT_OP_RANGE)) {

            (*pd_status)->status_code = OPS_CLS_STATUS_HW_UNSUPPORTED_ERR;
            (*pd_status)->entry_id = i;
        }
    }
}

int
ops_xp_cls_apply(struct ops_cls_list *list, struct ofproto *ofproto,
                 void *aux, struct ops_cls_interface_info *interface_info,
                 enum ops_cls_direction direction,
                 struct ops_cls_pd_status *pd_status)
{
    struct xp_acl_entry         *acl_entry;
    struct ofproto_xpliant      *ofproto_xp;
    struct bundle_xpliant       *bundle;
    struct xp_acl_rule          *entry, *next_entry;
    struct ovs_list             *ovslist;
    struct ofport_xpliant       *port = NULL;
    struct ofport_xpliant       *next_port = NULL;
    xpsInterfaceId_t            intf;
    uint32_t                    tcamId;

    VLOG_DBG("%s", __FUNCTION__);

    ofproto_xp = ops_xp_ofproto_cast(ofproto);
    bundle = bundle_lookup(ofproto_xp, aux);
    if ( (bundle == NULL) ||
         ((bundle->l3_intf == NULL) &&
         (interface_info->flags & OPS_CLS_INTERFACE_L3ONLY)) ) {
        VLOG_ERR("Failed to get port bundle/l3_intf not configured");
        return EPERM;
    }

    /* Validate unsupported parameters */
    ops_xp_cls_validate_entries(list, &pd_status);

    if (pd_status->status_code == OPS_CLS_STATUS_HW_UNSUPPORTED_ERR) {
        return 0;
    }

    if (interface_info->interface == OPS_CLS_INTERFACE_PORT) {
        VLOG_DBG("Interface value %d", bundle->intfId);
    } else if(interface_info->interface == OPS_CLS_INTERFACE_VLAN) {
        if (interface_info->flags & OPS_CLS_INTERFACE_L3ONLY) {
            VLOG_DBG("L3 interface value %d", bundle->l3_intf->l3_intf_id);
        }
    }

    VLOG_DBG("key_failed_pacl = %d     key_failed_racl = %d",
             key_failed_pacl, key_failed_racl);

    acl_entry = ops_xp_cls_hmap_lookup(&list->list_id, interface_info, direction);
    if (!acl_entry) {
        acl_entry = malloc(sizeof(struct xp_acl_entry));
        memset(acl_entry, 0x0, sizeof(struct xp_acl_entry));
        if (!acl_entry) {
            VLOG_ERR("%s: Could not allocate memory for the uuid %u, "
                     "to store it in HMAP", __FUNCTION__, list->list_id);

            pd_status->status_code = OPS_CLS_STATUS_HW_RESOURCE_ERR;
            return 0;
        }

        memcpy(&acl_entry->intf_info, interface_info,
               sizeof(struct ops_cls_interface_info));
        acl_entry->cls_direction = direction;

        pd_status = ops_xp_cls_acl_add(list, acl_entry);

    } else {
        int i = 0;

        VLOG_DBG("acl_id %d exists in HMAP for this UUID %d",
                 acl_entry->acl_id, list->list_id);

        ovslist = &acl_entry->rule_list;

        /* Print TCAM manager rule ids for debugging purpose */
        LIST_FOR_EACH_SAFE (entry, next_entry, list_node, ovslist) {
            xpsTcamMgrTcamIdFromEntryGet(0, 0, entry->rule_id, &tcamId);
            VLOG_DBG("rule entry %d, tcam manger rule id :%d, HWTCAM id :%d ",
                     i, entry->rule_id, tcamId);
            i++;
        }
    }

    /* Update ingress ACLID for port and enable ACL on port */
    if (interface_info->interface == OPS_CLS_INTERFACE_VLAN) {
        if (interface_info->flags & OPS_CLS_INTERFACE_L3ONLY) {
            xpsL3SetRouterAclId(ofproto_xp->xpdev->id, 
                                bundle->l3_intf->l3_intf_id, acl_entry->acl_id);
            xpsL3SetRouterAclEnable(ofproto_xp->xpdev->id,
                                    bundle->l3_intf->l3_intf_id, 0x1);
        }
    } else if (interface_info->interface == OPS_CLS_INTERFACE_PORT) {

        LIST_FOR_EACH_SAFE (port, next_port, bundle_node, &bundle->ports) {
            intf = ops_xp_get_ofport_intf_id(port);
            xpsPortSetField(ofproto_xp->xpdev->id, 
                            intf, XPS_PORT_ACL_EN, 1);
            xpsPortSetField(ofproto_xp->xpdev->id, 
                            intf, XPS_PORT_ACL_ID, acl_entry->acl_id);
            VLOG_DBG("Interface value on which acl %d, applied is%d",
                     acl_entry->acl_id, intf);
        }
    }

    /* Keep track of acl enabled interface count */
    acl_entry->num_intfs++;
    return 0;
}

int
ops_xp_cls_remove(const struct uuid *list_id, const char *list_name,
                  enum ops_cls_type list_type, struct ofproto *ofproto,
                  void *aux, struct ops_cls_interface_info *interface_info,
                  enum ops_cls_direction direction,
                  struct ops_cls_pd_status *pd_status)
{
    struct xp_acl_entry     *acl_entry;
    struct ofproto_xpliant  *ofproto_xp;
    struct bundle_xpliant   *bundle;
    struct hmap             *classifier_hmap;
    xpAclType_e             tableType;
    struct ofport_xpliant   *port = NULL;
    struct ofport_xpliant   *next_port = NULL;
    xpsInterfaceId_t        intf;

    VLOG_DBG("%s", __FUNCTION__);

    ofproto_xp = ops_xp_ofproto_cast(ofproto);
    bundle = bundle_lookup(ofproto_xp, aux);
    if ( (bundle == NULL) ||
         ((bundle->l3_intf == NULL) &&
         (interface_info->flags & OPS_CLS_INTERFACE_L3ONLY)) ) {
        VLOG_ERR("Failed to get port bundle/l3_intf not configured");
        return EPERM;
    }

    acl_entry = ops_xp_cls_hmap_lookup(list_id, interface_info, direction);

    if (acl_entry) {

        /* Update ingress ACLID for port and disble ACL on port */
        if (interface_info->interface == OPS_CLS_INTERFACE_VLAN) {
            if (interface_info->flags & OPS_CLS_INTERFACE_L3ONLY) {
                xpsL3SetRouterAclId(ofproto_xp->xpdev->id, 
                                    bundle->l3_intf->l3_intf_id, 0x0);
                xpsL3SetRouterAclEnable(ofproto_xp->xpdev->id,
                                    bundle->l3_intf->l3_intf_id, 0x0);
            }
        } else if (interface_info->interface == OPS_CLS_INTERFACE_PORT) {

            LIST_FOR_EACH_SAFE (port, next_port, bundle_node, &bundle->ports) {
                intf = ops_xp_get_ofport_intf_id(port);
                xpsPortSetField(ofproto_xp->xpdev->id, 
                                intf, XPS_PORT_ACL_EN, 0);
                xpsPortSetField(ofproto_xp->xpdev->id, 
                                intf, XPS_PORT_ACL_ID, 0x0);
            }
        }
        acl_entry->num_intfs--;

        tableType = ops_xp_cls_get_type(interface_info, direction);
        switch (tableType) {
        case XP_ACL_IACL0:
            classifier_hmap = &classifier_hmap_pacl;
            break;

        case XP_ACL_IACL1:
            classifier_hmap = &classifier_hmap_bacl;
            break;

        case XP_ACL_IACL2:
            classifier_hmap = &classifier_hmap_racl;
            break;

        default:
            break;

        }

        if (!acl_entry->num_intfs) {
            ops_xp_cls_acl_delete(acl_entry, (char *)list_name, classifier_hmap);
        }
    }
    return 0;
}

int
ops_xp_cls_replace(const struct uuid *list_id_orig, const char *list_name_orig,
                   struct ops_cls_list *list_new, struct ofproto *ofproto,
                   void *aux, struct ops_cls_interface_info *interface_info,
                   enum ops_cls_direction direction,
                   struct ops_cls_pd_status *pd_status)
{
    struct xp_acl_entry     *acl_entry;
    struct ofproto_xpliant  *ofproto_xp;
    struct bundle_xpliant   *bundle;
    struct hmap             *classifier_hmap;
    struct ofport_xpliant   *port = NULL;
    struct ofport_xpliant   *next_port = NULL;
    xpAclType_e             tableType;
    xpsInterfaceId_t        intf;

    VLOG_DBG("%s", __FUNCTION__);

    ofproto_xp = ops_xp_ofproto_cast(ofproto);
    bundle = bundle_lookup(ofproto_xp, aux);
    if ( (bundle == NULL) ||
         ((bundle->l3_intf == NULL) &&
         (interface_info->flags & OPS_CLS_INTERFACE_L3ONLY)) ) {
        VLOG_ERR("Failed to get port bundle/l3_intf not configured");
        return EPERM;
    }

    acl_entry = ops_xp_cls_hmap_lookup(list_id_orig, interface_info, direction);

    if (acl_entry) {

        /* Update ingress ACLID for port and disble ACL on port*/
        if (interface_info->interface == OPS_CLS_INTERFACE_VLAN) {
            if (interface_info->flags & OPS_CLS_INTERFACE_L3ONLY) {
                xpsL3SetRouterAclId(ofproto_xp->xpdev->id, 
                                    bundle->l3_intf->l3_intf_id, 0x0);
                xpsL3SetRouterAclEnable(ofproto_xp->xpdev->id,
                                    bundle->l3_intf->l3_intf_id, 0x0);
            }
        } else if (interface_info->interface == OPS_CLS_INTERFACE_PORT) {
            LIST_FOR_EACH_SAFE (port, next_port, bundle_node, &bundle->ports) {
                intf = ops_xp_get_ofport_intf_id(port);
                xpsPortSetField(ofproto_xp->xpdev->id, 
                                intf, XPS_PORT_ACL_EN, 0);
                xpsPortSetField(ofproto_xp->xpdev->id, 
                                intf, XPS_PORT_ACL_ID, 0x0);
            }
        }
        acl_entry->num_intfs--;

        ops_xp_cls_apply(list_new, ofproto, aux, interface_info, direction, pd_status);

        if (pd_status->status_code == OPS_CLS_STATUS_SUCCESS) {

            tableType = ops_xp_cls_get_type(interface_info, direction);
            switch(tableType) {
            case XP_ACL_IACL0:
                classifier_hmap = &classifier_hmap_pacl;
                break;

            case XP_ACL_IACL1:
                classifier_hmap = &classifier_hmap_bacl;
                break;

            case XP_ACL_IACL2:
                classifier_hmap = &classifier_hmap_racl;
                break;

            default:
                break;

            }

            if (!acl_entry->num_intfs) {
                ops_xp_cls_acl_delete(acl_entry, (char *)list_name_orig, classifier_hmap);
            }

        } else {
            /* Revert ingress ACLID for port and enable ACL on port */
            if (interface_info->interface == OPS_CLS_INTERFACE_VLAN) {
                if (interface_info->flags & OPS_CLS_INTERFACE_L3ONLY) {
                    xpsL3SetRouterAclId(ofproto_xp->xpdev->id, 
                                        bundle->l3_intf->l3_intf_id, acl_entry->acl_id);
                    xpsL3SetRouterAclEnable(ofproto_xp->xpdev->id,
                                        bundle->l3_intf->l3_intf_id, 0x1);
                }
            } else if (interface_info->interface == OPS_CLS_INTERFACE_PORT) {

                LIST_FOR_EACH_SAFE (port, next_port, bundle_node, &bundle->ports) {
                    intf = ops_xp_get_ofport_intf_id(port);
                    xpsPortSetField(ofproto_xp->xpdev->id, 
                                    intf, XPS_PORT_ACL_EN, 1);
                    xpsPortSetField(ofproto_xp->xpdev->id, 
                                    intf, XPS_PORT_ACL_ID, acl_entry->acl_id);
                }
            }
            acl_entry->num_intfs++;

        }
    }
    return 0;
}

int
ops_xp_cls_list_update(struct ops_cls_list *list,
                       struct ops_cls_pd_list_status *status)
{
    struct xp_acl_entry         *acl_entry_pacl, *acl_entry_racl;
    uint8_t                     updated_pacl, updated_racl;
    uint8_t                     failed_pacl, failed_racl;
    struct ovs_list             pacl_new_list, racl_new_list;
    struct ovs_list             pacl_old_list, racl_old_list;
    struct ops_cls_pd_status    *pd_status;

    VLOG_DBG("%s", __FUNCTION__);

    updated_pacl = 0;
    updated_racl = 0; 

    failed_pacl = 0;
    failed_racl = 0;

    pd_status = malloc(sizeof(struct ops_cls_pd_status));

    /* Validate unsupported parameters */
    ops_xp_cls_validate_entries(list, &pd_status);

    if (pd_status->status_code == OPS_CLS_STATUS_HW_UNSUPPORTED_ERR) {
        status->status_code = OPS_CLS_STATUS_HW_UNSUPPORTED_ERR;
        return 0;
    }

    /* Check if PACL needs to be updated and update if needed */
    acl_entry_pacl = ops_xp_cls_lookup_from_hmap_type(&list->list_id, &classifier_hmap_pacl);

    if (acl_entry_pacl) {

        pacl_old_list = acl_entry_pacl->rule_list;
        list_init(&pacl_new_list);

        if (list->entries != NULL) {

            pd_status = ops_xp_cls_create_rule_entry_list(acl_entry_pacl,
                                                          list, &pacl_new_list);

            acl_entry_pacl->rule_list = pacl_new_list;
            updated_pacl = 1;

            if (pd_status != OPS_CLS_STATUS_SUCCESS) {
                failed_pacl = 1;
            }
        }
    }

    /* Check if RACL needs to be updated and update if needed */
    acl_entry_racl = ops_xp_cls_lookup_from_hmap_type(&list->list_id,
                                                      &classifier_hmap_racl);
    
    if (acl_entry_racl) {

        racl_old_list = acl_entry_racl->rule_list;
        list_init(&racl_new_list);

        if (list->entries != NULL) {

            pd_status = ops_xp_cls_create_rule_entry_list(acl_entry_racl, list,
                                                          &racl_new_list);
                
            acl_entry_racl->rule_list = racl_new_list;
            updated_racl = 1;

            if (pd_status != OPS_CLS_STATUS_SUCCESS) {
                failed_racl = 1;
            }
        }
    }

    if (!updated_pacl && !updated_racl) {
        /* We are trying to update a  non existing Classifier
         * hence returning invalid configuration
         */

        status->status_code = OPS_CLS_STATUS_HW_CONFIG_ERR;
        return 0;
    }

    if (failed_pacl || failed_racl) {

        if (updated_pacl) {
            /* Destroy new ACLlist and add old list to ACL */
            ops_xp_cls_destroy_rule_entry_list(acl_entry_pacl);

            /* Update it to old value */
            acl_entry_pacl->rule_list = pacl_old_list;
        }

        if (updated_racl) {
            /* Destroy new ACLlist and add old list to ACL */
            ops_xp_cls_destroy_rule_entry_list(acl_entry_racl);

            /* Update it to old value */
            acl_entry_racl->rule_list = racl_old_list;
        }

        status->status_code = OPS_CLS_STATUS_HW_CONFIG_ERR;

    } else {
    
        if (updated_pacl) {
            /* The API to destroy expects all the info in ACL entry
             * so first assign old list to the entry, destroy the list and
             * then update the new list to ACL entry
             */
            acl_entry_pacl->rule_list = pacl_old_list;
            ops_xp_cls_destroy_rule_entry_list(acl_entry_pacl);

            acl_entry_pacl->rule_list = pacl_new_list;
            
            /* updating new lists first node and last node propely with base
             * list pointer, so that circular linked list is updated properly
             */
            pacl_new_list.next->prev = &acl_entry_pacl->rule_list;
            pacl_new_list.prev->next = &acl_entry_pacl->rule_list;
        }

        if (updated_racl) {
            /* The API to destroy expects all the info in ACL entry so
             * first assign old list to the entry, destroy the list and
             * then update the new list to ACL entry
             */
            acl_entry_racl->rule_list = racl_old_list;
            ops_xp_cls_destroy_rule_entry_list(acl_entry_racl);

            acl_entry_racl->rule_list = racl_new_list;

            /* updating new lists first node and last node propely with base
             * list pointer, so that circular linked list is updated properly
             */
            racl_new_list.next->prev = &acl_entry_racl->rule_list;
            racl_new_list.prev->next = &acl_entry_racl->rule_list;
        }
    }
    return 0;
}

int
ops_xp_cls_stats_get(const struct uuid *list_id, const char *list_name,
                     enum ops_cls_type list_type, struct ofproto *ofproto,
                     void *aux, struct ops_cls_interface_info *interface_info,
                     enum ops_cls_direction direction,
                     struct ops_cls_statistics *statistics,
                     int num_entries, struct ops_cls_pd_list_status *status)
{
    
    xpAclType_e         acl_type;
    xpAcmClient_e       client;
    xpsDevice_t         devId;
    struct hmap         *classifier_hmap;
    uint8_t             invalid_iacl_type;
    struct xp_acl_entry *acl_entry;
    struct ovs_list     *ovslist;
    struct xp_acl_rule  *entry, *next_entry;
    uint32_t            tcamId;
    uint64_t            count_pkts, count_bytes;

    VLOG_DBG("%s", __FUNCTION__);
    
    /* currently we support statistics based on the ACL type only
     * i.e per ACE statistics available for L2 port, L3 port and Vlan
     * interface. we can not support per port per ACE basis as per HW
     * limitation
     */

    devId = 0;
    invalid_iacl_type = 0;

    acl_type = ops_xp_cls_get_type(interface_info, direction);

    switch(acl_type) {
    case XP_ACL_IACL0:
        classifier_hmap = &classifier_hmap_pacl;
        client = XP_ACM_IPACL_COUNTER;
        break;

    case XP_ACL_IACL1:
        classifier_hmap = &classifier_hmap_bacl;
        client = XP_ACM_IBACL_COUNTER;
        break;

    case XP_ACL_IACL2:
        classifier_hmap = &classifier_hmap_racl;
        client = XP_ACM_IRACL_COUNTER;
        break;

    default:
        invalid_iacl_type = 1;
        break;

    }
    
    if (invalid_iacl_type) {
        /* update status and return */
        return 0;
    }

    acl_entry = ops_xp_cls_lookup_from_hmap_type(list_id,
                                                 classifier_hmap);
    
    if (acl_entry) {
        
        int i = 0;
        ovslist = &acl_entry->rule_list;
        
        if (num_entries+1 != acl_entry->num_rules) {
            /* update status and return */
            return 0;
        }

        VLOG_DBG("acl_id %d exists in HMAP for this UUID %d",
                 acl_entry->acl_id, *list_id);

        /* Print TCAM manager rule ids for debugging purpose */
        LIST_FOR_EACH_SAFE (entry, next_entry, list_node, ovslist) {
            
            xpsTcamMgrTcamIdFromEntryGet(0, 0, entry->rule_id, &tcamId);
            count_pkts = 0;
            count_bytes = 0;
            
            if ((i != num_entries) && entry->counter_en) {

                xpsAcmGetCounterValue(devId, client, tcamId, &count_pkts, &count_bytes);
                entry->count += count_pkts;
                statistics[i].stats_enabled = 1;
                statistics[i].hitcounts = entry->count;

                VLOG_DBG("count for rule entry %d, tcam manger rule id :%d, HWTCAM id :%d is %d",
                         i, entry->rule_id, tcamId, entry->count);

            }

            i++;
        }

    }

    return 0;
}

int
ops_xp_cls_stats_clear(const struct uuid *list_id, const char *list_name,
                       enum ops_cls_type list_type, struct ofproto *ofproto,
                       void *aux, struct ops_cls_interface_info *interface_info,
                       enum ops_cls_direction direction,
                       struct ops_cls_pd_list_status *status)
{
    xpAclType_e         acl_type;
    xpAcmClient_e       client;
    xpsDevice_t         devId;
    struct hmap         *classifier_hmap;
    uint8_t             invalid_iacl_type;
    struct xp_acl_entry *acl_entry;
    struct ovs_list     *ovslist;
    struct xp_acl_rule  *entry, *next_entry;
    uint32_t            tcamId;
    uint64_t            count_pkts, count_bytes;

    VLOG_DBG("%s", __FUNCTION__);

    /* currently we support statistics based on the ACL type only
     * i.e per ACE statistics available for L2 port, L3 port and Vlan
     * interface. we can not support per port per ACE basis as per HW
     * limitation
     */

    devId = 0;
    invalid_iacl_type = 0;

    acl_type = ops_xp_cls_get_type(interface_info, direction);

    switch(acl_type) {
    case XP_ACL_IACL0:
        classifier_hmap = &classifier_hmap_pacl;
        client = XP_ACM_IPACL_COUNTER;
        break;

    case XP_ACL_IACL1:
        classifier_hmap = &classifier_hmap_bacl;
        client = XP_ACM_IBACL_COUNTER;
        break;

    case XP_ACL_IACL2:
        classifier_hmap = &classifier_hmap_racl;
        client = XP_ACM_IRACL_COUNTER;
        break;

    default:
        invalid_iacl_type = 1;
        break;

    }

    if (invalid_iacl_type) {
        /* update status and return */
        return 0;
    }

    acl_entry = ops_xp_cls_lookup_from_hmap_type(list_id,
                                                 classifier_hmap);

    if (acl_entry) {

        int i = 0;
        ovslist = &acl_entry->rule_list;

        VLOG_DBG("acl_id %d exists in HMAP for this UUID %d",
                 acl_entry->acl_id, *list_id);

        /* Print TCAM manager rule ids for debugging purpose */
        LIST_FOR_EACH_SAFE (entry, next_entry, list_node, ovslist) {

            xpsTcamMgrTcamIdFromEntryGet(0, 0, entry->rule_id, &tcamId);
            count_pkts = 0;
            count_bytes = 0;

            if (entry->counter_en) {

                /* HW counters work on clear on read basis and hence the below call clears the counter value */
                xpsAcmGetCounterValue(devId, client, tcamId, &count_pkts, &count_bytes);
                entry->count = 0;

                VLOG_DBG("count for rule entry %d, tcam manger rule id :%d, HWTCAM id :%d is %d",
                         i, entry->rule_id, tcamId, entry->count);
            }

            i++;
        }

    }

    return 0;
}

int
ops_xp_cls_stats_clear_all(struct ops_cls_pd_list_status *status)
{
    VLOG_DBG("%s", __FUNCTION__);

    return 0;
}

int
ops_xp_cls_acl_log_pkt_register_cb(void (*callback_handler)(struct acl_log_info *))
{
    VLOG_DBG("%s", __FUNCTION__);

    return 0;
}
