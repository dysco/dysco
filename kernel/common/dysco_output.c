/*
 *
 *	Dynamic Service Chaining with Dysco
 *	Dysco kernel agent: dysco_output.c
 *
 *	This module implements the Dysco  data path when packets leave
 *	a host.
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
#include <net/udp.h>
#include <net/checksum.h>
#include <net/neighbour.h>
#include <net/arp.h>
#include <net/ip_fib.h>
#include <net/flow.h>
#include "dysco.h"
/* */

/*********************************************************************
 *
 *	dysco_fix_tcp_ip_csum: fix  the IP and TCP  checksums. This is
 *	one of  the few  times that both  checksums are  computed from
 *	scratch. However, this is not a big deal because this function
 *	is called only with a TCP syn packet.
 *
 *********************************************************************/
static inline void dysco_fix_tcp_ip_csum(struct sk_buff *skb)
{
	int		tcp_len;
	struct iphdr	*iph = ip_hdr(skb);
	struct tcphdr	*th  = tcp_hdr(skb);

	// Fix IP checksum
	iph->check = 0;
	iph->check = ip_fast_csum(iph, iph->ihl);

	// Fix TCP checksum
	tcp_len    = ntohs(iph->tot_len) - (iph->ihl << 2);
	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		__wsum pseudo_hdr;
		
		th->check  = 0;
		pseudo_hdr = csum_tcpudp_nofold(iph->saddr, iph->daddr,
						tcp_len, IPPROTO_TCP, 0);
		th->check  = csum_fold(csum_partial(th, tcp_len, pseudo_hdr));
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	}
	else 
		th->check = ~tcp_v4_check(tcp_len, iph->saddr, iph->daddr, 0);
}
/* dysco_fix_tcp_ip_csum */


/*********************************************************************
 *
 *	dysco_remove_tag: removes the Dysco  tag that was  inserted in
 *	the input.   We currently assume  that the middlebox  does not
 *	insert a  new option. If  the middlebox inserts a  new option,
 *	then we need to parse the  header again and shift the inserted
 *	options to the place where the Dysco tag is.
 *
 *********************************************************************/
static inline void dysco_remove_tag(struct sk_buff *skb)
{
	struct iphdr	*iph = ip_hdr(skb);
	struct tcphdr	*th  = tcp_hdr(skb);

	th->doff    -= DYSCO_TCP_OPTION_LEN / 4;	
	iph->tot_len = htons(ntohs(iph->tot_len) - DYSCO_TCP_OPTION_LEN);

	// The call to skb_trim could be  avoided, but it is not worth
	// the hassle because skb_trim is a small and fast function.	
	skb_trim(skb, skb->len - DYSCO_TCP_OPTION_LEN); 
}
/* dysco_remove_tag */


/*********************************************************************
 *
 *	dysco_tcp_sack_csum:  translates  the   TCP  sack  blocks  and
 *	recomputes  the checksum  incrementally more  efficiently than
 *	using the native Linux functions.
 *
 *********************************************************************/
void dysco_tcp_sack_csum(struct sk_buff *skb, struct tcphdr *th, __u32 delta, __u8 add)
{
	const unsigned char	*ptr;
	unsigned char		fix_checksum = FALSE;
	int			length;
	__wsum			from, to;
	__u64			from64, to64;

	from64 = 0;
	to64   = 0;
	length = (th->doff * 4) - sizeof(struct tcphdr);	
	ptr    = (const unsigned char *)(th + 1);	
	while (length > 0) {
		int opcode = *ptr++;
		int opsize;
		
		switch (opcode) {
		case TCPOPT_EOL:
			return;
			
		case TCPOPT_NOP:        /* Ref: RFC 793 section 3.1 */
			length--;
			continue;
			
		default:
			opsize = *ptr++;
			if (opsize < 2) /* "silly options" */
				return;
			
			if (opsize > length)
				return; /* don't parse partial options */
			
			switch(opcode) {
			case TCPOPT_SACK:
				if ((opsize >= (TCPOLEN_SACK_BASE + TCPOLEN_SACK_PERBLOCK)) &&
                                    !((opsize - TCPOLEN_SACK_BASE) % TCPOLEN_SACK_PERBLOCK)) {
					const unsigned char *lptr = ptr;
					int blen = opsize - 2; // sack-block length.

					while (blen > 0) {
						__u32	*left_edge, *right_edge;
						__u32	new_ack_l, new_ack_r;

						left_edge  = (__u32 *)lptr;
						right_edge = (__u32 *)(lptr+4);
						if (add) {
							new_ack_l = htonl(ntohl(*left_edge) + delta);
							new_ack_r = htonl(ntohl(*right_edge) + delta);
						}
						else {
							new_ack_l = htonl(ntohl(*left_edge) - delta);
							new_ack_r = htonl(ntohl(*right_edge) - delta);
						}
						
						if (skb->ip_summed != CHECKSUM_PARTIAL) {
							fix_checksum = TRUE;
						
							from64 += *left_edge;
							to64   += new_ack_l;
						
							from64 += *right_edge;
							to64   += new_ack_r;
						}
					
						*left_edge  = new_ack_l;
						*right_edge = new_ack_r;

						lptr += 8;
						blen -= 8;
						tcp_sack_rewrites++;
					}
				}
				break;
			}
			ptr += opsize - 2;
			length -= opsize;
		}
	}
	
	if (fix_checksum) {
		from64 = (from64 & 0xffffffff) + (from64 >> 32);
		from   = (from64 & 0xffffffff) + (from64 >> 32);
		
		to64 = (to64 & 0xffffffff) + (to64 >> 32);
		to   = (to64 & 0xffffffff) + (to64 >> 32);
		
		dysco_inet_proto_csum_replace4(&th->check, skb, from, to, FALSE);
	}	
}
/* dysco_tcp_sack_csum */


/*********************************************************************
 *
 *	dysco_tcp_sack: translates  the TCP  sack blocks. I  kept this
 *	function and the  above to compare the impact  of the checksum
 *	computation.
 *
 *********************************************************************/
