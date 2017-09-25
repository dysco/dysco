/*
 *
 *	Dynamic Service Chaining with Dysco
 *	Dysco kernel agent: dysco_input.c
 *
 *	This module implements the Dysco data path when packets arrive
 *	at a host.
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
#include <linux/init.h>
#include <linux/llc.h>
#include <linux/filter.h>
#include <net/llc.h>
#include <net/stp.h>
#include <net/switchdev.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/netns/hash.h>
#include <linux/timekeeping.h>
#include "dysco.h"
/* */


/*********************************************************************
 *
 *	dysco_remove_sc: removes  the service  chain from the  TCP syn
 *	payload.
 *
 *********************************************************************/
static inline void dysco_remove_sc(struct sk_buff *skb,
				   struct iphdr *iph,
				   struct tcphdr *th)
{
	int sc_memsz = sizeof(struct tcp_session);
	struct syn_packet *sp;
	unsigned char *data;

	data = (unsigned char *)th;
	data += th->doff << 2;
	sp = (struct syn_packet *)data;
	sc_memsz += SC_MEM_SZ(&sp->sc);
	skb_trim(skb, skb->len - sc_memsz);
	
	/* fix ip header: total length has changed. */
	iph->tot_len = htons(skb->len);
}
/* dysco_remove_sc */


/*********************************************************************
 *
 *	dysco_insert_pending: inserts  an output control block  in the
 *	pending hash tables.
 *
 *********************************************************************/
int dysco_insert_pending(struct dysco_hashes *dh, struct syn_packet *sp)
{
	int			sc_mem_sz;	
	struct dysco_cb_out *dcb_out;
	
	dcb_out = kzalloc(sizeof(struct dysco_cb_out), GFP_ATOMIC);
	if (dcb_out == NULL) {
		printk(DYSCO_ALERT "could not allocate memory in insert_cb_output\n");
		return 0;
	}
	
	dcb_out->super.sip   = sp->ss.sip;
	dcb_out->super.dip   = sp->ss.dip;
	dcb_out->super.sport = sp->ss.sport;
	dcb_out->super.dport = sp->ss.dport;
	// sub session will be allocated at output
		
	sc_mem_sz = sizeof(struct service_chain) + (sp->sc.len-1)*sizeof(struct next_hop);
	dcb_out->sc = kmalloc(sc_mem_sz, GFP_ATOMIC);
	if (dcb_out->sc == NULL) {
		printk(DYSCO_ALERT "could no allocate memory in inser_cb_output 2 dcb_out->sc\n");
		kfree(dcb_out);
		return 0;
	}
	
	dcb_out->sc->len = sp->sc.len-1;
	dcb_out->free_sc = TRUE;
		
	spin_lock_init(&dcb_out->seq_lock);
		
	memcpy(&dcb_out->sc->hops[0], &sp->sc.hops[1],
	       dcb_out->sc->len * sizeof(struct next_hop));
	
	// Insert in the pendind table with the five-tuple as key
	rhashtable_insert_fast(&dh->dysco_hash_pen, &dcb_out->node,
			       dysco_rhashtable_params_out);

	// Insert in the pending table with the Dysco tag as key
	rhashtable_insert_fast(&dh->dysco_hash_pen_tag, &dcb_out->node,
			       dysco_rhashtable_params_pen);
	return 1;
}
/* dysco_insert_pending */


/*********************************************************************
 *
 *	dysco_insert_cb_in_reverse:  creates an  output control  block
 *	with the five-tuple information reversed.
 *
 *********************************************************************/
struct dysco_cb_out *
dysco_insert_cb_in_reverse(struct dysco_hashes *dh, struct sk_buff *skb,
			   struct syn_packet *sp, struct iphdr *iph,
			   struct tcphdr *th, unsigned char *mac)
{
	struct dysco_cb_out *dcb_out;
	
