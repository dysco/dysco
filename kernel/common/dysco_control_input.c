/*
 *
 *	Dynamic Service Chaining with Dysco
 *	Dysco kernel agent: dysco_control_input.c
 *
 *	This module implements the Dysco control protocol when packets
 *	arrive at a host.
 *
 *	Author: Ronaldo A. Ferreira (raf@facom.ufms.br)
 *
 *
 *	This program is free software;  you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 *
 *	THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *	WARRANTIES,  INCLUDING,  BUT  NOT   LIMITED  TO,  THE  IMPLIED
 *	WARRANTIES  OF MERCHANTABILITY  AND FITNESS  FOR A  PARTICULAR
 *	PURPOSE  ARE DISCLAIMED.   IN NO  EVENT SHALL  THE AUTHORS  OR
 *	CONTRIBUTORS BE  LIABLE FOR ANY DIRECT,  INDIRECT, INCIDENTAL,
 *	SPECIAL, EXEMPLARY,  OR CONSEQUENTIAL DAMAGES  (INCLUDING, BUT
 *	NOT LIMITED  TO, PROCUREMENT OF SUBSTITUTE  GOODS OR SERVICES;
 *	LOSS  OF  USE, DATA,  OR  PROFITS;  OR BUSINESS  INTERRUPTION)
 *	HOWEVER  CAUSED AND  ON ANY  THEORY OF  LIABILITY, WHETHER  IN
 *	CONTRACT, STRICT  LIABILITY, OR TORT (INCLUDING  NEGLIGENCE OR
 *	OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 *	EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/rcupdate.h>
#include <linux/init.h>
#include <linux/llc.h>
#include <linux/filter.h>
#include <net/llc.h>
#include <net/stp.h>
#include <net/switchdev.h>
#include <net/ip.h>
#include <net/tcp.h>
#include "dysco.h"
/* */


/*********************************************************************
 *
 *	dysco_insert_rcb_control_input:   inserts  a   reconfiguration
 *	control block in its hash table. It saves the information from
 *	the configuration message for possible retransmissions.
 *
 *********************************************************************/
struct
dysco_cb_reconfig *dysco_insert_rcb_control_input(struct dysco_hashes *dh,
						  struct sk_buff *skb,
						  struct control_message *cmsg)
{
	struct iphdr		 *iph = ip_hdr(skb);
	struct dysco_cb_reconfig *rcb;
	
	rcb = kmalloc(sizeof(struct dysco_cb_reconfig), GFP_KERNEL);
	if (rcb == NULL) {
		printk(DYSCO_ALERT
		       "could not allocate memory for rcb at insert_rcb_control_input\n");
		return NULL;
	}

	rcb->super        = cmsg->super;
	rcb->leftSS	  = cmsg->leftSS;
	rcb->rightSS	  = cmsg->rightSS;
	rcb->sub_in.sip   = iph->saddr;
	rcb->sub_in.dip   = iph->daddr;
	rcb->sub_in.sport = cmsg->sport;
	rcb->sub_in.dport = cmsg->dport;
	rcb->sub_out.sip  = 0;
	
	rcb->leftIseq     = ntohl(cmsg->leftIseq); 
	rcb->leftIack     = ntohl(cmsg->leftIack);
	
	rcb->leftIts      = ntohl(cmsg->leftIts);
	rcb->leftItsr	  = ntohl(cmsg->leftItsr);
	
	rcb->leftIws      = ntohs(cmsg->leftIws);
	rcb->leftIwsr     = ntohs(cmsg->leftIwsr);
	
	rcb->sack_ok      = ntohs(cmsg->sackOk);
	
	rhashtable_insert_fast(&dh->dysco_hash_reconfig,
			       &rcb->node,
			       dysco_rhashtable_params_reconfig);
	return rcb;
}
/* dysco_insert_rcb_control_input */


/*********************************************************************
 *
 *	dysco_build_cb_in_reverse: builds an  output control block for
 *	the reverse path.
 *
 *********************************************************************/
struct dysco_cb_out *dysco_build_cb_in_reverse(struct dysco_hashes *dh,
						struct sk_buff *skb,
						struct dysco_cb_reconfig *rcb)
{	
	struct dysco_cb_out	*dcb_out;
	unsigned char		*src_mac;
	struct iphdr		*iph = ip_hdr(skb);
	
	dcb_out = kzalloc(sizeof(struct dysco_cb_out), GFP_KERNEL);
	if (!dcb_out) {
		printk(DYSCO_ALERT "could not allocate memory for dcb_out\n");
		return NULL;
	}
       