void dysco_tcp_sack(struct sk_buff *skb, struct tcphdr *th, __u32 delta, __u8 add)
{
	const unsigned char	*ptr;
	int			length;

	length = (th->doff * 4) - sizeof(struct tcphdr);	
	ptr    = (const unsigned char *)(th + 1);	
	while (length > 0) {
		int opcode = *ptr++;
		int opsize;
		
		switch (opcode) {
		case TCPOPT_EOL:
			return;
			
		case TCPOPT_NOP:        /* Ref: RFC 793 section 3.1 */
			length--;
			continue;
			
		default:
			opsize = *ptr++;
			if (opsize < 2) /* "silly options" */
				return;
			
			if (opsize > length)
				return; /* don't parse partial options */
			
			if (opcode == TCPOPT_SACK) {
				if ((opsize >= (TCPOLEN_SACK_BASE + TCPOLEN_SACK_PERBLOCK)) &&
                                    !((opsize - TCPOLEN_SACK_BASE) % TCPOLEN_SACK_PERBLOCK)) {
					const unsigned char *lptr = ptr;
					int blen = opsize - 2; // sack-block length

					//printk(DYSCO_ALERT "Rewriting SACK blocks\n");
					while (blen > 0) {					
						__u32	*left_edge, *right_edge;
						__u32	new_ack_l, new_ack_r;

						left_edge  = (__u32 *)lptr;
						right_edge = (__u32 *)(lptr+4);
						if (add) {
							new_ack_l = htonl(ntohl(*left_edge) + delta);
							new_ack_r = htonl(ntohl(*right_edge) + delta);
						}
						else {
							new_ack_l = htonl(ntohl(*left_edge) - delta);
							new_ack_r = htonl(ntohl(*right_edge) - delta);
						}
						dysco_inet_proto_csum_replace4(&th->check, skb, *left_edge,
									       new_ack_l, FALSE);
						dysco_inet_proto_csum_replace4(&th->check, skb, *right_edge,
									       new_ack_r, FALSE);
						*left_edge  = new_ack_l;
						*right_edge = new_ack_r;
						
						lptr += 8;
						blen -= 8;
						tcp_sack_rewrites++;						
					}
				}
			}
			ptr += opsize-2;
			length -= opsize;
		}
         }
}
/* dysco_tcp_sack */


/*********************************************************************
 *
 *	dysco_out_hdr_rewrite:  rewrites  the source  and  destination
 *	addresses and  the source and  destination ports, but  it does
 *	not compute a new checkum.
 *
 *********************************************************************/
static inline void dysco_out_hdr_rewrite(struct sk_buff *skb,
					 struct dysco_cb_out *dcb)
{
	struct iphdr  *iph = ip_hdr(skb);
	struct tcphdr  *th = tcp_hdr(skb);
	int ip_tot_len, iphdr_len, tcp_len;
	
	ip_tot_len = ntohs(iph->tot_len);
	iphdr_len  = iph->ihl << 2;
	tcp_len = ip_tot_len - (iph->ihl << 2);

	iph->saddr = dcb->sub.sip;
	iph->daddr = dcb->sub.dip;
	th->source = dcb->sub.sport;
	th->dest   = dcb->sub.dport;

	memcpy(eth_hdr(skb)->h_dest, dcb->nh_mac,  ETHER_ADDR_LEN);
	memcpy(eth_hdr(skb)->h_source, skb->dev->dev_addr,  ETHER_ADDR_LEN);
}
/* dysco_out_hdr_rewrite */


/*********************************************************************
 *
 *	dysco_out_hdr_rewrite_csum:  rewrites  source and  destination
 *	addresses  and  source  and  destination  addresses.  It  also
 *	computes the new checksum incrementally.
 *
 *********************************************************************/
static inline void dysco_out_hdr_rewrite_csum(struct sk_buff *skb,
					      struct dysco_cb_out *dcb_out) 
{
	struct iphdr	*iph = ip_hdr(skb);
	struct tcphdr	*th = tcp_hdr(skb);
	unsigned int	from, to;

	// Add IP addresses in the original packet
	from  = iph->saddr;
	from += iph->daddr;
	from += (iph->daddr > from);  // add carry

	// Add the new IP addresses 
	to  = dcb_out->sub.sip;
	to += dcb_out->sub.dip;
	to += (dcb_out->sub.dip > to); // add carry

	dysco_csum_replace4(&iph->check, from, to);
	iph->saddr = dcb_out->sub.sip;
	iph->daddr = dcb_out->sub.dip;

	// Fix the checksum of the TCP pseudo-header because the IP addresses changed.
	inet_proto_csum_replace4(&th->check, skb, from, to, TRUE);
	
	inet_proto_csum_replace2(&th->check, skb, th->source, dcb_out->sub.sport, FALSE);
	th->source = dcb_out->sub.sport;
	
	inet_proto_csum_replace2(&th->check, skb, th->dest, dcb_out->sub.dport, FALSE);
	th->dest   = dcb_out->sub.dport;
	
	memcpy(eth_hdr(skb)->h_dest, dcb_out->nh_mac,  ETHER_ADDR_LEN);
	memcpy(eth_hdr(skb)->h_source, skb->dev->dev_addr,  ETHER_ADDR_LEN);
}
/* dysco_out_hdr_rewrite_csum */


/*********************************************************************
 *
 *	dysco_out_rewrite_seq: rewrites the TCP sequence number.
 *
 *********************************************************************/
static inline void dysco_out_rewrite_seq(struct sk_buff *skb,
					 struct dysco_cb_out *dcb_out,
					 struct tcphdr *th)
{
	if (dcb_out->seq_delta) {
		__u32 new_seq;
		__u32 seq = ntohl(th->seq);

		if (dcb_out->seq_add)
			new_seq = htonl(seq + dcb_out->seq_delta);
		else
			new_seq = htonl(seq - dcb_out->seq_delta);
		
		dysco_inet_proto_csum_replace4(&th->check, skb, th->seq,
					       new_seq, FALSE);
		th->seq = new_seq;
	}
}
/* dysco_out_rewrite_seq */


/*********************************************************************
 *
 *	dysco_out_rewrite_ack: rewrites the TCP ACK number.
 *
 *********************************************************************/
static inline void dysco_out_rewrite_ack(struct sk_buff *skb,
					     struct dysco_cb_out *dcb_out,
					     struct tcphdr *th)
{
	if (dcb_out->ack_delta) {
		__u32 new_ack;
		__u32 ack = ntohl(th->ack_seq);

		//printk(DYSCO_ALERT "delta is not zero in dysco_output\n");
		
		if (dcb_out->ack_add) 
			new_ack = htonl(ack + dcb_out->ack_delta);
		else
			new_ack = htonl(ack - dcb_out->ack_delta);

		if (dcb_out->sack_ok)
			dysco_tcp_sack(skb, th, dcb_out->ack_delta, dcb_out->ack_add);
		
		dysco_inet_proto_csum_replace4(&th->check, skb, th->ack_seq,
					       new_ack, FALSE);
		th->ack_seq = new_ack;
	}
}
/* dysco_out_rewrite_ack */


/*********************************************************************
 *
 *	dysco_out_rewrite_ts: rewrites the TCP timestamp option.
 *
 *********************************************************************/