	// Create and insert reverse path mapping to output
	dcb_out = kzalloc(sizeof(struct dysco_cb_out), GFP_ATOMIC);
	if (dcb_out == NULL) {
		printk(DYSCO_ALERT
		       "could not allocate memory for dcb_out in dysco_insert_cb_in_reverse\n");
		return NULL;
	}
	
	dcb_out->super.sip   = sp->ss.dip;
	dcb_out->super.dip   = sp->ss.sip;
	dcb_out->super.sport = sp->ss.dport;
	dcb_out->super.dport = sp->ss.sport;
	
	dcb_out->sub.sip   = iph->daddr;
	dcb_out->sub.dip   = iph->saddr;
	dcb_out->sub.sport = th->dest;
	dcb_out->sub.dport = th->source;

	// Save the initial sequence number in the input as
	// the initial ack number in the output.
	dcb_out->in_iack  = ntohl(th->seq);
	dcb_out->out_iack = ntohl(th->seq);

	dcb_out->other_path    = NULL;
	dcb_out->old_path      = FALSE;
	dcb_out->valid_ack_cut = FALSE;
	dcb_out->use_np_seq    = FALSE;
	dcb_out->use_np_ack    = FALSE;
	dcb_out->ack_cutoff    = 0;
	
	dcb_out->ack_ctr = 0;
	
	dcb_out->state = DYSCO_ONE_PATH;
	
	spin_lock_init(&dcb_out->seq_lock);
	
	memcpy(dcb_out->nh_mac, mac, ETHER_ADDR_LEN);
	
	return dcb_out;
}
/* */


/*********************************************************************
 *
 *	dysco_insert_cb_input: inserts  a control  block in  the input
 *	hash table.  It also calls  the function to insert  the output
 *	control block for the reverse  path and the function to insert
 *	in the pending hash table.
 *
 *********************************************************************/
struct dysco_cb_in *
dysco_insert_cb_input(struct dysco_hashes *dh, struct sk_buff *skb,
		      struct syn_packet *sp, struct iphdr *iph,
		      struct tcphdr *th, unsigned char *mac)
{
	struct dysco_cb_in	*dcb_in;
	struct dysco_cb_out	*dcb_out;

	dcb_in = kzalloc(sizeof(struct dysco_cb_in), GFP_ATOMIC);
	if (dcb_in == NULL) {
		printk(DYSCO_ALERT
		       "could not allocate memory for dcb_in in dysco_insert_cb_input\n");
		return NULL;
	}

	// Insert input mapping
	dcb_in->sub.sip   = iph->saddr;
	dcb_in->sub.dip   = iph->daddr;
	dcb_in->sub.sport = th->source;
	dcb_in->sub.dport = th->dest;
	
	dcb_in->super.sip   = sp->ss.sip;
	dcb_in->super.dip   = sp->ss.dip;
	dcb_in->super.sport = sp->ss.sport;
	dcb_in->super.dport = sp->ss.dport;

	dcb_in->two_paths = FALSE;
	dcb_in->skb_iif = skb->skb_iif;

	dcb_in->seq_delta = dcb_in->ack_delta = 0;
	
	dcb_out = dysco_insert_cb_in_reverse(dh, skb, sp, iph, th, mac);
	if (dcb_out == NULL) {
		kfree(dcb_in);
		return NULL;		
	}
	
	// Insert forward path in pending
	if (sp->sc.len > 1) {
		if (!dysco_insert_pending(dh, sp)) {
			kfree(dcb_in);
			kfree(dcb_out);
			return NULL;
		}
	}
	
	// Connect the input and output control blocks of the same interface
	dcb_in->dcb_out = dcb_out;
	dcb_out->dcb_in = dcb_in;
	
	rhashtable_insert_fast(&dh->dysco_hash_in, &dcb_in->node,
			       dysco_rhashtable_params_in);
	