	// Insert reverse mapping to output
	// Super session info
	dcb_out->super.sip   = rcb->super.dip;
	dcb_out->super.dip   = rcb->super.sip;
	dcb_out->super.sport = rcb->super.dport;
	dcb_out->super.dport = rcb->super.sport;

	// Sub session info
	dcb_out->sub.sip   = iph->daddr;
	dcb_out->sub.dip   = iph->saddr;
	dcb_out->sub.sport = rcb->sub_in.dport;
	dcb_out->sub.dport = rcb->sub_in.sport;

	// Sequence numbers are equal at left anchor. Translation happens at
	// the right anchor.	
	dcb_out->out_iseq = dcb_out->in_iseq = rcb->leftIack;

	// Ack numbers are equal at left anchor. Translation happens at
	// the right anchor.	
	dcb_out->out_iack = dcb_out->in_iack = rcb->leftIseq;

	spin_lock_init(&dcb_out->seq_lock);
	
	src_mac  = skb_mac_header(skb);
	src_mac += ETHER_ADDR_LEN;
	memcpy(dcb_out->nh_mac, src_mac, ETHER_ADDR_LEN);
	return dcb_out;
}
/* dysco_build_cb_in_reverse */


/*********************************************************************
 *
 *	dysco_compute_deltas_out:  computes the  deltas for  an output
 *	control  block  of  the  variables that  may  change  after  a
 *	reconfiguration:   sequence   and  ack   numbers,   timestamp,
 *	timestamp response, and window sacle.
 *
 *********************************************************************/
void dysco_compute_deltas_out(struct dysco_cb_out *dcb_out,
			      struct dysco_cb_out *old_out,
			      struct dysco_cb_reconfig *rcb)
{
	// In the right anchor, the new out cb receives packets with
	// sequence numbers from the old path and must translate to
	// the sequence numbers of the new path.
	dcb_out->in_iseq = old_out->in_iseq;
	dcb_out->in_iack = old_out->in_iack;
		
	// Save the sequence number delta in the output cb.
	if (dcb_out->in_iseq < dcb_out->out_iseq) {
		dcb_out->seq_delta = dcb_out->out_iseq - dcb_out->in_iseq;
		dcb_out->seq_add   = TRUE;
	}
	else {
		dcb_out->seq_delta = dcb_out->in_iseq - dcb_out->out_iseq;
		dcb_out->seq_add   = FALSE; // Not necessary, as dcb_out is zeroed. Used just for clarity.
	}
		
	// Save the ack delta in the input cb.
	if (dcb_out->in_iack < dcb_out->out_iack) {
		dcb_out->ack_delta = dcb_out->out_iack - dcb_out->in_iack;
		dcb_out->ack_add   = TRUE;
	}
	else {
		dcb_out->ack_delta = dcb_out->in_iack - dcb_out->out_iack;
		dcb_out->ack_add   = FALSE; // Not necessary, as dcb_out is zeroed. Used just for clarity.
	}

	if (rcb->leftIts) {
		dcb_out->ts_ok  = 1;
		dcb_out->ts_in  = old_out->ts_in;
		dcb_out->ts_out = rcb->leftItsr;
		if (dcb_out->ts_in < dcb_out->ts_out) {
			dcb_out->ts_delta = dcb_out->ts_out - dcb_out->ts_in;
			dcb_out->ts_add   = TRUE;
		}
		else {
			dcb_out->ts_delta = dcb_out->ts_in - dcb_out->ts_out;
			dcb_out->ts_add   = FALSE;
		}
		
		dcb_out->tsr_in  = old_out->tsr_in;
		dcb_out->tsr_out = rcb->leftIts;
		if (dcb_out->tsr_in < dcb_out->tsr_out) {
			dcb_out->tsr_delta = dcb_out->tsr_out - dcb_out->tsr_in;
			dcb_out->tsr_add   = TRUE;
		}
		else {
			dcb_out->tsr_delta = dcb_out->tsr_in - dcb_out->tsr_out;
			dcb_out->tsr_add   = FALSE;
		}
		printk(DYSCO_ALERT "OUTPUT: ts_in=%u ts_out=%u tsr_in=%u tsr_out=%u\n",
		       dcb_out->ts_in, dcb_out->ts_out, dcb_out->tsr_in, dcb_out->tsr_out);
	}
	
