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
#include <crm/common/crm.h>
#include <crmd_fsa.h>
#include <libxml/tree.h>
#include <crm/common/xmltags.h>
#include <crm/common/xmlvalues.h>
#include <crm/common/xmlutils.h>
#include <crm/common/ipcutils.h>
#include <crm/common/msgutils.h>
#include <crm/cib.h>
#include <string.h>
#include <crmd_messages.h>

#include <crm/dmalloc_wrapper.h>

#define CCM_UNAME 1

GHashTable *joined_nodes = NULL;

/*	A_ELECTION_VOTE	*/
enum crmd_fsa_input
do_election_vote(long long action,
		 enum crmd_fsa_cause cause,
		 enum crmd_fsa_state cur_state,
		 enum crmd_fsa_input current_input,
		 void *data)
{
	const char *node_uname = NULL;
	enum crmd_fsa_input election_result = I_NULL;
	FNIN();

	// send our vote unless we want to shut down
#if 0
	if(we are sick || shutting down) {
		log error ;
		FNRET(I_NULL);
	} 
#endif

#ifdef CCM_UNAME
	node_uname = fsa_membership_copy->members[0].node_uname;
#else
	node_uname = "";
#endif

	// set the "we won" timer
	startTimer(election_timeout);
	CRM_DEBUG("Set the election timer... if it goes off, we win.");

	/* host_to = NULL (for broadcast) */
	send_request(NULL, NULL, CRM_OPERATION_VOTE, NULL, CRM_SYSTEM_CRMD);
	
	FNRET(election_result);
}

gboolean
timer_popped(gpointer data)
{
	fsa_timer_t *timer = (fsa_timer_t *)data;

	cl_log(LOG_INFO, "#!!#!!# Timer %s just popped!",
	       fsa_input2string(timer->fsa_input));
	
	stopTimer(timer); // dont make it go off again

	s_crmd_fsa(C_TIMER_POPPED, timer->fsa_input, NULL);
	
	return TRUE;
}

gboolean
do_dc_heartbeat(gpointer data)
{
	fsa_timer_t *timer = (fsa_timer_t *)data;
//	cl_log(LOG_DEBUG, "#!!#!!# Heartbeat timer just popped!");
	
	gboolean was_sent = send_request(NULL, NULL, CRM_OPERATION_HBEAT, 
					 NULL, CRM_SYSTEM_CRMD);

	if(was_sent == FALSE) {
		// this is bad
		stopTimer(timer); // dont make it go off again
		s_crmd_fsa(C_HEARTBEAT_FAILED, I_SHUTDOWN, NULL);
	}
	
	return TRUE;
}