	rhashtable_insert_fast(&dh->dysco_hash_out, &dcb_out->node,
			       dysco_rhashtable_params_out);
	
	return dcb_in;
}
/* dysco_insert_cb_input */


/*********************************************************************
 *
 *	dysco_lookup_input: lookups  up in the input  hash table using
 *	the five-tuple as the key.
 *
 *********************************************************************/
static inline struct dysco_cb_in *
dysco_lookup_input(struct dysco_hashes *dh, struct sk_buff *skb,
		   struct iphdr *iph, struct tcphdr *th)
{
	struct tcp_session local_sub;
	
	local_sub.sip   = iph->saddr;
	local_sub.dip   = iph->daddr;
	local_sub.sport = th->source;
	local_sub.dport = th->dest;
	
	return rhashtable_lookup_fast(&dh->dysco_hash_in, &local_sub,
				      dysco_rhashtable_params_in);
}
/* dysco_lookup_input */


/*********************************************************************
 *
 *	dysco_in_hdr_rewrite:  rewrites  the  IP  addresses  and  port
 *	numbers,  and  recomputes  the   checksums  from  scratch  (if
 *	checksum offload is not on).
 *
 *********************************************************************/
static inline void dysco_in_hdr_rewrite(struct sk_buff *skb,
					struct iphdr *iph,
					struct tcphdr *th,
					struct dysco_cb_in *dcb_in)
{
	int	ip_tot_len, iphdr_len;
	__wsum	csum;

	ip_tot_len = ntohs(iph->tot_len);
	iphdr_len  = iph->ihl << 2;

	iph->daddr = dcb_in->super.dip;
	iph->saddr = dcb_in->super.sip;
	th->source = dcb_in->super.sport;
	th->dest   = dcb_in->super.dport;

	// IP checksum computation
	iph->check = 0;
	iph->check = ip_fast_csum(iph, iph->ihl);
		
	if (skb->ip_summed != CHECKSUM_UNNECESSARY) {
		// TCP checksum computation
		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			skb->ip_summed  = CHECKSUM_UNNECESSARY; // This is just for Linux containers.
		}
		else {
			th->check  = 0;
			csum = csum_partial(th, ip_tot_len - iphdr_len,
					    csum_tcpudp_nofold(iph->saddr, iph->daddr,
							       ip_tot_len - iphdr_len,
							       IPPROTO_TCP, 0));
			th->check = csum_fold(csum);
		}
	}
}
/* dysco_in_hdr_rewrite */


/*********************************************************************
 *
 *	dysco_in_rewrite_seq:  rewrites the  sequence  number after  a
 *	reconfiguration to  account for initial sequence  numbers that
 *	differ in two TCP sessions.
 *
 *********************************************************************/
static inline void dysco_in_rewrite_seq(struct sk_buff *skb,
					struct dysco_cb_in *dcb_in,
					struct tcphdr *th)
{
	if (dcb_in->seq_delta) {
		__u32 new_seq;
		__u32 seq = ntohl(th->seq);
		
		if (dcb_in->seq_add)
			new_seq = htonl(seq + dcb_in->seq_delta);
		else
			new_seq = htonl(seq - dcb_in->seq_delta);
		
		dysco_inet_proto_csum_replace4(&th->check, skb, th->seq,
					       new_seq, FALSE);
		th->seq = new_seq;
	}
}
/* dysco_in_rewrite_seq */


/*********************************************************************
 *
 *	dysco_in_rewrite_ack:   rewrites  the   ack  number   after  a
 *	reconfiguration to  account for initial sequence  numbers that
 *	differ in two TCP sessions.
 *
 *********************************************************************/