	if (rcb->leftIwsr) {
		dcb_out->ws_ok  = 1;
		dcb_out->ws_in  = old_out->ws_in;
		dcb_out->ws_out = rcb->leftIwsr;
		if (dcb_out->ws_in < dcb_out->ws_out)
			dcb_out->ws_delta = dcb_out->ws_out - dcb_out->ws_in;
		else
			dcb_out->ws_delta = dcb_out->ws_in - dcb_out->ws_out;
	}
	else
		dcb_out->ws_ok = 0;

	dcb_out->sack_ok = rcb->sack_ok;
}
/* dysco_compute_deltas_out */


/*********************************************************************
 *
 *	dysco_compute_deltas_out:  computes the  deltas for  an  input
 *	control  block  of  the  variables that  may  change  after  a
 *	reconfiguration:   sequence   and  ack   numbers,   timestamp,
 *	timestamp response, and window sacle.
 *
 *********************************************************************/
void dysco_compute_deltas_in(struct dysco_cb_in *dcb_in,
			     struct dysco_cb_out *old_out,
			     struct dysco_cb_reconfig *rcb)
{
	// In the right anchor, the new in cb receives packets with
	// ack number from the new path and must translate to
	// the ack numbers of the old path.
	dcb_in->out_iseq = old_out->in_iack;
	dcb_in->out_iack = old_out->in_iseq;
	
	// Save the sequence number delta in the input cb.
	if (dcb_in->in_iseq < dcb_in->out_iseq) {
		dcb_in->seq_delta = dcb_in->out_iseq - dcb_in->in_iseq;
		dcb_in->seq_add   = TRUE;
	}
	else {
		dcb_in->seq_delta = dcb_in->in_iseq - dcb_in->out_iseq;
		dcb_in->seq_add   =  FALSE;
	}

	// Save the ack delta in the input cb.
	if (dcb_in->in_iack < dcb_in->out_iack) {
		dcb_in->ack_delta = dcb_in->out_iack - dcb_in->in_iack;
		dcb_in->ack_add   = TRUE;
	}
	else {
		dcb_in->ack_delta = dcb_in->in_iack - dcb_in->out_iack;
		dcb_in->ack_add   = FALSE;
	}

	// Save the timestamp delta in the input cb.
	if (rcb->leftIts) {
		dcb_in->ts_ok = 1;
		dcb_in->ts_in  = rcb->leftIts;
		dcb_in->ts_out = old_out->dcb_in->ts_out;
		if (dcb_in->ts_in < dcb_in->ts_out) {
			dcb_in->ts_delta = dcb_in->ts_out - dcb_in->ts_in;
			dcb_in->ts_add   = TRUE;
		}
		else {
			dcb_in->ts_delta = dcb_in->ts_in - dcb_in->ts_out;
			dcb_in->ts_add   = FALSE;
		}

		dcb_in->tsr_in  = rcb->leftItsr;
		dcb_in->tsr_out = old_out->dcb_in->tsr_out;
		if (dcb_in->tsr_in < dcb_in->tsr_out) {
			dcb_in->tsr_delta = dcb_in->tsr_out - dcb_in->tsr_in;
			dcb_in->tsr_add   = TRUE;
		}
		else {
			dcb_in->tsr_delta = dcb_in->tsr_in - dcb_in->tsr_out;
			dcb_in->tsr_add   = FALSE;
		}

		printk(DYSCO_ALERT "INPUT: ts_in=%u ts_out=%u tsr_in=%u tsr_out=%u\n",
		       dcb_in->ts_in, dcb_in->ts_out, dcb_in->tsr_in, dcb_in->tsr_out);
	}
	else
		dcb_in->ts_ok = 0;
	
	// Save the window scaling delta in the input cb.
	if (rcb->leftIws) {
		dcb_in->ws_ok = 1;
		dcb_in->ws_in  = rcb->leftIws;
		dcb_in->ws_out = old_out->ws_in;
		if (dcb_in->ws_in < dcb_in->ws_out)
			dcb_in->ws_delta  = dcb_in->ws_out - dcb_in->ws_in;
		else 
			dcb_in->ws_delta = dcb_in->ws_in - dcb_in->ws_out;
	}
	else
		dcb_in->ws_ok = 0;

	dcb_in->sack_ok = rcb->sack_ok;
}
/* dysco_compute_deltas_in */


/*********************************************************************
 *
 *	dysco_control_config_rightA:   performs  the   reconfiguration
 *	actions that are specific to the right anchor.
 *
 *********************************************************************/