static inline void dysco_out_rewrite_ts(struct sk_buff *skb,
					struct dysco_cb_out *dcb_out,
					struct tcphdr *th)
{
	struct tcp_ts *ts;
	__u32 new_ts, new_tsr;
	
	ts = (struct tcp_ts *)dysco_get_ts_option(th);
	if (ts == NULL)
		return;

	if (dcb_out->ts_delta) {
		if (dcb_out->ts_add)
			new_ts = ntohl(ts->ts) + dcb_out->ts_delta;
		else
			new_ts = ntohl(ts->ts) - dcb_out->ts_delta;
		
		new_ts = htonl(new_ts);
		dysco_inet_proto_csum_replace4(&th->check, skb, ts->ts, new_ts, FALSE);
		ts->ts  = new_ts;
		tcp_ts_rewrites++;
	}

	if (dcb_out->tsr_delta) {
		if (dcb_out->tsr_add)
			new_tsr = ntohl(ts->tsr) + dcb_out->tsr_delta;
		else
			new_tsr = ntohl(ts->tsr) - dcb_out->tsr_delta;
		
		new_tsr = htonl(new_tsr);		
		dysco_inet_proto_csum_replace4(&th->check, skb, ts->tsr, new_tsr, FALSE);		
		ts->tsr = new_tsr;
		tcp_tsr_rewrites++;
	}
}
/* dysco_out_rewrite_ts */


/*********************************************************************
 *
 *	dysco_out_rewrite_rcv_wnd: rewrites  the receiver  window that
 *	is being advertised.
 *
 *********************************************************************/
static inline void dysco_out_rewrite_rcv_wnd(struct sk_buff *skb,
					     struct dysco_cb_out *dcb_out,
					     struct tcphdr *th)
{
	if (dcb_out->ws_delta) {
		__u16 new_win;
		__u32 wnd  = ntohs(th->window);

		printk(DYSCO_ALERT "Rewriting WS OUTPUT in=%u out=%u\n",
		       dcb_out->ws_in, dcb_out->ws_out);
		wnd <<= dcb_out->ws_in;
		wnd >>= dcb_out->ws_out;
		new_win = htons(wnd);
		inet_proto_csum_replace2(&th->check, skb, th->window, new_win, FALSE);
		th->window = new_win;
	}
}
/* dysco_out_rewrite_rcv_wnd */


/*********************************************************************
 *
 *	dysco_pick_path_seq: selects the old or new path  based on the
 *	sequence number.
 *
 *********************************************************************/
static inline struct dysco_cb_out *dysco_pick_path_seq(struct sk_buff *skb,
						       struct dysco_cb_out *dcb_out,
						       __u32 seq)
{
	if (dcb_out->state_t) {
		if (dcb_out->state == DYSCO_ESTABLISHED)
			dcb_out = dcb_out->other_path;
	}
	else if (dcb_out->use_np_seq) {
		// Return the new path cb.
		dcb_out = dcb_out->other_path;
	}       // check if it is a retransmission
	else if (!before(seq, dcb_out->seq_cutoff)) { 
		// It is not a retransmission. Return the new path cb.
		dcb_out = dcb_out->other_path;
	}
	return dcb_out;
}
/* dysco_pick_path_seq */


/*********************************************************************
 *
 *	dysco_pick_path_ack: selects the old or new path  based on the
 *	ack number.
 *
 *********************************************************************/
static inline struct dysco_cb_out *dysco_pick_path_ack(struct sk_buff *skb,
						       struct dysco_cb_out *dcb_out,
						       struct tcphdr *th)
{
	__u32 ack = ntohl(th->ack_seq);

	if (dcb_out->state_t) {
		if (dcb_out->state == DYSCO_ESTABLISHED)
			dcb_out = dcb_out->other_path;
	} else if (dcb_out->valid_ack_cut) {
		if (dcb_out->use_np_ack) {
			dcb_out = dcb_out->other_path;
		}
		else if (!after(dcb_out->ack_cutoff, ack)) {
			// dcb_out->use_np_ack = TRUE;
			// If it is a FIN, return the new path. Otherwise, return
			// old path, but switch to the new one.
			if (tcp_flag_byte(th) & TCPHDR_FIN)
				dcb_out = dcb_out->other_path;
			else {
				//printk(DYSCO_ALERT "finishing old path ack_seq=%u ack_cutoff=%u "
				//       "in_iack=%u out_iack=%u\n",
				//       ack, dcb_out->ack_cutoff,
				//       dcb_out->other_path->in_iack, dcb_out->other_path->out_iack);
				__u32 ack_cutoff = htonl(dcb_out->ack_cutoff);
				
				inet_proto_csum_replace4(&th->check, skb,
							 th->ack_seq,
							 ack_cutoff,
							 FALSE);
				th->ack_seq = ack_cutoff;
				
				/*
				 FIXME:  make sure  at  least two  acks are  sent  in the  old
				 path. I  am currently  using the  acks from  the new  path to
				 acknowledge data in the old path, but I fix the ack   number.
				 Need  to  create a new ACK and send one in the  old  path and
				 the  other one in the new path. Better yet, need to implement
				 the old-path teardown.  Also, need to remove  the SACK blocks
				 that  do  not  belong  in the old path. I am not removing the
				 blocks,  but it  does break the session.  TCP  is supposed to
				 silently  discard  SACK  blocks that fall outside its sending
				 window.
				*/
				dcb_out->ack_ctr++;
				if (dcb_out->ack_ctr > 1)
					dcb_out->use_np_ack = TRUE;
			}
		}
	}
	return dcb_out;
}
/* dysco_pick_path_ack */


struct net_device *dev_measurement = NULL;
/* */

/*********************************************************************
 *
 *	dysco_out_translate:  this  functions   rewrites  the  session
 *	ID. It  rewrites the  IP addresses and  port numbers  from the
 *	session to  subsession. It  calls the  functions that  fix (if
 *	necessary) the sequence and ack numbers.
 *
 *********************************************************************/
static inline void dysco_out_translate(struct sk_buff *skb,
					  struct dysco_cb_out *dcb_out)
{
	struct iphdr		*iph = ip_hdr(skb);
	struct tcphdr		*th  = tcp_hdr(skb);
	__u16			seg_sz;
	__u32			seq;
	struct dysco_cb_out	*other_path;
	
	seg_sz = ntohs(iph->tot_len) - (iph->ihl * 4) - (th->doff * 4);	
	seq = ntohl(th->seq) + seg_sz;

