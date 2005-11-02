/*
 * CIM Provider
 * 
 * Author: Jia Ming Pan <jmltc@cn.ibm.com>
 * Copyright (c) 2005 International Business Machines
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */


#include <portability.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <clplumbing/cl_malloc.h>

#include "linuxha_info.h"
#include "cmpi_resource.h"
#include "cmpi_utils.h"
#include "ha_resource.h"

static void add_res_for_each (gpointer data, gpointer user);

static GPtrArray * get_resource_info_table (void);
static int free_resource_info_table ( GPtrArray * resource_info_table);


static void 
add_res_for_each (gpointer data, gpointer user)
{
        struct res_node * node = NULL;
        GPtrArray * array = NULL;

        node = (struct res_node *) data;
   
        if ( node == NULL ) {
                return;
        }

        array = (GPtrArray *) user;
                
        if ( node->type == GROUP ) {
                struct cluster_resource_group_info * info = NULL;
                info = (struct cluster_resource_group_info *)
                        node->res;
                g_list_foreach(info->res_list, add_res_for_each, array);

        } else {
                struct cluster_resource_info * info = NULL;
                info = (struct cluster_resource_info *)
                        node->res;
                cl_log(LOG_INFO, 
                       "%s: add resource %s", __FUNCTION__, info->name);
                
                g_ptr_array_add(array, res_info_dup (info));
        }
}


static GPtrArray * 
get_resource_info_table ()
{
        
        GPtrArray * resource_info_table = NULL;
        GList * list = NULL;
        
        DEBUG_ENTER();

        resource_info_table = g_ptr_array_new ();

        if ( resource_info_table == NULL ) {
                cl_log(LOG_ERR, "%s: failed to alloc array", __FUNCTION__);
                return NULL;
        }

        list = get_res_list ();
        
        g_list_foreach(list, add_res_for_each, resource_info_table);

        free_res_list (list);
        
        DEBUG_LEAVE();

        return resource_info_table;
}

static int 
free_resource_info_table ( GPtrArray * resource_info_table)
{
        struct cluster_resource_info * resource_info = NULL;

        while (resource_info_table->len) {

                resource_info = (struct cluster_resource_info *)
                        g_ptr_array_remove_index_fast(resource_info_table, 0);
                
                free(resource_info->name);
                free(resource_info->class);
                free(resource_info->type);

                free(resource_info);

                resource_info = NULL;

        }

        g_ptr_array_free(resource_info_table, 0);

        return HA_OK;
}



static CMPIInstance *
make_resource_instance(char * classname, CMPIBroker * broker, 
                       CMPIObjectPath * op, char * rsc_name, CMPIStatus * rc)
{
        CMPIInstance * ci = NULL;
        char * hosting_node = NULL;
        GPtrArray * info_table = NULL;
        struct cluster_resource_info * info = NULL;
        int i = 0;


        DEBUG_ENTER();

        cl_log(LOG_INFO, "%s: make instance for %s", __FUNCTION__, rsc_name);

        info_table = get_resource_info_table ();

        if ( info_table == NULL ) {
                cl_log(LOG_ERR, "%s: can't get resource info", __FUNCTION__);

	        CMSetStatusWithChars(broker, rc, 
		       CMPI_RC_ERR_FAILED, "Can't get resource info");
                goto out;
        }

        
        for ( i = 0; i < info_table->len; i++ ) {

                info = (struct cluster_resource_info *)
                        g_ptr_array_index(info_table, i); 

                
                cl_log(LOG_INFO, "%s: looking for [%s], rsc @ %d = [%s]",
                        __FUNCTION__, rsc_name, i, info->name);
                
                if ( strcmp(info->name, rsc_name) == 0 ) {
                        cl_log(LOG_INFO, "%s: found!", __FUNCTION__);
                        break;
                } 
        }


        if ( i == info_table->len ) {
                cl_log(LOG_WARNING, "%s: resource %s not found!",
                                __FUNCTION__, rsc_name);

	        CMSetStatusWithChars(broker, rc, 
		       CMPI_RC_ERR_NOT_FOUND, "Resource not found");

                goto out;
        }

        ASSERT(info);

        ci = CMNewInstance(broker, op, rc);

        if ( CMIsNullObject(ci) ) {
                cl_log(LOG_ERR, "%s: can't create instance", __FUNCTION__);

	        CMSetStatusWithChars(broker, rc, 
		       CMPI_RC_ERR_FAILED, "Can't get create instance");
                goto out;
        }
 
        cl_log(LOG_INFO, "%s: setting properties", __FUNCTION__);

        /* setting properties */

        CMSetProperty(ci, "Type", info->type, CMPI_chars);
        CMSetProperty(ci, "ResourceClass", info->class, CMPI_chars);
        CMSetProperty(ci, "Name", info->name, CMPI_chars);
       
        hosting_node = get_hosting_node(rsc_name);

        cl_log(LOG_INFO, "%s: hosting_node of %s is %s", __FUNCTION__, 
               rsc_name, hosting_node);

        if (hosting_node){
                char status [] = "Running";
                cl_log(LOG_INFO, "Hosting node is %s", hosting_node);

                CMSetProperty(ci, "Status", status, CMPI_chars);
                CMSetProperty(ci, "HostingNode", hosting_node, CMPI_chars);
        }else{
                char status [] = "Not running";
                CMSetProperty(ci, "Status", status, CMPI_chars);
        }

out:
        if ( info_table ) {
                free_resource_info_table ( info_table );
        }

        DEBUG_LEAVE();
        return ci;
}