int dysco_control_config_rightA(struct dysco_hashes *dh,
				struct dysco_cb_reconfig *rcb,
				struct control_message *cmsg,
				struct dysco_cb_in *dcb_in,
				struct dysco_cb_out *dcb_out)
				
{
	struct dysco_cb_out	*old_out;
	struct tcp_session	local_ss;
		
	//printk(DYSCO_ALERT "In the right anchor at dysco_contol_reconfig_in\n");
		
	local_ss.sip = cmsg->rightSS.dip;
	local_ss.dip = cmsg->rightSS.sip;
	local_ss.sport = cmsg->rightSS.dport;
	local_ss.dport = cmsg->rightSS.sport;
		
	old_out = rhashtable_lookup_fast(&dh->dysco_hash_out,
					 &local_ss,
					 dysco_rhashtable_params_out);
	if (old_out == NULL) {
		printk(DYSCO_ALERT
		       "BUG: dcb_out is null in ctl_reconfig_in"
		       " sip=" IP_STR " dip=" IP_STR " sport=%u dport=%u\n",
		       IP_TO_STR(local_ss.sip), IP_TO_STR(local_ss.dip),
		       ntohs(local_ss.sport), ntohs(local_ss.dport));
		kfree(dcb_in);
		rhashtable_remove_fast(&dh->dysco_hash_reconfig, &rcb->node,
				       dysco_rhashtable_params_reconfig);
		return 0;
	}
				
	dcb_in->super = cmsg->rightSS;

	dysco_compute_deltas_in(dcb_in, old_out, rcb);
	
	dysco_compute_deltas_out(dcb_out, old_out, rcb);
	
	dcb_in->two_paths = TRUE;
	
	rcb->old_dcb = old_out;
	rcb->new_dcb = dcb_out;
	
	dcb_out->other_path = old_out;
		
	if (ntohs(cmsg->semantic) == STATE_TRANSFER) {
		// printk(DYSCO_ALERT "setting state_t to TRUE in dysco_control_reconfig_in\n");
		old_out->state_t = TRUE;
	}
	return 1;
}
/* dysco_control_config_rightA */


/*********************************************************************
 *
 *	dysco_control_reconfig_in: allocates input  and output control
 *	blocks for the new session and sets their parameters variables
 *	from  the reconfiguration  control block.  The reconfiguration
 *	control block was initialy built from the control message.
 *
 *********************************************************************/
int dysco_control_reconfig_in(struct dysco_hashes *dh,
			       struct sk_buff *skb,
			       struct dysco_cb_reconfig *rcb,
			       struct control_message *cmsg)
{
	struct dysco_cb_in	*dcb_in;
	struct dysco_cb_out	*dcb_out;
	
	dcb_in = kmalloc(sizeof(struct dysco_cb_in), GFP_KERNEL);
	if (!dcb_in) {
		printk(DYSCO_ALERT "could not allocate memory for dcb_in\n");
		rhashtable_remove_fast(&dh->dysco_hash_reconfig, &rcb->node,
				       dysco_rhashtable_params_reconfig);
		return 0;
	}
	
	// Insert input mapping
	dcb_in->sub       = rcb->sub_in;
	dcb_in->in_iseq   = rcb->leftIseq;
	dcb_in->in_iack   = rcb->leftIack;
	
	dcb_in->two_paths = FALSE;
	dcb_in->skb_iif   = skb->skb_iif;
	
	dcb_out = dysco_build_cb_in_reverse(dh, skb, rcb);
	if (dcb_out == NULL) {
		kfree(dcb_in);
		rhashtable_remove_fast(&dh->dysco_hash_reconfig, &rcb->node,
				       dysco_rhashtable_params_reconfig);
		return 0;
	}
	
	dcb_out->dcb_in = dcb_in;
	dcb_in->dcb_out = dcb_out;
	