	// Begin of critical section
	spin_lock_bh(&dcb_out->seq_lock);
	other_path = dcb_out->other_path;
	if (other_path == NULL) {
		// There is only one path, i.e., there has been no reconfiguration.		
		// Record highest seq numbers if the segment contains data, and
		// it is not a retransmission.
		if (seg_sz > 0 && after(seq, dcb_out->seq_cutoff)) 
			dcb_out->seq_cutoff = seq;
		spin_unlock_bh(&dcb_out->seq_lock);
		// End of critical section 1
	}  
	else {
		spinlock_t	*local_lock; // This is necessary because dcb_out may change.
		
		local_lock = &dcb_out->seq_lock;
		if (dcb_out->state == DYSCO_ESTABLISHED) {
			if (seg_sz > 0) {
				// It is a data segment. Assume data in one direction only.
				// For data in both directions, may need to duplicate the packet.
				dcb_out = dysco_pick_path_seq(skb, dcb_out, seq);
				// The code below is necessary for multiple reconfigurations.
				// Commented until I change to dysco_sk
				// if (!dcb_out->old_path && after(seq, dcb_out->seq_cutoff))
				//	dcb_out->seq_cutoff = seq;
			}
			else
				dcb_out = dysco_pick_path_ack(skb, dcb_out, th);
		}
		else if (dcb_out->state == DYSCO_SYN_SENT) {
			// This is the left anchor. If it is a data segment,
			// it must be sent in the old path, so use the current dct.
			if (seg_sz > 0) {
				// Update seq_cutoff until switching to the new path
				if (after(seq, dcb_out->seq_cutoff))
					dcb_out->seq_cutoff = seq;
			}
			else {
				// It is an ack. This happens at the left anchor
				// only if TCP packets are received in the new
				// path before the UDP control message  DYSCO_SYN_ACK
				// has been received.
				dcb_out = dysco_pick_path_ack(skb, dcb_out, th);
			}
		}
		else if (dcb_out->state == DYSCO_SYN_RECEIVED) {
			if (seg_sz > 0) {
				// Right anchor is in the sending side of the TCP session.
				dcb_out = dysco_pick_path_seq(skb, dcb_out, seq);
				if (!dcb_out->old_path) {
					// The code below is necessary for multiple reconfigurations.
					// Commented until I change to dysco_sk
					//if (after(seq, dcb_out->seq_cutoff))
					//	dcb_out->seq_cutoff = seq;
					
					if (tcp_flag_byte(th) & TCPHDR_ACK) {
						// Clear the ack to avoid dup-acks in the new path.
						// I need to generate a new packet if the ack belongs
						// to the new path. This is not an issue for
						// single senders. Dysco is not ready for handling
						// data in both directions yet.
					}
				}
			}
			else
				dcb_out = dysco_pick_path_ack(skb, dcb_out, th);
		}
		else
			printk(DYSCO_ALERT
			       "BUG: unknown state in dysco_out_translate %u\n",
			       dcb_out->state);
		spin_unlock_bh(local_lock);
		// End of critical section 2

		dysco_out_rewrite_seq(skb, dcb_out, th);
		dysco_out_rewrite_ack(skb, dcb_out, th);

		if (dcb_out->ts_ok)
			dysco_out_rewrite_ts(skb, dcb_out, th);

		if (dcb_out->ws_ok)
			dysco_out_rewrite_rcv_wnd(skb, dcb_out, th);
	}
	
#ifdef DYSCO_MEASUREMENT_OUTPUT_REWRITE	
	if (skb->dev == dev_measurement) {
		struct timespec	local_before, local_after;
		static unsigned n_samples = 0;
		__u32	cpu_before, cpu_after;
		
		getnstimeofday(&local_before);
		dysco_out_hdr_rewrite_csum(skb, dcb_out);
		getnstimeofday(&local_after);
		
		if (local_after.tv_sec == local_before.tv_sec) {
			rewrite_output_samples[n_samples++] =
				local_after.tv_nsec - local_before.tv_nsec;
			if (n_samples == DYSCO_MEASUREMENT_SAMPLES) {
				n_samples	= 0;
				dev_measurement = NULL;
			}
		}
		return;
	}
#endif
	
	dysco_out_hdr_rewrite_csum(skb, dcb_out);
}
/* dysco_out_translate */


/*********************************************************************
 *
 *	dysco_add_sc: adds  the service  chain to  the syn  packet and
 *	recomputes the checksum.
 *
 *********************************************************************/
static inline int dysco_add_sc(struct sk_buff *skb, struct dysco_cb_out *dcb)
{
	int		syn_memsz;
	unsigned char	*pkt_sc, *tcp_data;
	__be16		tot_len;
	struct iphdr	*iph = ip_hdr(skb);
	struct tcphdr	*th  = tcp_hdr(skb);

	syn_memsz = sizeof(struct tcp_session) + SC_MEM_SZ(dcb->sc);
	if (syn_memsz > skb_tailroom(skb)) {
		printk(DYSCO_ALERT "sc is too long! len=%d room=%d\n",
		       syn_memsz, skb_tailroom(skb));
		return 0;
	}
	
	tcp_data = pkt_sc = (unsigned char *)skb_put(skb, syn_memsz);
	memcpy(pkt_sc, &dcb->super, sizeof(struct tcp_session));
	
	pkt_sc += sizeof(struct tcp_session);
	memcpy(pkt_sc, dcb->sc, SC_MEM_SZ(dcb->sc));
	
	tot_len = iph->tot_len;
	iph->tot_len = htons(skb->len - ETHER_HDR_LEN);
	dysco_csum_replace2(&iph->check, tot_len, iph->tot_len);
	
	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		// Fix TCP checksum
		th->check = csum_fold(csum_partial(tcp_data, syn_memsz,
						   ~csum_unfold(th->check)));
	}
	return 1;
}
/* dysco_add_sc */


/*********************************************************************
 *
 *	dysco_out_tx_init: init function for a syn packet. It adds the
 *	service chain in the TCP syn packet and recomputes it checksum
 *	from scratch.
 *
 *********************************************************************/
static inline netdev_tx_t dysco_out_tx_init(struct sk_buff *skb,
					    struct dysco_cb_out *dcb)
{
	if (!dysco_add_sc(skb, dcb)) {
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}	
	dysco_fix_tcp_ip_csum(skb);
	return 1;	
}
/* dysco_out_tx_init */


/*********************************************************************
 *
 *	dysco_match_policy: iterates over the policies to check if the
 *	packet matches one of them.
 *
 *********************************************************************/
static inline struct service_chain *dysco_match_policy(struct dysco_hashes *dh,
						       struct sk_buff *skb)
{
	struct dysco_policies	*dps;
	
	rcu_read_lock(); // It is already with rcu_read_lock section, but it is ok to call again.
	list_for_each_entry_rcu(dps, &dh->policies, list) {
		struct bpf_prog	*fp;
		
		fp = dps->filter;
		if ((*fp->bpf_func)(skb, fp->insnsi)) {
			struct service_chain	*sc;
			
			sc = dps->sc;
			rcu_read_unlock();
			return sc;
		}
	}
	rcu_read_unlock();
	