/*	A_ELECTION_COUNT	*/
enum crmd_fsa_input
do_election_count_vote(long long action,
		       enum crmd_fsa_cause cause,
		       enum crmd_fsa_state cur_state,
		       enum crmd_fsa_input current_input,
		       void *data)
{
	gboolean we_loose = FALSE;
	xmlNodePtr vote = (xmlNodePtr)data;
	unsigned int my_born = -1, your_born = -1;
	int lpc = 0, my_index = -1, your_index = -1;
	enum crmd_fsa_input election_result = I_NULL;
	const char *vote_from = xmlGetProp(vote, XML_ATTR_HOSTFROM);
	
	FNIN();

	if(vote_from == NULL || strcmp(vote_from, fsa_our_uname) == 0) {
		// dont count our own vote
		FNRET(election_result);
	}

	CRM_DEBUG2("Max bornon (%d)", your_born);
	
	for(; (your_index < 0 && my_index < 0)
		    || lpc < fsa_membership_copy->members_size; lpc++) {
		
#ifdef CCM_UNAME
		const char *node_uname =
			fsa_membership_copy->members[lpc].node_uname;
#else
		const char *node_uname = "";
#endif
		
		if(node_uname != NULL) {
			if(strcmp(vote_from, node_uname) == 0) {
				your_born = fsa_membership_copy->members[lpc].node_born_on;
				your_index = lpc;
			} else if (strcmp(fsa_our_uname, node_uname) == 0) {
				my_born = fsa_membership_copy->members[lpc].node_born_on;
				my_index = lpc;
			}
		}
	}
	
/* 	cl_log(LOG_DEBUG, "%s (index=%d), our index (%d)", */
/* 		   vote_from, your_index, my_index); */

	cl_log(LOG_DEBUG, "%s (bornon=%d), our bornon (%d)",
		   vote_from, your_born, my_born);

	cl_log(LOG_DEBUG, "%s %s %s",
	       fsa_our_uname,
	       strcmp(fsa_our_uname, vote_from) < 0?"<":">=",
	       vote_from);

	if(your_born < my_born) {
		CRM_DEBUG("Election fail: born_on");
		we_loose = TRUE;
	} else if(your_born == my_born
		  && strcmp(fsa_our_uname, vote_from) < 0) {
		CRM_DEBUG("Election fail: uname");
		we_loose = TRUE;
	} else {
		CRM_DEBUG("We might win... we should vote (possibly again)");
			
		// do vote (it safe to call this multiple times)
		election_result =
			do_election_vote(action,
					 cause,
					 cur_state,
					 current_input,
					 data);
	}

	if(we_loose) {
		CRM_DEBUG("We lost the election");
		if(fsa_input_register & R_THE_DC) {
			CRM_DEBUG("Give up the DC");
			election_result = I_RELEASE_DC;
		} else {
			CRM_DEBUG("We werent the DC anyway");
			election_result = I_NOT_DC;
		}
		
		// cancel timer, its been decided
		stopTimer(election_timeout);
	}
	
	FNRET(election_result);
}

/*	A_ELECTION_TIMEOUT	*/
// we won
enum crmd_fsa_input
do_election_timeout(long long action,
		    enum crmd_fsa_cause cause,
		    enum crmd_fsa_state cur_state,
		    enum crmd_fsa_input current_input,
		    void *data)
{
	FNIN();

	CRM_DEBUG("The election timer went off, that means we win");
	
	// cleanup timer
	stopTimer(election_timeout);
	
	FNRET(I_ELECTION_DC);
}

/*	A_DC_TIMER_STOP, A_DC_TIMER_START	*/
enum crmd_fsa_input
do_dc_timer_control(long long action,
		   enum crmd_fsa_cause cause,
		   enum crmd_fsa_state cur_state,
		   enum crmd_fsa_input current_input,
		   void *data)
{
	FNIN();

	if(action & A_DC_TIMER_STOP) {
		stopTimer(election_trigger);
	}

	if(action & A_DC_TIMER_START) {
		startTimer(election_trigger);
	}
	
	FNRET(I_NULL);
}


/*	 A_DC_TAKEOVER	*/
enum crmd_fsa_input
do_dc_takeover(long long action,
	       enum crmd_fsa_cause cause,
	       enum crmd_fsa_state cur_state,
	       enum crmd_fsa_input current_input,
	       void *data)
{
	FNIN();

	CRM_DEBUG("################## Taking over the DC ##################");
	set_bit_inplace(&fsa_input_register, R_THE_DC);

	CRM_DEBUG2("Am I the DC? %s", AM_I_DC?"yes":"no");
	
	set_bit_inplace(&fsa_input_register, R_JOIN_OK);
	set_bit_inplace(&fsa_input_register, R_INVOKE_PE);
	
	clear_bit_inplace(&fsa_input_register, R_CIB_DONE);
	clear_bit_inplace(&fsa_input_register, R_HAVE_CIB);

	startTimer(dc_heartbeat);
	
	FNRET(I_NULL);
}