static inline void dysco_in_rewrite_ack(struct sk_buff *skb,
					struct dysco_cb_in *dcb_in,
					struct tcphdr *th)
{
	if (dcb_in->ack_delta) {
		__u32 new_ack;
		__u32 ack = ntohl(th->ack_seq);

		if (dcb_in->ack_add)
			new_ack = htonl(ack + dcb_in->ack_delta);
		else
			new_ack = htonl(ack - dcb_in->ack_delta);

		if (dcb_in->sack_ok)
			dysco_tcp_sack(skb, th, dcb_in->ack_delta, dcb_in->ack_add);
		
		dysco_inet_proto_csum_replace4(&th->check, skb, th->ack_seq,
					       new_ack, FALSE);
		th->ack_seq = new_ack;
	}
}
/* dysco_in_rewrite_ack */


/*********************************************************************
 *
 *	dysco_out_rewrite_ts: rewrites the TCP timestamp option.
 *
 *********************************************************************/
static inline void dysco_in_rewrite_ts(struct sk_buff *skb,
				       struct dysco_cb_in *dcb_in,
				       struct tcphdr *th)
{
	struct tcp_ts *ts;
	__u32 new_ts, new_tsr;
	
	ts = (struct tcp_ts *)dysco_get_ts_option(th);
	if (ts == NULL)
		return;

	if (dcb_in->ts_delta) {
		//printk(DYSCO_ALERT "Rewriting timestamp INPUT delta=%u in=%u out=%u.\n",
		//       dcb_in->ts_delta, dcb_in->ts_in, dcb_in->ts_out);
		if (dcb_in->ts_add)
			new_ts = ntohl(ts->ts) + dcb_in->ts_delta;
		else
			new_ts = ntohl(ts->ts) - dcb_in->ts_delta;
		
		new_ts = htonl(new_ts);
		dysco_inet_proto_csum_replace4(&th->check, skb, ts->ts, new_ts, FALSE);
		ts->ts  = new_ts;
		tcp_ts_rewrites++;
	}

	if (dcb_in->tsr_delta) {	
		//printk(DYSCO_ALERT "Rewriting TSR INPUT delta=%u in=%u out=%u.\n",
		//       dcb_in->tsr_delta, dcb_in->tsr_in, dcb_in->tsr_out);
		if (dcb_in->tsr_add)
			new_tsr = ntohl(ts->tsr) + dcb_in->tsr_delta;
		else
			new_tsr = ntohl(ts->tsr) - dcb_in->tsr_delta;
		
		new_tsr = htonl(new_tsr);		
		dysco_inet_proto_csum_replace4(&th->check, skb, ts->tsr, new_tsr, FALSE);
		ts->tsr = new_tsr;
		tcp_tsr_rewrites++;
	}
}
/* dysco_in_rewrite_ts */


/*********************************************************************
 *
 *	dysco_in_rewrite_rcv_wnd: rewrites  the receiver  window that
 *	is being advertised.
 *
 *********************************************************************/
static inline void dysco_in_rewrite_rcv_wnd(struct sk_buff *skb,
					    struct dysco_cb_in *dcb_in,
					    struct tcphdr *th)
{
	if (dcb_in->ws_delta) {
		__u16 new_win;
		__u32 wnd  = ntohs(th->window);

		printk(DYSCO_ALERT "Rewriting WS INPUT in=%u out=%u\n",
		       dcb_in->ws_in, dcb_in->ws_out);
		wnd <<= dcb_in->ws_in;
		wnd >>= dcb_in->ws_out;
		new_win = htons(wnd);
		inet_proto_csum_replace2(&th->check, skb, th->window, new_win, FALSE);
		th->window = new_win;
	}
}
/* dysco_in_rewrite_rcv_wnd */


/*********************************************************************
 *
 *	dysco_in_hdr_rewrite_csum:  rewrites  IP  addresses  and  port
 *	numbers, and recomputes the checksums.
 *
 *********************************************************************/
static inline void dysco_in_hdr_rewrite_csum(struct sk_buff *skb,
					     struct iphdr *iph,
					     struct tcphdr *th,
					     struct dysco_cb_in *dcb_in)
{
	unsigned int from, to;