	return NULL;
}
/* dysco_match_policy */


/*********************************************************************
 *
 *	dysco_same_subnet:  checks  if  two  IPs belong  to  the  same
 *	subnet.
 *
 *********************************************************************/
static inline int dysco_same_subnet(__u32 ip1, __u32 ip2, __u32 mask)
{
	if ( (ip1 & mask) == (ip2 & mask) )
		return TRUE;
	else
		return FALSE;
}
/* dysco_same_subnet */


/*********************************************************************
 *
 *	dysco_arp: This function picks the MAC address of the next hop
 *	and the source IP address of the subsession.
 *
 *********************************************************************/
void dysco_arp(struct sk_buff *skb, struct dysco_cb_out *dcb)
{
	struct flowi4		fl4;
	struct net		*net = dev_net(skb->dev);
	struct iphdr		*iph = ip_hdr(skb);
	struct neighbour	*n;
	struct rtable		*rt;
	__u32			nh;
	
	fl4.flowi4_oif   = 0;
	fl4.flowi4_iif   = skb->dev->ifindex;
	fl4.flowi4_mark  = skb->mark;
	fl4.flowi4_tos   = iph->tos;
	fl4.flowi4_scope = RT_SCOPE_UNIVERSE;
	fl4.flowi4_flags = 0;
	fl4.daddr        = dcb->sub.dip;
	fl4.saddr        = 0; // 0=the lookup function picks the source IP address.
	
	nh = dcb->sub.dip;
	rt = __ip_route_output_key(net, &fl4);
	if (rt) {
		nh = rt_nexthop(rt, nh);
		ip_rt_put(rt);
	}
	
	dcb->sub.sip = fl4.saddr;
	n = __neigh_lookup(&arp_tbl, &nh, skb->dev, TRUE);
	if (n == NULL)
		; /*printk(DYSCO_ALERT
		       "neigh_lookup returned NULL: "
		       "saddr=" IP_STR " daddr=" IP_STR " nh="IP_STR "\n",
		       IP_TO_STR(fl4.saddr), IP_TO_STR(dcb->sub.dip),
		       IP_TO_STR(nh)); */
	else {
		/*
		printk(DYSCO_ALERT
		       "neigh_lookup saddr=" IP_STR " daddr=" IP_STR
		       " nh=" IP_STR " gw mac=" MAC_STR "\n", 
		       IP_TO_STR(fl4.saddr), IP_TO_STR(dcb->sub.dip),
		       IP_TO_STR(nh), MAC_TO_STR(n->ha));
		*/
		memcpy(&dcb->nh_mac[0], n->ha, ETHER_ADDR_LEN);
		neigh_release(n);
	}
}
/* dysco_arp */


/*********************************************************************
 *
 *	dysco_create_cb_out:  creates  an  output  control  block  and
 *	initializes it with the super session information.
 *
 *********************************************************************/
static inline struct dysco_cb_out *dysco_create_cb_out(struct sk_buff *skb,
						       struct service_chain *sc)
{
	struct dysco_cb_out	*dcb;
	
	dcb = kzalloc(sizeof(struct dysco_cb_out), GFP_ATOMIC);
	if (!dcb) {
		printk(DYSCO_ALERT
		       "could not allocate memory in dysco_create_cb_out\n");
		return NULL;
	}
	
	// Initialize the dysco control block
	dcb->sc            = sc;
	dcb->super.sip     = ip_hdr(skb)->saddr;
	dcb->super.dip     = ip_hdr(skb)->daddr;
	dcb->super.sport   = tcp_hdr(skb)->source;
	dcb->super.dport   = tcp_hdr(skb)->dest;
	
	spin_lock_init(&dcb->seq_lock);

	if (sc->len) {
		dcb->sub.dip = sc->hops[0].ip;		
		dysco_arp(skb, dcb);
		dcb->sub.sport = allocate_local_port();
		dcb->sub.dport = allocate_neighbor_port();
		return dcb;
	}
	else {
		printk(DYSCO_ALERT "ERROR in the service chain\n");
		kfree(dcb);
		return NULL;
	}
} 
/* dysco_create_cb_out */


/*********************************************************************
 *
 *	dysco_insert_cb_out_reverse:  inserts a  control  block in the
 *	input hash table with the five-tuple information reversed.
 *
 *********************************************************************/
struct dysco_cb_in *dysco_insert_cb_out_reverse(struct dysco_hashes *dh,
						struct dysco_cb_out *dcb_out,
						__u8 two_paths)
{
	struct dysco_cb_in *dcb_in;

	dcb_in = kzalloc(sizeof(struct dysco_cb_in), GFP_ATOMIC);
	if (dcb_in == NULL) {
		printk(DYSCO_ALERT
		       "could not allocate memory for dcb_in in dysco_insert_cb_reverse\n");
		// FIXME: need to remove dcb_out and check other consequences.
		return NULL;
	}
	
	dcb_in->sub.sip   = dcb_out->sub.dip;
	dcb_in->sub.dip   = dcb_out->sub.sip;
	dcb_in->sub.sport = dcb_out->sub.dport;
	dcb_in->sub.dport = dcb_out->sub.sport;
	
	dcb_in->super.sip   = dcb_out->super.dip;
	dcb_in->super.dip   = dcb_out->super.sip;
	dcb_in->super.sport = dcb_out->super.dport;
	dcb_in->super.dport = dcb_out->super.sport;
	
	dcb_in->in_iack = dcb_in->out_iack = dcb_out->out_iseq;
	dcb_in->in_iseq = dcb_in->out_iseq = dcb_out->out_iack;

	dcb_in->seq_delta = dcb_in->ack_delta = 0;

	// The initialization of timestamp and  ws at this point might
	// be unnecessary. Need to check again.
	dcb_in->ts_ok = dcb_out->ts_ok;
	
	dcb_in->ts_in = dcb_in->ts_out = dcb_out->tsr_in;
	dcb_in->ts_delta = 0;

	dcb_in->tsr_in = dcb_in->tsr_out = dcb_out->ts_in;
	dcb_in->tsr_delta = 0;
	
	dcb_in->ws_ok = dcb_out->ws_ok;
	dcb_in->ws_in = dcb_in->ws_out = dcb_out->ws_in;	
	dcb_in->ws_delta = 0;
	
	dcb_in->sack_ok = dcb_out->sack_ok;
	
	dcb_in->two_paths = two_paths;

	// Connect the input and output control blocks from the same interface
	dcb_in->dcb_out = dcb_out;		
	dcb_out->dcb_in = dcb_in;
	
	rhashtable_insert_fast(&dh->dysco_hash_in, &dcb_in->node,
			       dysco_rhashtable_params_in);
	return dcb_in;
}
/* dysco_insert_cb_out_reverse */