/*	 A_DC_RELEASE	*/
enum crmd_fsa_input
do_dc_release(long long action,
	      enum crmd_fsa_cause cause,
	      enum crmd_fsa_state cur_state,
	      enum crmd_fsa_input current_input,
	      void *data)
{
	enum crmd_fsa_input result = I_NULL;
	FNIN();

	CRM_DEBUG("################## Releasing the DC ##################");

	stopTimer(dc_heartbeat);

	if(action & A_DC_RELEASE) {
		clear_bit_inplace(&fsa_input_register, R_THE_DC);
		
		/* get a new CIB from the new DC */
		clear_bit_inplace(&fsa_input_register, R_HAVE_CIB);
	} else if (action & A_DC_RELEASED) {

		if(cur_state == S_STOPPING) {
			result = I_SHUTDOWN; // necessary?
			result = I_RELEASE_SUCCESS;
		}
#if 0
		else if( are there errors ) {
			// we cant stay up if not healthy
			// or perhaps I_ERROR and go to S_RECOVER?
			result = I_SHUTDOWN;
		}
#endif
		else
			result = I_RELEASE_SUCCESS;

	} else {
		cl_log(LOG_ERR, "Warning, do_dc_release invoked for action %s",
		       fsa_action2string(action));
	}

	CRM_DEBUG2("Am I still the DC? %s", AM_I_DC?"yes":"no");

	FNRET(result);
}

/*	 A_JOIN_WELCOME, A_JOIN_WELCOME_ALL	*/
enum crmd_fsa_input
do_send_welcome(long long action,
		enum crmd_fsa_cause cause,
		enum crmd_fsa_state cur_state,
		enum crmd_fsa_input current_input,
		void *data)
{
	int lpc = 0, size = 0, num_sent = 0;
	oc_node_t *members;
	gboolean was_sent = TRUE;
	FNIN();

	startTimer(integration_timer);
	
	if(action & A_JOIN_WELCOME && data == NULL) {
		cl_log(LOG_ERR,
		       "Attempt to send welcome message "
		       "without a message to reply to!");
		FNRET(I_NULL);
		
	} else if(action & A_JOIN_WELCOME) {
		xmlNodePtr welcome = (xmlNodePtr)data;
		xmlNodePtr options = find_xml_node(welcome, XML_TAG_OPTIONS);
		set_xml_property_copy(options, XML_ATTR_OP, CRM_OPERATION_WELCOME);

		send_ha_reply(fsa_cluster_connection, welcome, NULL);

		FNRET(I_NULL);
	}
	
	members = fsa_membership_copy->members;
	size = fsa_membership_copy->members_size;
	
	if(joined_nodes != NULL) {
		g_hash_table_destroy(joined_nodes);
		joined_nodes = g_hash_table_new(&g_str_hash, &g_str_equal);
		
	}
	

	for(; members != NULL && lpc < size; lpc++) {
#ifdef CCM_UNAME
		const char *new_node = members[lpc].node_uname;
#else
		const char *new_node = "";
#endif		
		if(strcmp(fsa_our_uname, new_node) == 0) {
			// dont send one to ourselves
			continue;
		}

		CRM_DEBUG3("Sending welcome message to %s (%d)",
			   new_node, was_sent);
		num_sent++;
		was_sent = was_sent
			&& send_request(NULL, NULL, CRM_OPERATION_WELCOME,
					new_node, CRM_SYSTEM_CRMD);
		CRM_DEBUG3("Sent welcome message to %s (%d)",
			   new_node, was_sent);
	}

	if(was_sent == FALSE)
		FNRET(I_FAIL);

/* No point hanging around in S_INTEGRATION if we're the only ones here! */
	if(num_sent == 0) {
		// that was the last outstanding join ack)
		cl_log(LOG_INFO,"That was the last outstanding join ack");
		FNRET(I_SUCCESS);
	} else {
		cl_log(LOG_DEBUG,
		       "Still waiting on %d outstanding join acks",
		       num_sent);
		//dont waste time by invoking the pe yet;
	}
	
	FNRET(I_NULL);
}