	// Add IP addresses in the original packet
	from  = iph->saddr;
	from += iph->daddr;
	from += (iph->daddr > from);  // add carry

	// Add the new IP addresses 
	to  = dcb_in->super.sip;
	to += dcb_in->super.dip;
	to += (dcb_in->sub.dip > to); // add carry

	dysco_csum_replace4(&iph->check, from, to);
	iph->saddr = dcb_in->super.sip;
	iph->daddr = dcb_in->super.dip;

	inet_proto_csum_replace4(&th->check, skb, from, to, TRUE);
	
	inet_proto_csum_replace2(&th->check, skb, th->source, dcb_in->super.sport, FALSE);
	th->source = dcb_in->super.sport;
	
	inet_proto_csum_replace2(&th->check, skb, th->dest, dcb_in->super.dport, FALSE);
	th->dest   = dcb_in->super.dport;
	
	dysco_in_rewrite_seq(skb, dcb_in, th);
	dysco_in_rewrite_ack(skb, dcb_in, th);

	if (dcb_in->ts_ok)
		dysco_in_rewrite_ts(skb, dcb_in, th);
	
	if (dcb_in->ws_ok)
		dysco_in_rewrite_rcv_wnd(skb, dcb_in, th);
}
/* dysco_in_hdr_rewrite_csum */


/*********************************************************************
 *
 *	dysco_rx_initiation_new:   processes   a    syn   packet   and
 *	initializes the data structure for the TCP session.
 *
 *********************************************************************/
static inline rx_handler_result_t dysco_rx_initiation_new(struct dysco_hashes *dh,
							  struct sk_buff *skb,
							  struct iphdr *iph,
							  struct tcphdr *th)
{
	int tcp_len;
	
	tcp_len = ntohs(iph->tot_len) - (iph->ihl << 2);
	if (tcp_len > (th->doff << 2)) {
		struct syn_packet	*sp;
		struct dysco_cb_in	*dcb_in;		
		unsigned char		*data;
		unsigned char		*src_mac;

		src_mac  = skb_mac_header(skb);
		src_mac += ETHER_ADDR_LEN;
		data = (unsigned char *)th;		
		data += th->doff << 2;
		sp = (struct syn_packet *)data;
		if ((dcb_in = dysco_insert_cb_input(dh, skb, sp, iph, th, src_mac)) == NULL) {
			kfree_skb(skb);
			return RX_HANDLER_CONSUMED;
		}
		
		dysco_remove_sc(skb, iph, th);
		dysco_parse_tcp_syn_opt_r(th, dcb_in);
		dysco_insert_tag(dh, skb, iph, th, dcb_in);
		dysco_in_hdr_rewrite(skb, iph, th, dcb_in);
	}	
	return RX_HANDLER_PASS;
}
/* dysco_rx_initiation_new */


/*********************************************************************
 *
 *	dysco_set_ack_number_out: records the initial ack and sequence
 *	numbers in the output control block.
 *
 *********************************************************************/
static inline void dysco_set_ack_number_out(struct dysco_hashes *dh,
					    struct tcphdr *th,
					    struct dysco_cb_in *dcb_in)
{ 
	struct dysco_cb_out *dcb_out;
	struct tcp_session local_ss;

	dcb_in->in_iseq = dcb_in->out_iseq = ntohl(th->seq);
	dcb_in->in_iack = dcb_in->out_iack = ntohl(th->ack_seq)-1;
	
	dcb_in->seq_delta = dcb_in->ack_delta = 0;
	
	local_ss.sip   = dcb_in->super.dip;
	local_ss.dip   = dcb_in->super.sip;
	local_ss.sport = dcb_in->super.dport;
	local_ss.dport = dcb_in->super.sport;
	
	dcb_out = rhashtable_lookup_fast(&dh->dysco_hash_out, &local_ss,
					 dysco_rhashtable_params_out);
	if (dcb_out == NULL) {
		printk(DYSCO_ALERT
		       "BUG: cannot find output CB for an ongoing session\n");
		return;
	}
	
