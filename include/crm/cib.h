/* $Id: cib.h,v 1.14 2005/01/18 20:33:04 andrew Exp $ */
/* 
 * Copyright (C) 2004 Andrew Beekhof <andrew@beekhof.net>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef CIB__H
#define CIB__H

#include <libxml/tree.h> 
#include <clplumbing/ipc.h>
#include <crm/common/ipc.h>
#include <ha_msg.h>

#define cib_feature_revision 1
#define cib_feature_revision_s "1"

enum cib_variant {
	cib_native,
	cib_database,
	cib_edir
};

enum cib_state {
	cib_connected_command,
	cib_connected_query,
	cib_disconnected
};

enum cib_conn_type {
	cib_command,
	cib_query,
	cib_no_connection
};

enum cib_call_options {
	cib_none          = 0x000000,
	cib_verbose       = 0x000001,
	cib_discard_reply = 0x000004,
	cib_scope_local   = 0x000010,
/* 	cib_scope_global  = 0x000020, */
	cib_sync_call     = 0x000040,
/* 	cib_async_call    = 0x000080, */
	cib_inhibit_notify= 0x000100
};

#define cib_default_options = cib_none

enum cib_errors {
	cib_ok			=  0,
	cib_operation		= -1,
	cib_create_msg		= -2,
	cib_not_connected	= -3,
	cib_not_authorized	= -4,
	cib_send_failed		= -5,
	cib_reply_failed	= -6,
	cib_return_code		= -7,
	cib_output_ptr		= -8,
	cib_output_data		= -9,
	cib_connection		= -10,
	cib_authentication	= -11,
	cib_missing		= -12,
	cib_variant		= -28,
	CIBRES_MISSING_ID	= -13,
	CIBRES_MISSING_TYPE	= -14,
	CIBRES_MISSING_FIELD	= -15,
	CIBRES_OBJTYPE_MISMATCH	= -16,
	CIBRES_CORRUPT		= -17,	
	CIBRES_OTHER		= -18,
	cib_unknown		= -19,
	cib_STALE		= -20,
	cib_EXISTS		= -21,
	cib_NOTEXISTS		= -22,
	cib_ACTIVATION		= -23,
	cib_NOSECTION		= -24,
	cib_NOOBJECT		= -25,
	cib_NOPARENT		= -26,
	cib_NODECOPY		= -27,
	cib_NOTSUPPORTED	= -29,
	cib_registration_msg	= -30,
	cib_callback_token	= -31,
	cib_callback_register	= -32,
	cib_msg_field_add	= -33,
	cib_client_gone		= -34,
	cib_not_master		= -35,
	cib_client_corrupt	= -36,
	cib_master_timeout	= -37,
	cib_revision_unsupported= -38,
	cib_revision_unknown	= -39,
	cib_missing_data	= -40
};

enum cib_op {
	CIB_OP_NONE = 0,
	CIB_OP_ADD,
	CIB_OP_MODIFY,
	CIB_OP_DELETE,
	CIB_OP_MAX
};

enum cib_section {
	cib_section_none,
	cib_section_all,
	cib_section_nodes,
	cib_section_constraints,
	cib_section_resources,
	cib_section_crmconfig,
	cib_section_status
};

#define F_CIB_CLIENTID  "cib_clientid"
#define F_CIB_CALLOPTS  "cib_callopt"
#define F_CIB_CALLID    "cib_callid"
#define F_CIB_CALLDATA  "cib_calldata"
#define F_CIB_OPERATION "cib_op"
#define F_CIB_ISREPLY   "cib_isreplyto"
#define F_CIB_SECTION   "cib_section"
#define F_CIB_HOST	"cib_host"
#define F_CIB_RC	"cib_rc"
#define F_CIB_DELEGATED	"cib_delegated_from"
#define F_CIB_OBJID	"cib_object"
#define F_CIB_OBJTYPE	"cib_object_type"
#define F_CIB_EXISTING	"cib_existing_object"
#define F_CIB_SEENCOUNT	"cib_seen"
#define F_CIB_TIMEOUT	"cib_timeout"
#define F_CIB_UPDATE	"cib_update"
#define F_CIB_CALLBACK_TOKEN	"cib_callback_token"
#define F_CIB_GLOBAL_UPDATE	"cib_update"
#define F_CIB_UPDATE_RESULT	"cib_update_result"

#define T_CIB			"cib"
#define T_CIB_NOTIFY		"cib_notify"
/* notify sub-types */
#define T_CIB_PRE_NOTIFY	"cib_pre_notify"
#define T_CIB_POST_NOTIFY	"cib_post_notify"
#define T_CIB_UPDATE_CONFIRM	"cib_update_confirmation"