/*********************************************************************
 *
 *	dysco_insert_cb_out:  inserts a  control block  in the  output
 *	hash table  and its correspondent  control block in  the input
 *	hash  table.  The  five-tuple information  is reversed  in the
 *	input hash table.
 *
 *********************************************************************/
void dysco_insert_cb_out(struct dysco_hashes *dh, struct sk_buff *skb,
			 struct dysco_cb_out *dcb_out, __u8 two_paths)
{
	rhashtable_insert_fast(&dh->dysco_hash_out, &dcb_out->node,
			       dysco_rhashtable_params_out);
	dcb_out->dcb_in = dysco_insert_cb_out_reverse(dh, dcb_out, two_paths);
}
/* dysco_insert_cb_out */


/*********************************************************************
 *
 *	dysco_out_lookup:  searches for  a control  block in  the output
 *	hash table. The key is the TCP five-tuple
 *
 *********************************************************************/
static inline struct dysco_cb_out *dysco_out_lookup(struct dysco_hashes *dh,
						       struct sk_buff *skb)
{
	struct tcp_session	local_ss;

	local_ss.sip   = ip_hdr(skb)->saddr;
	local_ss.dip   = ip_hdr(skb)->daddr;
	local_ss.sport = tcp_hdr(skb)->source;
	local_ss.dport = tcp_hdr(skb)->dest;
	
	return rhashtable_lookup_fast(&dh->dysco_hash_out, &local_ss,
				      dysco_rhashtable_params_out);
}
/* dysco_out_lookup */


/*********************************************************************
 *
 *	dysco_lookup_pending:  searches for a  control  block  in  the
 *	pending hash table. The key is the TCP five-tuple.
 *
 *********************************************************************/
static inline struct dysco_cb_out *dysco_lookup_pending(struct dysco_hashes *dh,
							struct sk_buff *skb)
{
	struct tcp_session	local_ss;

	local_ss.sip   = ip_hdr(skb)->saddr;
	local_ss.dip   = ip_hdr(skb)->daddr;
	local_ss.sport = tcp_hdr(skb)->source;
	local_ss.dport = tcp_hdr(skb)->dest;
	
	return rhashtable_lookup_fast(&dh->dysco_hash_pen, &local_ss,
				      dysco_rhashtable_params_out);
}
/* dysco_lookup_pending */


/*********************************************************************
 *
 *	dysco_lookup_pen_tag:  searches for a  control  block  in  the
 *	pending hash table.  The key is  the Dysco tag inserted in the
 *	input (if present).
 *
 *********************************************************************/
static inline struct dysco_cb_out *dysco_lookup_pen_tag(struct dysco_hashes *dh,
							struct sk_buff *skb)
{
	struct dysco_cb_out dcb_out_aux, *dcb_out;

	dcb_out_aux.tag_ok    = 0;
	dcb_out_aux.sub.sip   = 0;
	dcb_out_aux.sub.sport = 0;
	dysco_parse_tcp_syn_opt_s(tcp_hdr(skb), &dcb_out_aux);
	
	if (dcb_out_aux.tag_ok) {
		dcb_out = rhashtable_lookup_fast(&dh->dysco_hash_pen_tag, &dcb_out_aux.dysco_tag,
						 dysco_rhashtable_params_pen);
		
		if (dcb_out) {
			dcb_out->ws_ok    = dcb_out_aux.ws_ok;
			dcb_out->ws_delta = 0;
			dcb_out->ws_in    = dcb_out->ws_out = dcb_out_aux.ws_in;

			dcb_out->ts_ok    = dcb_out_aux.ts_ok;
			dcb_out->ts_delta = 0;
			dcb_out->ts_in    = dcb_out->ts_out = dcb_out_aux.ts_in;

			dcb_out->sack_ok  = dcb_out_aux.sack_ok;

			dcb_out->tag_ok    = 1;
			dcb_out->dysco_tag = dcb_out_aux.dysco_tag;
		}
		return dcb_out;
	}
	else
		return NULL;
}
/* dysco_lookup_pen_tag */


/*********************************************************************
 *
 *	dysco_update_five_tuple:   the  middlebox   has  changed   the
 *	five-tuple of  the packet,  so we need  to update  the control
 *	block.
 *
 *********************************************************************/
static inline void dysco_update_five_tuple(struct sk_buff *skb,
					   struct dysco_cb_out *dcb_out)
{
	struct iphdr *iph = ip_hdr(skb);
	struct tcphdr *th = tcp_hdr(skb);

	// Could check if a field changed, but it is not worth.
	dcb_out->super.sip   = iph->saddr;
	dcb_out->super.dip   = iph->daddr;
	dcb_out->super.sport = th->source;
	dcb_out->super.dport = th->dest;
}
/* dysco_update_five_tuple */


/*********************************************************************
 *
 *	dysco_out_handle_mb:  handles a  packet that  has just  left a
 *	middlebox. It  has to remove  the entries in the  pending hash
 *	tables that were inserted in the input path.
 *
 *********************************************************************/
static inline netdev_tx_t dysco_out_handle_mb(struct dysco_hashes *dh,
					      struct sk_buff *skb,
					      struct dysco_cb_out *dcb_out)
{
	struct tcphdr *th = tcp_hdr(skb);
	
	rhashtable_remove_fast(&dh->dysco_hash_pen, &dcb_out->node,
			       dysco_rhashtable_params_out);

	rhashtable_remove_fast(&dh->dysco_hash_pen_tag, &dcb_out->node,
			       dysco_rhashtable_params_pen);
	
	if (dcb_out->sc->len) {
		dcb_out->sub.dip = dcb_out->sc->hops[0].ip;
		dysco_arp(skb, dcb_out);
	}
	
	dcb_out->sub.sport = allocate_local_port();
	dcb_out->sub.dport = allocate_neighbor_port();
		
	dcb_out->out_iseq = dcb_out->in_iseq = ntohl(th->seq);
	dysco_parse_tcp_syn_opt_s(th, dcb_out);

	dysco_insert_cb_out(dh, skb, dcb_out, FALSE);
	
	dysco_out_hdr_rewrite(skb, dcb_out);

	// Check if the Dysco tag  is still there. The middlebox might
	// have removed.
	if (dcb_out->tag_ok) 
		dysco_remove_tag(skb);
	
	dysco_add_sc(skb, dcb_out);
	dysco_fix_tcp_ip_csum(skb);
	
	return 1;
}
/* dysco_out_handle_mb */