	// Set the initial seq and ack number of the output cb.
	// No need to set the delta, as dcb_out was zeroed.	
	dcb_out->out_iack = dcb_out->in_iack = ntohl(th->seq);	
	dcb_out->out_iseq = dcb_out->in_iseq = ntohl(th->ack_seq)-1;

	dysco_parse_tcp_syn_opt_r(th, dcb_in);
	if (dcb_in->ts_ok) {
		dcb_out->ts_ok = 1;
		
		dcb_out->tsr_out = dcb_out->tsr_in = dcb_in->ts_in;
		dcb_out->ts_out  = dcb_out->ts_in  = dcb_in->tsr_in;

		dcb_out->ts_delta = dcb_out->tsr_delta = 0;
		
		printk(DYSCO_ALERT "INPUT SYN+ACK: ts=%u tsr=%u\n",
		       dcb_in->ts_in, dcb_in->tsr_in);
		
	}
	if (!dcb_in->sack_ok) {
		// The server side does not accept the sack option.
		dcb_out->sack_ok = 0;
	}
}
/* dysco_set_ack_number_out */


/*********************************************************************
 *
 *	dysco_set_zero_window: set the TCP advertise window to zero.
 *
 *********************************************************************/
static inline void dysco_set_zero_window(struct sk_buff *skb, struct tcphdr *th) {
	inet_proto_csum_replace2(&th->check, skb, th->window, 0, 0);
	th->window = 0;
}
/* dysco_set_zero_window */


/*********************************************************************
 *
 *	dysco_in_two_paths_ack:  handles ack  segments when  there are
 *	two active paths.
 *
 *********************************************************************/
static inline void dysco_in_two_paths_ack(struct dysco_hashes *dh,
					  struct sk_buff *skb,
					  struct dysco_cb_in *dcb_in,
					  struct tcphdr *th)
	
{
	struct dysco_cb_out *dcb_out;
	__u32 ack_seq = ntohl(th->ack_seq);
		
	
	dcb_out = dcb_in->dcb_out;
	if (dcb_out == NULL) {
		printk(DYSCO_ALERT "BUG: dcb_out is NULL in dysco_in_two_paths_ack\n");
		return;
	}
	if (dcb_out->old_path) {
		if (dcb_out->state_t) {
			
			if (dcb_out->state == DYSCO_ESTABLISHED) {
				// printk(DYSCO_ALERT "setting two_paths to FALSE in dysco input OLD path\n");
				dcb_in->two_paths = FALSE;
			}
		}
		else {
			// Begin of critical section
			spin_lock_bh(&dcb_out->seq_lock);
			if (!after(dcb_out->seq_cutoff, ack_seq)) {
				dcb_out->use_np_seq = TRUE;
				dcb_in->two_paths   = FALSE;
				spin_unlock_bh(&dcb_out->seq_lock);
			}
			else {
				spin_unlock_bh(&dcb_out->seq_lock);			
				//dysco_set_zero_window(skb, th);
			}
			// End of critical section
		}
	}
	else {
		// Received on the new path.
		dcb_out = dcb_out->other_path;
		if (dcb_out == NULL) {
			printk(DYSCO_ALERT "BUG: dcb_out (new path) is NULL\n");
			return;
		}
		if (dcb_out->state_t &&dcb_out->state == DYSCO_ESTABLISHED) {
			dcb_in->two_paths = FALSE;
		}
		else {
			// Begin of critical section
			spin_lock_bh(&dcb_out->seq_lock);
			if (!after(dcb_out->seq_cutoff, ack_seq)) {
				dcb_out->use_np_seq = TRUE;
				dcb_in->two_paths = FALSE;
			}
			spin_unlock_bh(&dcb_out->seq_lock);
			// End of critical section
		}
	}
}
/* dysco_in_two_paths_ack */


