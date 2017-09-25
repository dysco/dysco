/*
 *
 *	Dynamic Service Chaining with Dysco
 *	Dysco kernel agent: dysco_tcp.c
 *
 *	This module implements functions that are specifics to TCP.
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
 *	dysco_insert_tag: insert a  TCP option used as a  local tag to
 *	identify packets whose "five-tuple" might change.
 *
 *********************************************************************/
void dysco_insert_tag(struct dysco_hashes *dh,
		      struct sk_buff *skb,
		      struct iphdr *iph,
		      struct tcphdr *th,
		      struct dysco_cb_in *dcb_in)
{
	unsigned int			tag;
	struct dysco_tcp_option		*dopt;
	
	spin_lock_bh(&dh->tag_lock);
	tag = dh->dysco_tag++;
	spin_unlock_bh(&dh->tag_lock);

	dopt = (struct dysco_tcp_option *)skb_put(skb, DYSCO_TCP_OPTION_LEN);
	
	dopt->kind    = DYSCO_TCP_OPTION;
	dopt->len     = DYSCO_TCP_OPTION_LEN;
	dopt->padding = 0;
	dopt->tag     = tag;	// no need to change byte order because the tag
				// has local meaning only.

	th->doff    += DYSCO_TCP_OPTION_LEN / 4;	
	iph->tot_len = htons(ntohs(iph->tot_len) + DYSCO_TCP_OPTION_LEN);
}
/* dysco_insert_tag */


/*********************************************************************
 *
 *	dysco_parse_tcp_syn_opt_r:  parses TCP  options  in the  input
 *	path and stores the relevant  information in the input control
 *	block. This function parses only the SYN and SYN+ACK packets.
 *
 *********************************************************************/
void dysco_parse_tcp_syn_opt_r(struct tcphdr *th,
			       struct dysco_cb_in *dcb_in)
{
	const unsigned char	*ptr;
	int			length;

	length = (th->doff*4) - sizeof(struct tcphdr);
	ptr    = (const unsigned char *)(th + 1);

	dcb_in->sack_ok = 0;
	
	while (length > 0) {
		int opcode, opsize;

		opcode = *ptr++;
		switch (opcode) {
		case TCPOPT_EOL:
			return;
			
		case TCPOPT_NOP:
			length--;
			continue;

		default:
			opsize = *ptr++;
			if (opsize < 2)
				return;
			if (opsize > length)
				return;
			switch (opcode) {

			case TCPOPT_WINDOW:
				if (opsize == TCPOLEN_WINDOW) {
					__u8 snd_wscale = *(__u8 *)ptr;
					
					dcb_in->ws_ok    = 1;
					dcb_in->ws_delta = 0;
					if (snd_wscale > 14) {
						printk(DYSCO_ALERT
						       "%s: Illegal window scaling value %d >14 received\n",
						       __func__,
						       snd_wscale);
						snd_wscale = 14;
					}
					dcb_in->ws_in = dcb_in->ws_out = snd_wscale;
					
				}
				break;
				
			case TCPOPT_TIMESTAMP:
				if (opsize == TCPOLEN_TIMESTAMP) {
					if (tcp_flag_byte(th) & TCPHDR_ACK) {
						__u32 ts, tsr;
						
						dcb_in->ts_ok = 1;
						ts  = get_unaligned_be32(ptr);		// Return in host byte order. 
						tsr = get_unaligned_be32(ptr + 4);	// Don't need to convert.
						dcb_in->ts_in  = dcb_in->ts_out  = ts;
						dcb_in->tsr_in = dcb_in->tsr_out = tsr;
						
						dcb_in->ts_delta = dcb_in->tsr_delta = 0;
					}
				}
				break;
				
			case TCPOPT_SACK_PERM:
				if (opsize == TCPOLEN_SACK_PERM) {
					dcb_in->sack_ok = 1;
				}
				break;				
			}
			ptr += opsize-2;
			length -= opsize;			
		}
	}
}
/* dysco_parse_tcp_syn_opt_r */


/*********************************************************************
 *
 *	dysco_parse_tcp_syn_opt_s:  parses TCP  options in  the output
 *	path and stores the relevant information in the output control
 *	block. This function parses only the SYN and SYN+ACK packets.
 *
 *********************************************************************/
void dysco_parse_tcp_syn_opt_s(struct tcphdr *th,
			       struct dysco_cb_out *dcb_out)
{
	const unsigned char	*ptr;
	int			length;

	length = (th->doff*4) - sizeof(struct tcphdr);
	ptr    = (const unsigned char *)(th + 1);

	dcb_out->sack_ok = 0;
	
	while (length > 0) {
		int opcode, opsize;

		opcode = *ptr++;
		switch (opcode) {
		case TCPOPT_EOL:
			return;
			
		case TCPOPT_NOP:
			length--;
			continue;

		default:
			opsize = *ptr++;
			if (opsize < 2)
				return;
			if (opsize > length)
				return;
			switch (opcode) {

			case TCPOPT_WINDOW:
				if (opsize == TCPOLEN_WINDOW) {
					__u8 snd_wscale = *(__u8 *)ptr;
					
					dcb_out->ws_ok    = 1;
					dcb_out->ws_delta = 0;
					if (snd_wscale > 14) {
						printk(DYSCO_ALERT
						       "%s: Illegal window scaling value %d >14 received\n",
						       __func__,
						       snd_wscale);
						snd_wscale = 14;
					}
					dcb_out->ws_in = dcb_out->ws_out = snd_wscale;
				}
				break;
				
			case TCPOPT_TIMESTAMP:
				if (opsize == TCPOLEN_TIMESTAMP) {
					// Get timestamp information only from the SYN+ACK packet.
					if (tcp_flag_byte(th) & TCPHDR_ACK) {
						__u32 ts, tsr;
						
						dcb_out->ts_ok    = 1;
						ts  = get_unaligned_be32(ptr);
						tsr = get_unaligned_be32(ptr + 4);
						dcb_out->ts_in  = dcb_out->ts_out  = ts;
						dcb_out->tsr_in = dcb_out->tsr_out = tsr;
						
						dcb_out->ts_delta = dcb_out->tsr_delta = 0;
					}
				}
				break;
				
			case TCPOPT_SACK_PERM:
				if (opsize == TCPOLEN_SACK_PERM) {
					dcb_out->sack_ok = 1;
				}
				break;

			case DYSCO_TCP_OPTION:
				dcb_out->tag_ok    = 1;
				dcb_out->dysco_tag = *(u32 *)ptr;
				break;
				
			}
			ptr += opsize-2;
			length -= opsize;			
		}
	}
}
/* dysco_parse_tcp_syn_opt_s */
