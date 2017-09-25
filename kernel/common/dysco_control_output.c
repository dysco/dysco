/*
 *
 *	Dynamic Service Chaining with Dysco
 *	Dysco kernel agent: dysco_control_output.c
 *
 *	This module implements the Dysco control protocol when packets
 *	leave a host.
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
#include "dysco.h"
/* */

__be16	dysco_control_port = htons(DYSCO_SERVER_PORT);
__be16	dysco_mgm_port = htons(DYSCO_MANAGEMENT_PORT);
/* */


/*********************************************************************
 *
 *	dysco_fix_udp_chekcsum: computes the UDP checksum from scratch.
 *
 *********************************************************************/
static inline void dysco_fix_udp_checksum(struct sk_buff *skb)
{
	struct iphdr	*iph;
	struct udphdr	*uh;
	unsigned char	*data;
	__u16		udp_len;

	iph   = ip_hdr(skb);
	data  = (unsigned char *)iph;
	data +=  iph->ihl<<2;
	uh    = (struct udphdr *)data;
	
	uh->check = 0;
	udp_len   = ntohs(uh->len);
	uh->check = csum_fold(csum_partial(uh, udp_len,
					   csum_tcpudp_nofold(iph->saddr,
							      iph->daddr,
							      udp_len,
							      IPPROTO_UDP, 0)));
	skb->ip_summed = CHECKSUM_UNNECESSARY;
}
/* dysco_fix_udp_chekcsum */


/*********************************************************************
 *
 *	dysco_insert_cb_control:  inserts  a  reconfiguration  control
 *	block. Most  fields of the  control block are filled  with the
 *	data in the  control message. It also allocates  ports for the
 *	subsession.
 *
 *********************************************************************/
struct dysco_cb_reconfig *
dysco_insert_cb_control(struct dysco_hashes *dh, struct sk_buff *skb,
			struct control_message *cmsg)
{
	struct iphdr			*iph;
	struct dysco_cb_reconfig	*rcb;
	
	rcb = kmalloc(sizeof(struct dysco_cb_reconfig), GFP_KERNEL);
	if (rcb == NULL) {
		printk(DYSCO_ALERT "could not allocate memory for dcb\n");
		return NULL;
	}
	
	iph = ip_hdr(skb);
	
	// Record the time of the beginning of the reconfiguration
	getnstimeofday(&rcb->rec_begin); // = current_kernel_time();
	rcb->rec_done = FALSE;
	
	rcb->super         = cmsg->leftSS;
	rcb->sub_out.sip   = iph->saddr;
	rcb->sub_out.dip   = iph->daddr;
	rcb->sub_out.sport = allocate_local_port();
	rcb->sub_out.dport = allocate_neighbor_port();

	// Initial sequence and ack numbers.
	rcb->leftIseq	   = ntohl(cmsg->leftIseq);
	rcb->leftIack	   = ntohl(cmsg->leftIack);

	// TCP timestamp option.
	rcb->leftIts  = ntohl(cmsg->leftIts);
	rcb->leftItsr = ntohl(cmsg->leftItsr);

	// TCP window scaling option.
	rcb->leftIws  = ntohs(cmsg->leftIws);
	rcb->leftIwsr = ntohs(cmsg->leftIwsr);

	// Is the sack option negotiated.
	rcb->sack_ok = ntohs(cmsg->sackOk);
	
	// skb->data points to dst mac. Copy the mac to avoid future ARP calls.
	memcpy(rcb->nh_mac, skb->data, ETHER_ADDR_LEN); 

	printk(DYSCO_ALERT "nh_mac=" MAC_STR "\n", MAC_TO_STR(rcb->nh_mac));
	
	cmsg->sport = rcb->sub_out.sport;
	cmsg->dport = rcb->sub_out.dport;
	
	dysco_fix_udp_checksum(skb);
	
	rhashtable_insert_fast(&dh->dysco_hash_reconfig,
			       &rcb->node,
			       dysco_rhashtable_params_reconfig);	
	return rcb;
}
/* dysco_insert_cb_control */


/*********************************************************************
 *
 *	dysco_control_insert_out:  inserts   a  control  block   in  a
 *	middlebox  that  is neither  the  left  anchor nor  the  right
 *	anchor.   It   basically  copies  the  information   from  the
 *	reconfiguration control block to the output control block. The
 *	function  dysco_insert_cb_out calls  a  function  to insert  a
 *	control block  for the reverse  direction, i.e., in  the input
 *	path.
 *
 *********************************************************************/