/*********************************************************************
 *
 *	dysco_out_syn: handles a syn packet in the output path.
 *
 *********************************************************************/
static inline netdev_tx_t dysco_out_syn(struct dysco_hashes *dh,
					struct sk_buff *skb,
					struct dysco_cb_out *dcb_out,
					struct tcphdr *th)
{
	if (dcb_out == NULL) {
		struct service_chain	*sc;
		
		if ((sc = dysco_match_policy(dh, skb)) == NULL) 
			return 1;

		dcb_out = dysco_create_cb_out(skb, sc);
		if (dcb_out == NULL) 
			return 1;
		
		dysco_insert_cb_out(dh, skb, dcb_out, FALSE);
		
		if (dev_measurement == NULL) 
			dev_measurement = skb->dev;
	}
	
	dcb_out->seq_cutoff = ntohl(th->seq);
	dysco_parse_tcp_syn_opt_s(th, dcb_out);	
	if (tcp_flag_byte(th) & TCPHDR_ACK) {
		struct tcp_session local_sub;
		struct dysco_cb_in *dcb_in_aux;

		local_sub.sip   = dcb_out->sub.dip;
		local_sub.dip   = dcb_out->sub.sip;
		local_sub.sport = dcb_out->sub.dport;
		local_sub.dport = dcb_out->sub.sport;
		dcb_in_aux = rhashtable_lookup_fast(&dh->dysco_hash_in,
						    &local_sub,
						    dysco_rhashtable_params_in);
		if (dcb_in_aux == NULL) {
			printk(DYSCO_ALERT
			       "WRONG: dcb_in_aux is null at dysco_output\n");
			return 1;
		}
		
		// Save the iseq in the SYN-ACK direction. The ack info
		// could have been saved in the input, but I chose
		// to record all the info in a single place.
		dcb_out->in_iseq = dcb_out->out_iseq = ntohl(th->seq);
		dcb_out->in_iack = dcb_out->out_iack = ntohl(th->ack_seq)-1;

		// Update the seq, ack, and timestamp info in the input direction.
		dcb_in_aux->in_iseq = dcb_in_aux->out_iseq = dcb_out->out_iack;
		dcb_in_aux->in_iack = dcb_in_aux->out_iack = dcb_out->out_iseq;
		
		dcb_in_aux->seq_delta = dcb_in_aux->ack_delta = 0;

		if (dcb_out->ts_ok) {
			dcb_in_aux->ts_ok = 1;
			
			dcb_in_aux->ts_in  = dcb_in_aux->ts_out  = dcb_out->tsr_out;
			dcb_in_aux->tsr_in = dcb_in_aux->tsr_out = dcb_out->ts_out;
		
			dcb_in_aux->ts_delta = dcb_in_aux->tsr_delta = 0;

			printk(DYSCO_ALERT "OUTPUT SYN+ACK: ts=%u tsr=%u\n",
			       dcb_out->ts_out, dcb_out->tsr_out);
		}
		else
			dcb_in_aux->ts_ok = 0;

		if (!dcb_out->sack_ok) {
			// The server side does not accept the sack option.
			dcb_in_aux->sack_ok = 0;
		}
		
		dysco_out_hdr_rewrite_csum(skb, dcb_out);
		return 1;
	}
	else {
		__u32 *mac = (__u32 *)dcb_out->nh_mac;

		// If the four first bytes are zero, I assume the mac is zero.		
		if (!(*mac)) {
			//printk(DYSCO_ALERT "resending ARP request\n");
			dysco_arp(skb, dcb_out);
		}
		
		dysco_out_hdr_rewrite(skb, dcb_out);
		return dysco_out_tx_init(skb, dcb_out);
	}
}
/* dysco_out_syn */


__u64 DYSCO_MAX_INT32 = 4294967296L;
#define MAX_WND_REC	16*1024
/* */

/*********************************************************************
 *
 *	dysco_fix_rcv_window:  this  function   changes  the  receiver
 *	window announcement  to throttle the  sender. It runs  only in
 *	the proxy removal case when two sockets are spliced.
 *
 *********************************************************************/
static inline void dysco_fix_rcv_window(struct sk_buff *skb, struct dysco_cb_out *dcb_out)
{
	struct tcp_sock *my_tp, *other_tp;
	struct tcphdr	*th;
	__u32		rcv_buf; // Variables to compute the ammount of data in the buffers.
	__u32		old_wnd, new_wnd, snd_wnd;
	__u16		new_win, wscale;

	th = tcp_hdr(skb);
	if (!th->window) {
		//printk(DYSCO_ALERT "zero window announced\n");
		return; // If it is already zero, there is nothing to do.
	}
	
	my_tp    = dcb_out->my_tp;
	other_tp = dcb_out->other_tp;
	wscale   = my_tp->rx_opt.rcv_wscale;
	
	old_wnd   = ntohs(th->window);
	old_wnd <<= wscale;
	new_wnd   = old_wnd;

	rcv_buf = my_tp->rcv_nxt;
	if (my_tp->rcv_nxt >= my_tp->copied_seq)
		rcv_buf -= my_tp->copied_seq;
	else
		rcv_buf += DYSCO_MAX_INT32 - other_tp->copied_seq;

	snd_wnd = other_tp->snd_wnd;
	if (rcv_buf > snd_wnd) 
		new_wnd = 0;
	else if ((snd_wnd - rcv_buf) < old_wnd) 
		new_wnd = snd_wnd - rcv_buf;
	
	if (!wscale)
		new_wnd = 0; // This is a hack to avoid large window. Need to lock the sock.
	
	if (rcv_buf > MAX_WND_REC)
		new_wnd = 0;
	else {
		new_wnd = MAX_WND_REC;
		if (new_wnd > old_wnd)
			new_wnd = old_wnd;
	}
	
	new_win = htons(new_wnd >> wscale);
	
	inet_proto_csum_replace2(&th->check, skb, th->window, new_win, FALSE);
	th->window = new_win;
}
/* dysco_fix_rcv_window */


/*********************************************************************
 *
 *	dysco_fix_rcv_window_old:  an  attempt  to  fix  the  receiver
 *	window.  Need to  revist this  code to  find the  best way  of
 *	fixing the window. The current  function uses a fixed constant
 *	to keep packets flowing.
 *
 *********************************************************************/