	if (ip_hdr(skb)->daddr == cmsg->rightA) {
		if (!dysco_control_config_rightA(dh, rcb, cmsg, dcb_in, dcb_out))
			return 0;
	}
	else {
		dcb_in->super     = rcb->super;
		dcb_in->out_iseq  = rcb->leftIseq;
		dcb_in->out_iack  = rcb->leftIack;
		dcb_in->seq_delta = dcb_in->ack_delta = 0;

		// timestamp different from 0 means it was present at the syn packet.
		if (rcb->leftIts) {
			dcb_in->ts_in    = dcb_in->ts_out = rcb->leftIts;
			dcb_in->ts_delta = 0;
			dcb_in->ts_ok    = 1;
		}
		else
			dcb_in->ts_ok = 0;

		// ws different from 0 means it was present at the syn packet.
		if (rcb->leftIws) {
			dcb_in->ws_in    = dcb_in->ws_out = rcb->leftIws;
			dcb_in->ws_delta = 0;
			dcb_in->ws_ok    = 1;
		}
		else
			dcb_in->ws_ok = 0;

		dcb_out->sack_ok = dcb_in->sack_ok = rcb->sack_ok;
		
		// If it is not the right anchor, allocate state for the
		// data path in the reverse path. The right anchor will
		// allocate state when it receives an ACK and switches
		// to the new path. FIXME: Wrong description.
		// Insert reverse path.
		rhashtable_insert_fast(&dh->dysco_hash_out,
				       &dcb_out->node,
				       dysco_rhashtable_params_out);
	}
	rhashtable_insert_fast(&dh->dysco_hash_in,
			       &dcb_in->node,
			       dysco_rhashtable_params_in);
	return 1;
}
/* dysco_control_reconfig_in */


/*********************************************************************
 *
 *	dysco_control_input: processes  UDP control packets  when they
 *	enter a host.
 *
 *********************************************************************/
