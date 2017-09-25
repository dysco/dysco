/*
 *
 *	Dynamic Service Chaining with Dysco
 *	Dysco kernel agent: d.c
 *
 *	Dysco module for Linux Netfilter
 *
 *	Author: Ronaldo A. Ferreira (raf@facom.ufms.br)
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
#include <net/xfrm.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/inetdevice.h>

#include "../common/dysco.h"

static struct nf_hook_ops netfilter_ops_in;  /* IP PRE ROUTING     */
static struct nf_hook_ops netfilter_ops_out; /* NF_IP_POST_ROUTING */
/* */


/*********************************************************************
 *
 *	dysco_nf_in: input processing for Linux Netfilter.
 *
 *********************************************************************/
static unsigned int dysco_nf_in(void *priv,
				struct sk_buff *skb,
				const struct nf_hook_state *state)
{
	struct in_device	*in_dev;
	
	// This is necessary to avoid processing packets that are not destined
	// to this host. For example, packets that cross a router running the
	// Dysco module. Also, the for loop handles virtual IP interfaces.
	rcu_assign_pointer(in_dev, skb->dev->ip_ptr);
	for_ifa(in_dev) {
		if (ip_hdr(skb)->daddr == ifa->ifa_address) {
			int ret;
			
			rcu_read_lock();
			ret = dysco_input(skb);
			rcu_read_unlock();
			if (ret) 
				return NF_ACCEPT;
			else
				return NF_DROP;
		}
	} endfor_ifa(in_dev);
	return NF_ACCEPT;
}
/* dysco_nf_in */


/*********************************************************************
 *
 *	dysco_nf_out: output processing for Linux Netfilter.
 *
 *********************************************************************/
static unsigned int dysco_nf_out(void *priv,
				 struct sk_buff *skb,
				 const struct nf_hook_state *state)
{
	int ret;
	
	skb_push(skb, ETHER_HDR_LEN);
	skb_reset_mac_header(skb);
	eth_hdr(skb)->h_proto = htons(ETH_P_IP);	

	rcu_read_lock();
	ret = dysco_output(skb, state->out);
	rcu_read_unlock();
	
	skb_pull(skb, ETHER_HDR_LEN);
	
	if (ret == 1)
		return NF_ACCEPT;
	else
		return NF_DROP;
}
/* dysco_nf_out */


/*********************************************************************
 *
 *	dysco_init_nf: initializes the Linux Netfilter hooks.
 *
 *********************************************************************/
void dysco_init_nf(void)
{
	netfilter_ops_in.hook     = dysco_nf_in;
	netfilter_ops_in.pf       = NFPROTO_IPV4; //PF_INET;
	netfilter_ops_in.hooknum  = NF_INET_PRE_ROUTING; 
	netfilter_ops_in.priority = NF_IP_PRI_FIRST;
	
	nf_register_hook(&netfilter_ops_in);	

	netfilter_ops_out.hook     = dysco_nf_out;
	netfilter_ops_out.pf       = NFPROTO_IPV4; //PF_INET;
	netfilter_ops_out.hooknum  = NF_INET_POST_ROUTING; 
	netfilter_ops_out.priority = NF_IP_PRI_FIRST;
	
	nf_register_hook(&netfilter_ops_out);	
}
/* dysco_init_nf */


/*********************************************************************
 *
 *	dysco_start:  unitializes  the  Dysco  module  and  the  Linux
 *	Netfilter hooks.
 *
 *********************************************************************/
static __init int dysco_start(void)
{
	int ret;

	ret = dysco_init();
	if (ret < 0) {
		printk(DYSCO_ALERT "Dysco can't be initialized\n");
		return ret;
	}
	dysco_init_nf();
	
	return ret;
}
/* dysco_start */


/*********************************************************************
 *
 *	dysco_exit: unregisters  the Linux Netfilter hooks  and cleans
 *	up the Dysco data structures.
 *
 *********************************************************************/
static __exit void dysco_exit(void)
{
	nf_unregister_hook(&netfilter_ops_in);
	nf_unregister_hook(&netfilter_ops_out);
	
	dysco_deinit();
}
/* dysco_exit */


module_init(dysco_start);
module_exit(dysco_exit);


MODULE_DESCRIPTION("Dysco - Dynamic Service Chaining");
MODULE_AUTHOR("Ronaldo A. Ferreira");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");