void dysco_control_insert_out(struct dysco_hashes *dh,
			      struct sk_buff *skb,
			      struct dysco_cb_reconfig *rcb)
{
	struct dysco_cb_out *dcb_out;
	struct dysco_cb_in  *dcb_in;
	
	dcb_out = kzalloc(sizeof(struct dysco_cb_out), GFP_KERNEL);
	if (dcb_out == NULL) {
		printk(DYSCO_ALERT "could not allocate memory for dcb_out out\n");
		// FIXME: need to remove rcb from the reconfiguration table.
		return;
	}
	
	
	// FIXME: Need to fix the code below. The code should work fine for
	// middlebox removal in which we have only left and right anchors.
	dcb_out->super    = rcb->super;
	dcb_out->sub      = rcb->sub_out;
	
	dcb_out->out_iseq = dcb_out->in_iseq  = rcb->leftIseq;
	dcb_out->out_iack = dcb_out->in_iack  = rcb->leftIack;

	dcb_out->ts_out  = dcb_out->ts_in  = rcb->leftIts;
	dcb_out->tsr_out = dcb_out->tsr_in = rcb->leftItsr;
	
	dcb_out->ws_out = dcb_out->ws_in = rcb->leftIws;

	dcb_out->sack_ok = rcb->sack_ok;
	
	spin_lock_init(&dcb_out->seq_lock);
	
	memcpy(dcb_out->nh_mac, rcb->nh_mac, ETHER_ADDR_LEN);
	dysco_arp(skb, dcb_out);
	
	dysco_insert_cb_out(dh, skb, dcb_out, FALSE);
	
	dcb_in = dcb_out->dcb_in;
	dcb_in->ts_in  = dcb_in->ts_out  = dcb_out->tsr_out;
	dcb_in->tsr_in = dcb_in->tsr_out = dcb_out->ts_out;
}
/* dysco_control_insert_out */


/*********************************************************************
 *
 *	dysco_replace_cb_rightA: makes  a new control block  active at
 *	the right anchor.  It also  fills the control message with the
 *	sequence  number  cutoff.  This  function  is  called  when  a
 *	DYSCO_SYN_ACK message  is sent  from the  right anchor  to the
 *	left anchor.
 *
 *********************************************************************/
void dysco_replace_cb_rightA(struct dysco_hashes *dh,
			     struct sk_buff *skb,
			     struct control_message *cmsg)
{
	struct dysco_cb_out		*old_out, *new_out;
	struct dysco_cb_reconfig	*rcb;
	__u32				seq_cutoff;
			
	rcb = rhashtable_lookup_fast(&dh->dysco_hash_reconfig,
				     &cmsg->super,
				     dysco_rhashtable_params_reconfig);
	if (!rcb) {
		printk(DYSCO_ALERT "BUG: no out CB at the right anchor\n");
		return;
	}
			
	old_out = rcb->old_dcb;
	new_out = rcb->new_dcb;

	// Begin of critical section
	spin_lock_bh(&old_out->seq_lock);			
	seq_cutoff	    = old_out->seq_cutoff;
	old_out->old_path   = TRUE;
	old_out->state      = DYSCO_SYN_RECEIVED;
	old_out->other_path = new_out;
	spin_unlock_bh(&old_out->seq_lock);			
	// End of critical section
			
	// Fix sequence number outside the spin lock
	if (new_out->seq_add)
		seq_cutoff += new_out->seq_delta;
	else
		seq_cutoff -= new_out->seq_delta;
	
	cmsg->seqCutoff = htonl(seq_cutoff);
	dysco_fix_udp_checksum(skb);
}
/* dysco_replace_cb_rightA */


/*********************************************************************
 *
 *	dysco_replace_cb_leftA: makes  a new  control block  active at
 *	the left anchor by changing its state to DYSCO_ESTABLISHED. It
 *	also  fills  the  control  message with  the  sequence  number
 *	cutoff. This  function is called  when a DYSCO_ACK  message is
 *	sent from the left anchor to the right anchor.
 *
 *********************************************************************/
void dysco_replace_cb_leftA(struct sk_buff *skb,
			    struct dysco_cb_reconfig *rcb,
			    struct control_message *cmsg)
{
	struct dysco_cb_out	*old_dcb;

	old_dcb = rcb->old_dcb;
	
	// Begin of critical section
	spin_lock_bh(&old_dcb->seq_lock);
	// If it is not DYSCO_SYN_SENT, it is a retransmission.
	if (old_dcb->state == DYSCO_SYN_SENT) 
		old_dcb->state = DYSCO_ESTABLISHED;
	spin_unlock_bh(&old_dcb->seq_lock);
	// End of critical section
	