rx_handler_result_t dysco_control_input(struct dysco_hashes *dh, struct sk_buff *skb)
{
	struct iphdr			*iph;
	struct udphdr			*uh;
	unsigned char			*data;
	struct control_message		*cmsg;
	struct dysco_cb_reconfig	*rcb;
	__u16				udp_len;
	struct service_chain		*sc;

	iph   = ip_hdr(skb);
	data  = (unsigned char *)iph;
	data += iph->ihl << 2;	
	uh    = (struct udphdr *)data;
	if (!(uh->dest == dysco_control_port || uh->source == dysco_control_port))
		return 1;
	
	udp_len = ntohs(uh->len);
	if (udp_len < UDP_HDR_LEN+RUDP_HDR_LEN+sizeof(struct control_message)) {
		return 1;
	}

	// RUDP_HDR_LEN is the size of the header of reliable-udp
	data  = (unsigned char *)uh;
	data += sizeof(struct udphdr) + RUDP_HDR_LEN; 
	cmsg  = (struct control_message *)data;
	sc = &cmsg->sc[0];

	switch(cmsg->mtype) {
	case DYSCO_SYN:
		// printk(DYSCO_ALERT "received a DYSCO_SYN message semantic=%d\n", ntohs(cmsg->semantic));
		rcb = rhashtable_lookup_fast(&dh->dysco_hash_reconfig,
					     &cmsg->super,
					     dysco_rhashtable_params_reconfig);
		if (rcb)
			return 1; // This is a retransmission
		
		rcb = dysco_insert_rcb_control_input(dh, skb, cmsg);
		if (!rcb)			
			return 0; // Error. Did not allocate memory

		return dysco_control_reconfig_in(dh, skb, rcb, cmsg);

	case DYSCO_SYN_ACK:
		// printk(DYSCO_ALERT "received a DYSCO_SYN_ACK message semantic=%d\n", ntohs(cmsg->semantic));
		if (iph->daddr == cmsg->leftA) {
			// It is the left anchor.
			struct dysco_cb_out *dcb_out;

			/*
			printk(DYSCO_ALERT
			       "DYSCO_SYN_ACK at the left anchor. It will change the state to ESTABLISHED\n");
			*/
			dcb_out = rhashtable_lookup_fast(&dh->dysco_hash_out,
							 &cmsg->leftSS, 
							 dysco_rhashtable_params_out);
			if (!dcb_out) {
				printk(DYSCO_ALERT "BUG: no out CB at left anchor\n");
				return 1;
			}

			// Begin of critical section
			spin_lock_bh(&dcb_out->seq_lock);			
			if (dcb_out->state == DYSCO_ESTABLISHED) {
				// It is a retransmission
				spin_unlock_bh(&dcb_out->seq_lock);
				break;
			}			
			dcb_out->ack_cutoff    = ntohl(cmsg->seqCutoff);
			dcb_out->valid_ack_cut = TRUE;
			spin_unlock_bh(&dcb_out->seq_lock);
			// End of critical section
		}
		break;
		
	case DYSCO_ACK:

		// Check if it is the right anchor. If yes, switch to the new path.
		// printk(DYSCO_ALERT "received a DYSCO_ACK message semantic=%d\n", ntohs(cmsg->semantic));
		rcb = rhashtable_lookup_fast(&dh->dysco_hash_reconfig,
					     &cmsg->super,
					     dysco_rhashtable_params_reconfig);
		if (!rcb) {
			printk(DYSCO_ALERT
			       "BUG: reconfig CB is NULL at DYSCO_ACK\n");
			break;
		}
		
		if (cmsg->rightA == iph->daddr) {
			struct dysco_cb_out	*old_out, *new_out;
			__u32 old_out_ack_cutoff;
			
			if (rcb->old_dcb == NULL) {
				printk(DYSCO_ALERT
				       "dcb->old_dcb is NULL in input DYSCO_ACK\n");
				break;
			}
			old_out = rcb->old_dcb;
			
			/*
			  if (ntohs(cmsg->semantic) == STATE_TRANSFER)
				return 1;
			*/

			if (old_out->other_path == NULL) {
				printk(DYSCO_ALERT
				       "dysco_out->other_path is NULL in DYSCO_ACK in dysco_control_input\n");
				break;
			}
			
			new_out = old_out->other_path;

			old_out_ack_cutoff = ntohl(cmsg->seqCutoff);
			if (new_out->in_iack < new_out->out_iack) {
				__u32 delta;

				delta = new_out->out_iack - new_out->in_iack;
				old_out_ack_cutoff -= delta;
			}
			else {
				__u32 delta;
				
				delta = new_out->in_iack - new_out->out_iack;
				old_out_ack_cutoff += delta;
			}

			// Begin of critical section
			spin_lock_bh(&old_out->seq_lock);
			if (old_out->state == DYSCO_ESTABLISHED) {
				// It is a retransmission
				spin_unlock_bh(&old_out->seq_lock);
				//printk(DYSCO_ALERT "received a retransmitted DYSCO_ACK dcb=%u msg=%u\n",
				//       old_out->ack_cutoff, ntohl(cmsg->seqCutoff));
				return 1; 
			}

			if (!old_out->state_t) {
				//printk(DYSCO_ALERT "setting state to DYSCO_ESTABLISHED in DYSCO_ACK\n");
				old_out->ack_cutoff = old_out_ack_cutoff;
				// Now, it can Send ack in the new path.
				old_out->valid_ack_cut = TRUE;
				old_out->state = DYSCO_ESTABLISHED;
			}
			spin_unlock_bh(&old_out->seq_lock);			
			// End of critical section
		}
		break;

	case DYSCO_FIN:
		//printk(DYSCO_ALERT "received a DYSCO_FIN message\n");
		break;

	case DYSCO_STATE_TRANSFERRED:
		// printk(DYSCO_ALERT "received a DYSCO_STATE_TRANSFERRED message\n");
		rcb = rhashtable_lookup_fast(&dh->dysco_hash_reconfig,
					     &cmsg->super,
					     dysco_rhashtable_params_reconfig);		
		if (rcb == NULL) {
			printk(DYSCO_ALERT
			       "BUG: lookup in DYSCO_STATE_TRANSFERRED returned NULL\n");
			return 1;
		}
		if (iph->daddr == cmsg->leftA) {
			// get the current dcb for the supersession and replace with the new one.
			//printk(DYSCO_ALERT "replacing cb_leftA\n");
			printk(DYSCO_ALERT "received a DYSCO_STATE_TRANSFERRED message on the LEFT anchor\n");

			dysco_replace_cb_leftA(skb, rcb, cmsg);
		}		
		else if (iph->daddr == cmsg->rightA) {
			struct dysco_cb_out *dcb_out;
			printk(DYSCO_ALERT "received a DYSCO_STATE_TRANSFERRED message on the RIGHT anchor\n");
			
			dcb_out = rcb->old_dcb;

			// Begin of critical section
			spin_lock_bh(&dcb_out->seq_lock);
			dcb_out->state = DYSCO_ESTABLISHED;
			spin_unlock_bh(&dcb_out->seq_lock);
			// End of critical section
		}
		else
			printk(DYSCO_ALERT "BUG: I am not the left anchor");
		break;

	default:
		printk(DYSCO_ALERT "BUG: received an UNKNOWN control message");
	}

	skb->csum       = 0;
	skb->csum_valid = 1;
	skb->ip_summed  = CHECKSUM_UNNECESSARY;
	return 1;
}
/* dysco_control_input */