/*********************************************************************
 *
 *	dysco_in_two_paths_data_seg: handles  data segment  when there
 *	are two active paths.
 *
 *********************************************************************/
static inline int dysco_in_two_paths_data_seg(struct dysco_hashes *dh,
					      struct sk_buff *skb,
					      struct dysco_cb_in *dcb_in,
					      struct tcphdr *th)
	
{
	struct dysco_cb_out *dcb_out;
	
	dcb_out = dcb_in->dcb_out;
	if (dcb_out == NULL) {
		printk(DYSCO_ALERT
		       "BUG: dcb_out is NULL in dysco_input\n");
		return FALSE;
	}
	else if (!dcb_out->old_path) {
		struct dysco_cb_out *old_out = dcb_out->other_path;
		
		// Received data in the new path.				
		if (old_out == NULL) {
			printk(DYSCO_ALERT
			       "BUG: old_out (other_path) is NULL in dysco_input\n");
			return FALSE;
		}
		else {
			// Begin of critical section
			spin_lock_bh(&old_out->seq_lock);
			if (old_out->state == DYSCO_SYN_SENT ||
			    old_out->state == DYSCO_SYN_RECEIVED) {
				// It is the left/right anchors.  Data
				// from   the  other   anchor  arrived
				// before the UDP control message. Set
				// the  ack_cutoff  temporarily  until
				// the UDP control  message arrives to
				// set the correct value.
				__u32 seq = ntohl(th->seq);
				__u32 delta;
				
				if (dcb_out->in_iack < dcb_out->out_iack) {
					delta = dcb_out->out_iack - dcb_out->in_iack;
					seq  -= delta;
				}
				else {
					delta = dcb_out->in_iack - dcb_out->out_iack;
					seq  += delta;
				}
				
				if (old_out->valid_ack_cut) {
					if (before(seq, old_out->ack_cutoff))
						old_out->ack_cutoff = seq; 
				}
				else {
					old_out->ack_cutoff    = seq;
					old_out->valid_ack_cut = 1;
				}
				
			}
			spin_unlock_bh(&old_out->seq_lock);
			// End of critical section
		}
	}
	return TRUE;
}


/*********************************************************************
 *
 *	dysco_input: runs  always in  softirq context. Must  be called
 *	within a rcu_read_lock section.
 *
 *********************************************************************/