#define cib_channel_ro		"cib_ro"
#define cib_channel_rw		"cib_rw"
#define cib_channel_callback	"cib_callback"

typedef struct cib_s cib_t;

typedef struct cib_api_operations_s
{
		int (*variant_op)(
			cib_t *cib, const char *op, const char *host,
			const char *section, xmlNodePtr data,
			xmlNodePtr *output_data, int call_options);
		
		int (*signon) (cib_t *cib, enum cib_conn_type type);
		int (*signoff)(cib_t *cib);
		int (*free) (cib_t *cib);

		int (*set_op_callback)(
			cib_t *cib, void (*callback)(
				const HA_Message *msg, int callid ,
				int rc, xmlNodePtr output));

		int (*add_notify_callback)(
			cib_t *cib, const char *event, void (*callback)(
				const char *event, HA_Message *msg));

		int (*del_notify_callback)(
			cib_t *cib, const char *event, void (*callback)(
				const char *event, HA_Message *msg));

		int (*set_connection_dnotify)(
			cib_t *cib, void (*dnotify)(gpointer user_data));
		
		IPC_Channel *(*channel)(cib_t* cib);
		int (*inputfd)(cib_t* cib);

		int (*noop)(cib_t *cib, int call_options);
		int (*ping)(
			cib_t *cib, xmlNodePtr *output_data, int call_options);

		int (*query)(cib_t *cib, const char *section,
			     xmlNodePtr *output_data, int call_options);
		int (*query_from)(
			cib_t *cib, const char *host, const char *section,
			xmlNodePtr *output_data, int call_options);

		gboolean (*is_master) (cib_t *cib);
		int (*set_master)(cib_t *cib, int call_options);
		int (*set_slave) (cib_t *cib, int call_options);
		int (*set_slave_all)(cib_t *cib, int call_options);
		
		int (*sync)(cib_t *cib, const char *section, int call_options);
		int (*sync_from)(
			cib_t *cib, const char *host, const char *section,
			int call_options);

		int (*bump_epoch)(cib_t *cib, int call_options);
		
		int (*create)(cib_t *cib, const char *section, xmlNodePtr data,
			   xmlNodePtr *output_data, int call_options) ;
		int (*modify)(cib_t *cib, const char *section, xmlNodePtr data,
			   xmlNodePtr *output_data, int call_options) ;
		int (*replace)(cib_t *cib, const char *section, xmlNodePtr data,
			   xmlNodePtr *output_data, int call_options) ;
		int (*delete)(cib_t *cib, const char *section, xmlNodePtr data,
			   xmlNodePtr *output_data, int call_options) ;
		int (*erase)(
			cib_t *cib, xmlNodePtr *output_data, int call_options);

		int (*quit)(cib_t *cib,   int call_options);
		
		gboolean (*msgready)(cib_t* cib);
		int (*rcvmsg)(cib_t* cib, int blocking);
		gboolean (*dispatch)(IPC_Channel *channel, gpointer user_data);
} cib_api_operations_t;

struct cib_s
{
		enum cib_state	   state;
		enum cib_conn_type type;

		int   call_id;
		void  *variant_opaque;

		GList *notify_list;
		void (*op_callback)(const HA_Message *msg, int call_id,
				    int rc, xmlNodePtr output);

		cib_api_operations_t *cmds;
};

typedef struct cib_notify_client_s 
{
	const char *event;
	const char *obj_id;   /* implement one day */
	const char *obj_type; /* implement one day */
	void (*callback)(
		const char *event, HA_Message *msg);
	
} cib_notify_client_t;

/* Core functions */
extern cib_t *cib_new(void);

extern gboolean   startCib(const char *filename);
extern xmlNodePtr get_cib_copy(cib_t *cib);
extern xmlNodePtr cib_get_generation(cib_t *cib);
extern int cib_compare_generation(xmlNodePtr left, xmlNodePtr right);

/* Utility functions */
extern xmlNodePtr get_object_root(const char *object_type,xmlNodePtr the_root);
extern xmlNodePtr create_cib_fragment_adv(
			xmlNodePtr update, const char *section, const char *source);
extern char      *cib_pluralSection(const char *a_section);

/* Error Interpretation*/
extern const char *cib_error2string(enum cib_errors);
extern const char *cib_op2string(enum cib_op);

extern xmlNodePtr createEmptyCib(void);
extern gboolean verifyCibXml(xmlNodePtr cib);
extern int cib_section2enum(const char *a_section);

#define create_cib_fragment(update,section) create_cib_fragment_adv(update, section, __FUNCTION__)


#endif