/*	 A_JOIN_ACK	*/
enum crmd_fsa_input
do_ack_welcome(long long action,
	    enum crmd_fsa_cause cause,
	    enum crmd_fsa_state cur_state,
	    enum crmd_fsa_input current_input,
	    void *data)
{
	xmlNodePtr welcome = (xmlNodePtr)data;
	FNIN();
	
	/* Once we hear from the DC, we can stop the timer
	 *
	 * This timer was started either on startup or when a node
	 * left the CCM list
	 */
#if 0
	if(we are sick) {
		log error ;
		FNRET(I_NULL);
	} 
#endif

	xmlNodePtr msg_options = find_xml_node(welcome, XML_TAG_OPTIONS);
	
	set_xml_property_copy(msg_options, XML_ATTR_OP, CRM_OPERATION_JOINACK);
	
	send_ha_reply(fsa_cluster_connection,
		      welcome, NULL);
	
	FNRET(I_NULL);
}

/*	 A_ANNOUNCE	*/
enum crmd_fsa_input
do_announce(long long action,
	    enum crmd_fsa_cause cause,
	    enum crmd_fsa_state cur_state,
	    enum crmd_fsa_input current_input,
	    void *data)
{
	FNIN();
	
	/* Once we hear from the DC, we can stop the timer
	 *
	 * This timer was started either on startup or when a node
	 * left the CCM list
	 */
#if 0
	if(we are sick) {
		log error ;
		FNRET(I_NULL);
	} else 
#endif
	if(AM_I_OPERATIONAL) {
		send_request(NULL, NULL, CRM_OPERATION_ANNOUNCE,
			     NULL, CRM_SYSTEM_DC);
	} else {
		/* Delay announce until we have finished local startup */
		FNRET(I_NULL);
	}
	
	FNRET(I_NULL);
}


/*	 A_JOIN_PROCESS_ACK	*/
enum crmd_fsa_input
do_process_welcome_ack(long long action,
		    enum crmd_fsa_cause cause,
		    enum crmd_fsa_state cur_state,
		    enum crmd_fsa_input current_input,
		    void *data)
{
	int lpc = 0, size = 0;
	oc_node_t *members;
	gboolean is_a_member = FALSE;
	xmlNodePtr join_ack = (xmlNodePtr)data;
	const char *join_from = xmlGetProp(join_ack, XML_ATTR_HOSTFROM);
	FNIN();

	
	FNIN();

	members = fsa_membership_copy->members;
	size = fsa_membership_copy->members_size;
	
	for(; lpc < size; lpc++) {
#ifdef CCM_UNAME
		const char *new_node = members[lpc].node_uname;
#else
		const char *new_node = "";
#endif		
		if(strcmp(join_from, new_node) == 0) {
			is_a_member = TRUE;
		}
	}
	
	if(is_a_member == FALSE) {
		cl_log(LOG_ERR, "Node %s is not known to us", join_from);
		FNRET(I_FAIL);
	}
	
	CRM_DEBUG2("Forwarding CIB to %s", join_from);
	
	// add them to our list of "active" nodes
	
	g_hash_table_insert(joined_nodes, strdup(join_from),strdup(join_from));
	
	
	// TODO: clear their "block" in the CIB

	/*
	 * Send a message to the CIB asking what the contents are.
	 *
	 * Forward the ack so that the reply will be directed appropriatly
	 */
	xmlNodePtr cib_copy = get_cib_copy();
	xmlNodePtr frag = create_cib_fragment(cib_copy, "all");

	xmlNodePtr msg_options = create_xml_node(NULL, XML_TAG_OPTIONS);
	set_xml_property_copy(msg_options, XML_ATTR_VERBOSE, "true");

	send_request(msg_options, frag, CRM_OPERATION_STORE,
		     join_from, CRM_SYSTEM_CIB);
	
	if(g_hash_table_size(joined_nodes)
	   == fsa_membership_copy->members_size) {
		// that was the last outstanding join ack)
		cl_log(LOG_INFO,"That was the last outstanding join ack");
		FNRET(I_SUCCESS);
	} else {
		cl_log(LOG_DEBUG,
		       "Still waiting on %d outstanding join acks",
		       size);
		//dont waste time by invoking the pe yet;
	}
	FNRET(I_NULL);
}