static inline void dysco_fix_rcv_window_old(struct sk_buff *skb, struct dysco_cb_out *dcb_out)
{
	struct tcp_sock *my_tp, *other_tp;
	struct tcphdr	*th;
	__u32		rcv_buf, snd_buf, tot_buf; // Variables to compute the ammount of data in the buffers.
	__u32		old_wnd, new_wnd;
	__u16		new_win;
	
	th = tcp_hdr(skb);
	if (!th->window)
		return; // If it is already zero, there is nothing to do.
	
	my_tp    = dcb_out->my_tp;
	other_tp = dcb_out->other_tp;
	
	old_wnd   = ntohs(th->window);
	old_wnd <<= my_tp->rx_opt.rcv_wscale;
	new_wnd   = old_wnd;
	
	snd_buf = other_tp->snd_nxt;
	if (other_tp->snd_nxt >= other_tp->snd_una)
		snd_buf -= other_tp->snd_una; // Need to worry about wrap around
	else
		snd_buf += DYSCO_MAX_INT32 - other_tp->snd_una;
	//snd_buf = other_tp->max_window - snd_buf;
	
	rcv_buf = my_tp->rcv_nxt;
	if (my_tp->rcv_nxt >= my_tp->copied_seq)
		rcv_buf -= my_tp->copied_seq;
	else
		rcv_buf += DYSCO_MAX_INT32 - other_tp->copied_seq;

	tot_buf = snd_buf + rcv_buf;
	if (tot_buf >= other_tp->max_window) {
		inet_proto_csum_replace2(&th->check, skb, th->window, 0, FALSE);
		th->window = 0;
	}
	else if ((other_tp->max_window - tot_buf) < old_wnd) {
		new_wnd = other_tp->max_window - tot_buf;
		new_win = htons(new_wnd >> my_tp->rx_opt.rcv_wscale);
		inet_proto_csum_replace2(&th->check, skb, th->window, new_win, FALSE);
		th->window = new_win;
	}

	/*
	printk(DYSCO_ALERT "fix_rcv_window my_sip=" IP_STR " my_sport=%u my_dip=" IP_STR
	       " my_dport=%u old_wnd %u new_wnd %u old_hdr %u hdr_wnd %u snd_buf %u rcv_buf %u tp->snd_wnd %u\n",
	       IP_TO_STR(dcb_out->sub.sip), dcb_out->sub.sport,
	       IP_TO_STR(dcb_out->sub.dip), dcb_out->sub.dport,
	       old_wnd, new_wnd, old_wnd >> my_tp->rx_opt.rcv_wscale,
	       ntohs(th->window), snd_buf, rcv_buf, other_tp->snd_wnd);
	*/
	
	/*
	printk(DYSCO_ALERT "fix_rcv_window my_sip=" IP_STR " my_sport=%u my_dip=" IP_STR
	       " my_dport=%u th->window=%u window=%u other_tp->max_window=%u snd_buf=%u, rcv_buf=%u "
	       "other_tp(snd_nxt=%u snd_una=%u) other_tp(rcv_nxt=%u copied_seq=%u) "
	       "my_tp(snd_nxt=%u snd_una=%u) my_tp(rcv_nxt=%u copied_seq=%u)\n",
	       IP_TO_STR(dcb_out->sub.sip), dcb_out->sub.sport,
	       IP_TO_STR(dcb_out->sub.dip), dcb_out->sub.dport,
	       ntohs(th->window), window, other_tp->max_window, snd_buf, rcv_buf,
	       other_tp->snd_nxt, other_tp->snd_una,
	       other_tp->rcv_nxt, other_tp->copied_seq,
	       my_tp->snd_nxt, my_tp->snd_una,
	       my_tp->rcv_nxt, my_tp->copied_seq);
	*/
}
/* dysco_fix_rcv_window_old */


/*********************************************************************
 *
 *	dysco_output: runs in process context  at the end hosts and in
 *	softirq  context in  the  middleboxes. Must  be called  within
 *	rcu_read_lock section.
 *
 *********************************************************************/
netdev_tx_t dysco_output(struct sk_buff *skb, struct net_device *dev)
{
	struct tcphdr		*th;
	struct iphdr		*iph = ip_hdr(skb);
	struct dysco_cb_out	*dcb_out;
	struct dysco_hashes	*dh;

#ifdef DYSCO_MEASUREMENT_OUTPUT_TRANSLATE
	struct timespec	local_before, local_after;
	
	if (skb->dev == dev_measurement)
		getnstimeofday(&local_before);		
#endif

	/* Sanity checks */
	if (unlikely(ntohs(eth_hdr(skb)->h_proto) != ETH_P_IP))
		return 1;

	// Need to test  if transport header is set.  When packets are
	// injected via raw socket, the transport header is not set.
	if (skb->transport_header == skb->network_header)
		skb->transport_header = skb->network_header + iph->ihl * 4;

	//return 1;
	
	rcu_assign_pointer(dh, dysco_get_hashes(dev_net(skb->dev)));
	if (dh == NULL) {
		printk(DYSCO_ALERT "BUG: no namespace found\n");
		return 1;
	}

	if (iph->protocol == IPPROTO_UDP)
		return dysco_control_output(dh, skb);
	
	if (iph->protocol != IPPROTO_TCP)
		return 1;
	
	dcb_out = dysco_out_lookup(dh, skb);
	if (dcb_out == NULL) {
		dcb_out = dysco_lookup_pending(dh, skb);
		if (dcb_out != NULL) {
			// output from a middlebox
			dysco_out_handle_mb(dh, skb, dcb_out);
			return 1;
		}
		
		dcb_out = dysco_lookup_pen_tag(dh, skb);
		if (dcb_out != NULL) {
			// output from a middlebox
			// If we get here, the middlebox changed the five-tuple.
			dysco_update_five_tuple(skb, dcb_out);	
			dysco_out_handle_mb(dh, skb, dcb_out);
			return 1;
		}
	}

	th = tcp_hdr(skb);	
	if (tcp_flag_byte(th) & TCPHDR_SYN)
		return dysco_out_syn(dh, skb, dcb_out, th);
	
	if (dcb_out == NULL)
		return 1;

	// my_tp is updated by a control message. It points to the TCP control
	// block of this session.

	
	if (dcb_out->my_tp && tcp_flag_byte(th) & TCPHDR_ACK) {
		if (!dcb_out->state_t)
			dysco_fix_rcv_window(skb, dcb_out);
	}
	
	
#ifdef DYSCO_MEASUREMENT_OUTPUT_TRANSLATE
	if (skb->dev == dev_measurement) {
		static unsigned	n_samples = 0;
		
		dysco_out_translate(skb, dcb_out);
		getnstimeofday(&local_after);
		if (local_after.tv_sec == local_before.tv_sec) {
			translate_output_samples[n_samples++] =
				local_after.tv_nsec - local_before.tv_nsec;
			if (n_samples == DYSCO_MEASUREMENT_SAMPLES) {
				n_samples	= 0;
				dev_measurement = NULL;
			}
		}
		return 1;
	}
#endif
	
	dysco_out_translate(skb, dcb_out);
	return 1;
}
/* dysco_output */