int 
enumerate_resource_instances(char * classname, CMPIBroker * broker,
                             CMPIContext * ctx, CMPIResult * rslt,
                             CMPIObjectPath * ref, int enum_inst, 
                             CMPIStatus * rc)
{
        CMPIObjectPath * op = NULL;
        char * namespace = NULL;
        int i = 0;
        GPtrArray * info_table = NULL;

        
        info_table = get_resource_info_table (); 
        
        if ( info_table == NULL ) {
                cl_log(LOG_ERR, "%s: can't get resource info", __FUNCTION__);
                return HA_FAIL;
        }
        
        namespace = CMGetCharPtr(CMGetNameSpace(ref, rc));
        op = CMNewObjectPath(broker, namespace, classname, rc);

        if ( CMIsNullObject(op) ){
                free_resource_info_table ( info_table );
                return HA_FAIL;
        }

        for (i = 0; i < info_table->len; i++){
                struct cluster_resource_info * info = NULL;
           
                info = (struct cluster_resource_info *)
                        g_ptr_array_index(info_table, i); 

                /* create an object */
                CMAddKey(op, "Name", info->name, CMPI_chars);
                if ( enum_inst ) {
                        CMPIInstance * ci = NULL;
                        ci = make_resource_instance(classname, 
                                        broker, op, info->name, rc); 

                        if ( CMIsNullObject(ci) ){
                                cl_log(LOG_WARNING, 
                                   "%s: can not make instance", __FUNCTION__);
                                free_resource_info_table ( info_table );
                                return HA_FAIL;
                        }
                        
                        CMReturnInstance(rslt, ci);
                } else {
        
                        /* add object to rslt */
                        CMReturnObjectPath(rslt, op);
                
                }
        }
        
        CMReturnDone(rslt);

        free_resource_info_table ( info_table );

        return HA_OK;
}




int
get_resource_instance(char * classname, CMPIBroker * broker, CMPIContext * ctx,
                      CMPIResult * rslt, CMPIObjectPath * cop,
                      char ** properties, CMPIStatus * rc)
{
        CMPIInstance* ci = NULL;
        CMPIObjectPath* op = NULL;
        CMPIData data_name;
        char * rsc_name = NULL;
        int ret = 0;

        
        DEBUG_ENTER();

        data_name = CMGetKey(cop, "Name", rc);

        if ( data_name.value.string == NULL ) {
                cl_log(LOG_WARNING, "key %s is NULL", "Name");
                ret = HA_FAIL;
                goto out;
        }

        rsc_name = CMGetCharPtr(data_name.value.string);

        cl_log(LOG_INFO, "rsc_name = %s", rsc_name);
 
        op = CMNewObjectPath(broker, 
                        CMGetCharPtr(CMGetNameSpace(cop, rc)), classname, rc);

        if ( CMIsNullObject(op) ){
                ret = HA_FAIL;
                cl_log(LOG_WARNING, 
                        "%s: can not create object path.", __FUNCTION__);
                goto out;
        }

        ci = make_resource_instance(classname, broker, op, rsc_name, rc);

        if ( CMIsNullObject(ci) ) {
                ret = HA_FAIL;
                cl_log(LOG_WARNING, 
                        "%s: can not create instance.", __FUNCTION__);
                goto out;
        }

        CMReturnInstance(rslt, ci);

        CMReturnDone(rslt);

        ret = HA_OK;
out:
        DEBUG_LEAVE();
        return ret;
}