rx_handler_result_t dysco_input(struct sk_buff *skb)
{
	struct iphdr		*iph;
	struct tcphdr		*th;
	unsigned char		*data;
	struct dysco_cb_in	*dcb_in;
	struct dysco_hashes	*dh;
	struct ethhdr		*eth;
	int			ret = RX_HANDLER_PASS;
	
	eth = eth_hdr(skb);
	if (unlikely(ntohs(eth->h_proto) != ETH_P_IP))
		return RX_HANDLER_PASS;
	
	rcu_assign_pointer(dh, dysco_get_hashes(dev_net(skb->dev)));
	if (dh == NULL) {
		printk(DYSCO_ALERT "BUG: no namespace found\n");
		return RX_HANDLER_PASS;
	}
	
	iph = ip_hdr(skb);	
	if (iph->protocol == IPPROTO_UDP) {
		ret = dysco_control_input(dh, skb);
		return RX_HANDLER_PASS;
	}
	
	if (iph->protocol != IPPROTO_TCP)
		return RX_HANDLER_PASS;
	
	data = (unsigned char *)iph;
	data += iph->ihl << 2;
	th = (struct tcphdr *)data;
	if ((dcb_in = dysco_lookup_input(dh, skb, iph, th)) == NULL) {
		if (tcp_flag_byte(th) & TCPHDR_SYN)
			return dysco_rx_initiation_new(dh, skb, iph, th);
		else
			return RX_HANDLER_PASS;
	}
	
	if (tcp_flag_byte(th) & TCPHDR_SYN) {
		if (tcp_flag_byte(th) & TCPHDR_ACK) {
			// dcb_in->skb_iif is used in dysco_ctl_save_rcv_window
			// printk(DYSCO_ALERT "skb->ip_summed in syn-ack %d\n", skb->ip_summed);
			dcb_in->skb_iif = skb->skb_iif; 
			dysco_set_ack_number_out(dh, th, dcb_in);
			dysco_in_hdr_rewrite_csum(skb, iph, th, dcb_in);
		}
		else {
			// It is a retransmission, just removes the sc
			// and inserts the Dysco tag.
			dysco_remove_sc(skb, iph, th);
			dysco_insert_tag(dh, skb, iph, th, dcb_in);
			dysco_in_hdr_rewrite(skb, iph, th, dcb_in);
		}
		return RX_HANDLER_PASS;
	}

	if (dcb_in->two_paths) {
		__u16	seg_sz;
		
		seg_sz = ntohs(iph->tot_len) - (iph->ihl * 4) - (th->doff * 4);
		if (seg_sz > 0) {
			// It is a data segment. Assume data in one direction only.
			// For data in both directions, the packet may need to be
			// duplicated or handled separately.
			if (!dysco_in_two_paths_data_seg(dh, skb, dcb_in, th))
				return RX_HANDLER_PASS;
		}
		else
			dysco_in_two_paths_ack(dh, skb, dcb_in, th);
	}

#ifdef DYSCO_MEASUREMENT_INPUT_REWRITE
	
	if (skb->dev == dev_measurement) {
		struct timespec	local_before, local_after;
		static unsigned n_samples = 0;
		
		getnstimeofday(&local_before);
		dysco_in_hdr_rewrite_csum(skb, iph, th, dcb_in);
		getnstimeofday(&local_after);
		
		if (local_after.tv_sec == local_before.tv_sec) {
			rewrite_input_samples[n_samples++] = local_after.tv_nsec - local_before.tv_nsec;
			if (n_samples == DYSCO_MEASUREMENT_SAMPLES) {
				n_samples	= 0;
				dev_measurement = NULL;
			}
		}
		return RX_HANDLER_PASS;		
	}
	
#endif 
	
	dysco_in_hdr_rewrite_csum(skb, iph, th, dcb_in);
	return RX_HANDLER_PASS;		
}
/* dysco_input */


/*********************************************************************
 *
 *	dysco_handle_frame:  pre-process  a  packet to  make  sure  it
 *	should go through the dysco_input function.
 *
 *********************************************************************/
rx_handler_result_t dysco_handle_frame(struct sk_buff **pskb)
{
	struct sk_buff		*skb = *pskb;
	struct in_device	*in_dev;

	
	if (unlikely(skb->pkt_type == PACKET_LOOPBACK))
		return RX_HANDLER_PASS;
	
	if (!is_valid_ether_addr(eth_hdr(skb)->h_source)) {
		kfree_skb(skb);
		return RX_HANDLER_CONSUMED;
	}

	//return RX_HANDLER_PASS;
	
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (skb == NULL)
		return RX_HANDLER_CONSUMED;

	// This is necessary to avoid processing packets that are not destined
	// to this host. For example, packets that cross a router running the
	// Dysco module. Also, the for loop handles virtual IP interfaces.
	rcu_read_lock();
	rcu_assign_pointer(in_dev, skb->dev->ip_ptr);
	for_ifa(in_dev) {
		if (ip_hdr(skb)->daddr == ifa->ifa_address) {
			if (dysco_input(skb) == RX_HANDLER_CONSUMED) {
				rcu_read_unlock();
				return RX_HANDLER_CONSUMED;
			}
		}
	} endfor_ifa(in_dev);
	rcu_read_unlock();
	return RX_HANDLER_PASS;
}
/* dysco_handle_frame */