	cmsg->seqCutoff = htonl(old_dcb->seq_cutoff);
	dysco_fix_udp_checksum(skb);	

	if (!rcb->rec_done) {
		getnstimeofday(&rcb->rec_end); // = current_kernel_time();
		rcb->rec_done = TRUE;
	}
}
/* dysco_replace_cb_leftA */


/*********************************************************************
 *
 *	dysco_control_output_syn: processes  the DYSCO_SYN  message in
 *	the output  path. It  creates a reconfiguration  control block
 *	and fills the  control message with the  information stored at
 *	the output control block.
 *
 *********************************************************************/
static inline int
dysco_control_output_syn(struct dysco_hashes *dh, struct sk_buff *skb,
			 struct control_message *cmsg,
			 struct iphdr *iph)
{
	struct dysco_cb_reconfig	*rcb;
			
	rcb = rhashtable_lookup_fast(&dh->dysco_hash_reconfig,
				     &cmsg->super,
				     dysco_rhashtable_params_reconfig);

	// Check if it is the left anchor.
	if (iph->saddr == cmsg->leftA) { 
		struct dysco_cb_out	*old_dcb, *new_dcb;
		
		if (rcb) {
			// It is a retransmission. Need to fix cmsg
			// printk(DYSCO_ALERT "resending a DYSCO_SYN message\n");

			// Sequence numbers.
			cmsg->leftIseq = htonl(rcb->leftIseq);
			cmsg->leftIack = htonl(rcb->leftIack);

			// Timestamp options.
			cmsg->leftIts  = htonl(rcb->leftIts);
			cmsg->leftItsr = htonl(rcb->leftItsr);

			// Window scaling options.
			cmsg->leftIws  = htons(rcb->leftIws);
			cmsg->leftIwsr = htons(rcb->leftIwsr);

			// SACK option negotiated?
			cmsg->sackOk = rcb->sack_ok;
			cmsg->sackOk = htons(cmsg->sackOk);

			// Allocated source and  destination ports for
			// the subsession.
			cmsg->sport    = rcb->sub_out.sport;
			cmsg->dport    = rcb->sub_out.dport;
			
			dysco_fix_udp_checksum(skb);
			return 1;
		}
		else {
			old_dcb = rhashtable_lookup_fast(&dh->dysco_hash_out,
							 &cmsg->leftSS,
							 dysco_rhashtable_params_out);
			if (!old_dcb) {
				printk(DYSCO_ALERT
				       "WRONG: did not find leftCB at out\n");
				printk(DYSCO_ALERT
				       "WRONG: sip=" IP_STR " dip=" IP_STR
				       " sport=%u dport=%u\n",
				       IP_TO_STR(cmsg->leftSS.sip),
				       IP_TO_STR(cmsg->leftSS.dip),
				       ntohs(cmsg->leftSS.sport),
				       ntohs(cmsg->leftSS.dport));
				return 0;
			}

			// Stores the initial sequence and ack numbers
			// in the control message.
			cmsg->leftIseq = htonl(old_dcb->in_iseq);
			cmsg->leftIack = htonl(old_dcb->in_iack);		

			// Stores the initial timestamp options in the
			// control message.
			cmsg->leftIts  = htonl(old_dcb->ts_in);
			cmsg->leftItsr = htonl(old_dcb->tsr_in);

			// Stores the  initial window  scaling options
			// in the control message.			
			cmsg->leftIws  = htons(old_dcb->ws_in);
			cmsg->leftIwsr = htons(old_dcb->dcb_in->ws_in);

			// Is the sack option negotiated?
			cmsg->sackOk = old_dcb->sack_ok;
			cmsg->sackOk = htons(cmsg->sackOk);
			
			rcb = dysco_insert_cb_control(dh, skb, cmsg);
			if (!rcb)
				return 0;
		}

		new_dcb = kzalloc(sizeof(struct dysco_cb_out), GFP_ATOMIC);
		if (new_dcb == NULL) {
			printk(DYSCO_ALERT
			       "could not allocate memory for dcb out\n");
			// FIXME: need to remove rcb from the reconfiguraion table.
			return 0;
		}
		
		// Set the cb to avoid future lookups.
		rcb->old_dcb = old_dcb; 
		
		new_dcb->super    = rcb->super;
		new_dcb->sub      = rcb->sub_out;	
		
		new_dcb->out_iseq = new_dcb->in_iseq  = rcb->leftIseq;
		new_dcb->out_iack = new_dcb->in_iack  = rcb->leftIack;

		new_dcb->ts_out  = new_dcb->ts_in  = rcb->leftIts;
		new_dcb->tsr_out = new_dcb->tsr_in = rcb->leftItsr;
		
		new_dcb->ws_out = new_dcb->ws_in = rcb->leftIws;

		new_dcb->ts_ok = rcb->leftIts ? 1 : 0;
		new_dcb->ws_ok = rcb->leftIws ? 1 : 0;

		new_dcb->sack_ok = rcb->sack_ok;
		
		memcpy(new_dcb->nh_mac, rcb->nh_mac, ETHER_ADDR_LEN);

		dysco_arp(skb, new_dcb);
		
		// Not using this spin lock for now. I will use it in the future.		
		spin_lock_init(&new_dcb->seq_lock);	
			
		new_dcb->other_path   = old_dcb;
		
		new_dcb->dcb_in = dysco_insert_cb_out_reverse(dh, new_dcb, TRUE);
			
		old_dcb->old_path = TRUE;

		if (ntohs(cmsg->semantic) == STATE_TRANSFER) {
			// printk(DYSCO_ALERT
			// "setting state_t to TRUE in dysco_control_output_syn\n");
			old_dcb->state_t = TRUE;
		}
		
		// Begin of critical section
		spin_lock_bh(&old_dcb->seq_lock);
		
		// The reverse path must know that it has two paths now.
		old_dcb->dcb_in->two_paths = TRUE;
		old_dcb->state = DYSCO_SYN_SENT;
		
		// Start using other_path in dysco_output
		//rcu_assign_pointer(old_dcb->other_path, new_dcb); 
		old_dcb->other_path = new_dcb;
		spin_unlock_bh(&old_dcb->seq_lock);
		// End of critical section
		
		return 1;
	}

	if (rcb && rcb->sub_out.sip != 0) {
		// This is a retransmission
		return 1;
	}
	
	// rcb was inserted in the input if rcb not null. Need to allocate
	// new ports. The call bellow does the trick. This is equivalent
	// to handle_mb_out
	rcb = dysco_insert_cb_control(dh, skb, cmsg);
	if (!rcb) {
		// Problem! Could not allocate memory.
		return 0; 
	}
	
	// If it is not the left anchor, insert data path state.
	// Otherwise, wait for the ACK message to switch to the
	// new path.
	dysco_control_insert_out(dh, skb, rcb);
	return 1;
}
/* dysco_control_output_syn */


/*********************************************************************
 *
 *	dysco_ctl_save_rcv_window: this function is for the removal of
 *	a TCP  terminating middlebox.  It saves the  information about
 *	the two TCP sessions in the two output control control blocks.
 *
 *********************************************************************/
static inline int
dysco_ctl_save_rcv_window(struct dysco_hashes *dh,
			  struct control_message *cmsg)
{
	struct dysco_cb_out *left_dcb, *right_dcb;

	left_dcb = dysco_lookup_out_rev(&dh->dysco_hash_out, &cmsg->leftSS);
	if (left_dcb == NULL) {
		printk(DYSCO_ALERT "BUG: no left_dcb in save_rcv_window\n");
		return 0;
	}
	
	// If different from NULL, it is a retransmission of the control message.
	if (left_dcb->my_tp == NULL) {
		struct tcp_session	*ts;
		struct sock		*my_sk, *other_sk;
		
		right_dcb = rhashtable_lookup_fast(&dh->dysco_hash_out,
						   &cmsg->rightSS,
						   dysco_rhashtable_params_out);
		if (right_dcb == NULL) {
			printk(DYSCO_ALERT
			       "BUG: no right_dcb in save_rcv_window\n"
			       IP_STR IP_STR "sport=%u dport=%d\n",
			       IP_TO_STR(cmsg->rightSS.sip), IP_TO_STR(cmsg->rightSS.dip),
			       ntohs(cmsg->rightSS.sport), ntohs(cmsg->rightSS.dport));
			return 0;
		}
		
		ts = &cmsg->leftSS;		
		my_sk = __inet_lookup_established(dh->net_ns, &tcp_hashinfo,
						  ts->sip, ts->sport, ts->dip,
						  ntohs(ts->dport),
						  left_dcb->dcb_in->skb_iif);
		if (my_sk == NULL) {
			printk(DYSCO_ALERT "could not find TCP control block for leftSS\n");
			return 0;
		}
		
		ts = &cmsg->rightSS;
		other_sk = __inet_lookup_established(dh->net_ns, &tcp_hashinfo,
						     ts->dip, ts->dport, ts->sip,
						     ntohs(ts->sport),
						     right_dcb->dcb_in->skb_iif);
		if (other_sk == NULL) {
			printk(DYSCO_ALERT "could not find TCP control block for rightSS\n");
			return 0;
		}
		
		// Pointer assingment is atomic, so it does not need a lock.
		left_dcb->other_tp  = tcp_sk(other_sk);
		left_dcb->my_tp     = tcp_sk(my_sk);
		
		right_dcb->other_tp = tcp_sk(my_sk);
		right_dcb->my_tp    = tcp_sk(other_sk);
	}
	return 1;
}
/* dysco_ctl_save_rcv_window */


/*********************************************************************
 *
 *	dysco_control_output: processes UDP  control packets when they
 *	leave a host.
 *
 *********************************************************************/
netdev_tx_t dysco_control_output(struct dysco_hashes *dh,
				 struct sk_buff *skb) 
{
	struct iphdr			*iph;
	struct udphdr			*uh;
	struct in_device		*in_dev;
	unsigned char			*data;
	struct control_message		*cmsg;
	struct dysco_cb_reconfig	*rcb;
	__u16				udp_len;
	unsigned char			found;

	iph = ip_hdr(skb);
	data  = (unsigned char *)iph;
	data += iph->ihl<<2;
	uh    = (struct udphdr *)data;
	if (!(uh->dest   == dysco_control_port ||
	      uh->source == dysco_control_port ||
	      uh->dest   == dysco_mgm_port))
		return 1;
	
	udp_len = ntohs(uh->len);	
	if (udp_len < UDP_HDR_LEN+RUDP_HDR_LEN+sizeof(struct control_message)) {
		return 1;
	}

	rcu_assign_pointer(in_dev, skb->dev->ip_ptr);
	// The  for is  necessary in  case the  device has  virtual IP
	// interfaces.
	found = FALSE;
	for_ifa(in_dev) {
		if (iph->saddr == ifa->ifa_address) {
			found = TRUE;
			break;
		}
	} endfor_ifa(in_dev);
	
	if (!found)
		return 1;
		
	// The  9 below  is the  size  of the  header of  reliable-udp
	// (RUDP_HDR_LEN)
	data  = (unsigned char *)uh;
	data += sizeof(struct udphdr) + RUDP_HDR_LEN; 
	cmsg  = (struct control_message *)data;

	if (uh->dest == dysco_mgm_port) 
		return dysco_ctl_save_rcv_window(dh, cmsg);
	
	switch (cmsg->mtype) {
	case DYSCO_SYN:
		//printk(DYSCO_ALERT "sending a DYSCO_SYN message\n");
		return dysco_control_output_syn(dh, skb, cmsg, iph);
		// FIXME: Testing packet losses
		//		if (iph->saddr == cmsg->leftA) { // Check if it is the left anchor.
		//	printk(DYSCO_ALERT "DROPPING a DYSCO_SYN message\n");
		//	return 0;
		//}

	case DYSCO_SYN_ACK:
		//printk(DYSCO_ALERT "sending a DYSCO_SYN_ACK message\n");
		if (iph->saddr == cmsg->rightA)
			dysco_replace_cb_rightA(dh, skb, cmsg);
		break;
		
	case DYSCO_ACK:
		// printk(DYSCO_ALERT "sending a DYSCO_ACK message\n");
		rcb = rhashtable_lookup_fast(&dh->dysco_hash_reconfig,
					     &cmsg->super,
					     dysco_rhashtable_params_reconfig);
		if (rcb == NULL) {
			printk(DYSCO_ALERT "BUG: lookup in DYSCO_ACK returned NULL\n");
			return 1;
		}

		if (ntohs(cmsg->semantic) == STATE_TRANSFER)
			return 1;

		if (iph->saddr == cmsg->leftA) {
			if (!rcb->old_dcb->state_t)
				dysco_replace_cb_leftA(skb, rcb, cmsg);
		}
		break;

	case DYSCO_ACK_ACK:
		// printk(DYSCO_ALERT "sending a DYSCO_ACK_ACK message\n");
		break;

	case DYSCO_FIN:
		//printk(DYSCO_ALERT "sending a DYSCO_FIN message\n");
		break;

	case DYSCO_STATE_TRANSFERRED:
		//printk(DYSCO_ALERT "sending a DYSCO_STATE_TRANSFERRED message\n");
		break;
		
	default:
		printk(DYSCO_ALERT "sending an UNKNOWN control message");
	}
	return 1;
}
/* dysco_control_output */
