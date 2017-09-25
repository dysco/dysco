/*
 *
 *	Dynamic Service Chaining with Dysco
 *	Dysco kernel agent: dysco_main.c
 *
 *	Module for  initialization and termination, and  definition of
 *	common functions.
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
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/llc.h>
#include <linux/filter.h>
#include <linux/netlink.h>
#include <linux/rhashtable.h>
#include <net/llc.h>
#include <net/stp.h>
#include <net/switchdev.h>
#include <net/ip.h>
#include <net/tcp.h>
#include "dysco.h"
/* */

/* Definition of a few locks */
DEFINE_SPINLOCK(ns_lock);
DEFINE_SPINLOCK(new_ns_lock);
DEFINE_SPINLOCK(port_alloc_lock);
DEFINE_SPINLOCK(dysco_tag_lock);
/* */

// Global variable  for port  allocation. This needs  to change  for a
// more  robust  data  structure   that  accounts  for  conflicts  and
// reutilization. We are currently using ports in the range 10000-2000
// as destination port.  We never use a source port 10000 or smaller
// to avoid conflicts between two  middlebox. With this simple scheme,
// we can test with more than 400M sessions.
unsigned int alloc_ports = 0x2710ffff;
/* */
#define MAX_NAMESPACES		20	// max number of namespaces.
/* */
#define MAX_NETLINK_MSG		64*1024
struct list_head ns_hashes;
struct rhashtable ns_hash_table;
/* */

/* Dysco netlink configuration */
struct netlink_kernel_cfg dysco_nl_cfg = {
	.groups = 1,
	.input = dysco_user_kernel_com
};
/* */

/* Hash table template for the namespaces table lookup/insertion/deletion */
const struct rhashtable_params dysco_rhashtable_params_ns = {
	.nelem_hint  = 100,
	.key_len     = sizeof(struct net *),
	.key_offset  = offsetof(struct dysco_hashes, net_ns),
	.head_offset = offsetof(struct dysco_hashes, node),
	.min_size    = 50, 
	.nulls_base  = (1U << RHT_BASE_SHIFT),
};
/* */

/* Hash table template for the output table lookup/insertion/deletion */
const struct rhashtable_params dysco_rhashtable_params_out = {
	.nelem_hint  = DYSCO_NELEM_HINT,
	.key_len     = sizeof(struct tcp_session),
	.key_offset  = offsetof(struct dysco_cb_out, super),
	.head_offset = offsetof(struct dysco_cb_out, node),
	.min_size    = DYSCO_MIN_SIZE,
	.nulls_base  = (1U << RHT_BASE_SHIFT),
};
/* */

/* Hash table template for the input table lookup/insertion/deletion  */
const struct rhashtable_params dysco_rhashtable_params_in = {
	.nelem_hint  = DYSCO_NELEM_HINT,
	.key_len     = sizeof(struct tcp_session),
	.key_offset  = offsetof(struct dysco_cb_in, sub),
	.head_offset = offsetof(struct dysco_cb_in, node),
	.min_size    = DYSCO_MIN_SIZE,
	.nulls_base  = (1U << RHT_BASE_SHIFT),
};
/* */

/* Hash table template for the pending table lookup/insertion/deletion */
const struct rhashtable_params dysco_rhashtable_params_pen = {
	.nelem_hint  = DYSCO_NELEM_HINT,
	.key_len     = sizeof(unsigned int),
	.key_offset  = offsetof(struct dysco_cb_out, dysco_tag),
	.head_offset = offsetof(struct dysco_cb_out, node),
	.min_size    = DYSCO_MIN_SIZE,
	.nulls_base  = (1U << RHT_BASE_SHIFT),
};
/* */

/* Hash table template for the reconfiguration table lookup/insertion/deletion */
const struct rhashtable_params dysco_rhashtable_params_reconfig = {
	.nelem_hint  = DYSCO_NELEM_HINT,
	.key_len     = sizeof(struct tcp_session),
	.key_offset  = offsetof(struct dysco_cb_reconfig, super),
	.head_offset = offsetof(struct dysco_cb_reconfig, node),
	.min_size    = DYSCO_MIN_SIZE,
	.nulls_base  = (1U << RHT_BASE_SHIFT),
};
/* */

/* Network events handler for Dysco. */
static struct notifier_block dysco_dev_notifier = {
	.notifier_call = dysco_dev_event,
};
/* */

/*********************************************************************
 *
 *	dysco_add_policy:  adds a  policy  to a  network namespace.  A
 *	policy consists  of a  BPF filter  and its  associated service
 *	chain.
 *
 *********************************************************************/
void dysco_add_policy(struct dysco_hashes *dh, unsigned char *mptr)
{
	struct sock_fprog_kern  fprog;
	struct dysco_policies	*dps;
	struct service_chain	*sc;
	short			len;
	int			mem_sz;
	
	// Get the length of the service chain and allocate memory
	len    = *((short *)mptr);
	mem_sz = sizeof(struct service_chain) + len * sizeof(struct next_hop);
	sc     = kmalloc(mem_sz, GFP_KERNEL);
	if (sc == NULL)
		goto no_mem;
	
	// Create one dysco policy record
	dps = kmalloc(sizeof(*dps), GFP_KERNEL);
	if (dps == NULL)
		goto no_mem_sc;	
	
	sc->len = len;
	mptr   += 4;		// skip len and pos of service chain struct	
	memcpy(&sc->hops[0], mptr, len * sizeof(struct next_hop));
	mptr += len * sizeof(struct next_hop); 
	
	// Get the BPF filter
	len   = *((short *)mptr);
	mptr += 2;
	fprog.len    = len;
	fprog.filter = (struct sock_filter *)mptr;

	dps->sc  = sc;
	
	// Create the BPF program
	if (bpf_prog_create(&dps->filter, &fprog) < 0) {
		printk(DYSCO_ALERT "Error creating bpf_prog for Dysco\n");
		goto no_mem_dps;
	}	
	
	// Insert dps record into list
	// Begin of critical section
	spin_lock_bh(&dh->policy_lock);
	list_add_rcu(&dps->list, &dh->policies);
	spin_unlock_bh(&dh->policy_lock);
	// End of critical section
       
	return;
	
 no_mem_dps:
	kfree(dps);	
 no_mem_sc:
	kfree(sc);	
 no_mem:
	printk(DYSCO_ALERT "error in dysco_add_policy\n");
}
/* dysco_add_policy */


/*********************************************************************
 *
 *	dysco_del_policies: deletes all the current policies.
 *
 *********************************************************************/
void dysco_del_policies(struct dysco_hashes *dh)
{
	struct dysco_policies *dps;
	
	while (TRUE) {
		// Begin of critical section
		spin_lock_bh(&dh->policy_lock);
		if (list_empty(&dh->policies)) {
			spin_unlock_bh(&dh->policy_lock);
			// End of critical section 1
			break;
		}
		else {
			dps = list_first_entry(&dh->policies, typeof(*dps), list);
			list_del_rcu(&dps->list);
			spin_unlock_bh(&dh->policy_lock);
			// End of critical section 2
			
			synchronize_rcu();
			bpf_prog_destroy(dps->filter);
			if (dps->sc)	// This if is not necessary.
				kfree(dps->sc);
			printk(DYSCO_ALERT "Releasing one policy\n");
			kfree(dps);
		}
	} 
}
/* dysco_del_policies */


/*********************************************************************
 *
 *	dysco_clear_all: clear  all internal  state used by  the Dysco
 *	kernel module.
 *
 *********************************************************************/
void dysco_clear_all(void)
{
	printk(DYSCO_ALERT "clearing all internal states\n");
}
/* dysco_clear_all */


struct nlmsghdr	*dysco_nlh = NULL;

/*********************************************************************
 *
 *	send_pkt_user_space:  in  case  buffering is  necessary,  send
 *	packets to user space. [TODO]
 *
 *********************************************************************/
void send_pkt_user_space(struct dysco_hashes *dh, struct sk_buff *skb)
{
	int		res;
	struct sk_buff  *sk_temp;
	struct nlmsghdr *nlh;

	if (dysco_nlh == NULL) {
		printk(DYSCO_ALERT
		       "Can't buffer packet: no user space app defined\n");
		return;
	}
	if (skb_headroom(skb) < NLMSG_HDRLEN) {
		printk(DYSCO_ALERT
		       "No headroom in the original skb_buff to send"
		       " nelink message\n");
		sk_temp = nlmsg_new(skb->len, 0);
		nlh = nlmsg_put(sk_temp, dysco_nlh->nlmsg_pid,
				dysco_nlh->nlmsg_seq, NLMSG_DONE,
				skb->len, 0);
		memcpy(nlmsg_data(nlh), skb->data, skb->len);
		kfree_skb(skb);
		skb = sk_temp;
	}
	else {
		skb_push(skb, NLMSG_HDRLEN);
		nlh  = nlmsg_put(skb, dysco_nlh->nlmsg_pid,
				 dysco_nlh->nlmsg_seq, NLMSG_DONE,
				 skb->len, 0);
	}

	printk(DYSCO_ALERT "sk_buff built with size %d (nlmsg_len=%d)\n",
	       skb->len, nlh->nlmsg_len);
	NETLINK_CB(skb).dst_group = 0;
	res = nlmsg_unicast(dh->nl_sk, skb, dysco_nlh->nlmsg_pid);
	if (res < 0)
		printk(DYSCO_ALERT "Error while sending pkt to user\n");
}
/* send_pkt_user_space */


/*********************************************************************
 *
 *	dysco_walk_hash_table: this function iterates over the entries
 *	of both  input and output  tables to collect  mappings between
 *	session and subsession that are sent to user space.
 *
 *********************************************************************/
int dysco_walk_hash_table(struct rhashtable *ht, struct sk_buff *skb,
			  int offset, int size, int buf_size, __u16 *no_room)
{
	struct rhashtable_iter	iter;
	unsigned char		*rec, *data, *rec_begin;
	int			ret;

	printk(DYSCO_ALERT "dysco_walk_hash_table\n");
	ret = rhashtable_walk_init(ht, &iter);
	if (ret) {
		printk(DYSCO_ALERT "Error creating hash table iterator\n");
		return ret;
	}
	
	ret = rhashtable_walk_start(&iter);
	if (ret && ret != -EAGAIN) {
		printk(DYSCO_ALERT "error starting hash walk\n");
		goto rel_ret;
	}

	ret = 0;
	rec = rhashtable_walk_next(&iter);
	for ( ; rec; rec = rhashtable_walk_next(&iter)) {
		if (IS_ERR(rec)) {
			if (PTR_ERR(rec) == -EAGAIN)
				continue;
			break;
		}
		rec_begin = rec + offset;
		data = skb_put(skb, size);
		memcpy(data, rec_begin, size);
		ret++;
		if (size*(ret+1) > buf_size) {
			printk(DYSCO_ALERT "Netlink buffer size is too small for all entries\n");
			*no_room = 1;
			break;
		}
	}
	printk(DYSCO_ALERT "dysco_walk_hash_table returning %d elements\n", ret);
	
 rel_ret:
	rhashtable_walk_stop(&iter);
	rhashtable_walk_exit(&iter);
	return ret;
}
/* dysco_walk_hash_table */


/*********************************************************************
 *
 *	dysco_get_mapping: returns to user space the  current mappings
 *	between session/subsession mantained by the kernel module.
 *
 *********************************************************************/
void dysco_get_mapping(struct dysco_hashes *dh, struct nlmsghdr *nlh)
{
	struct sk_buff	*skb_out;
	struct nlmsghdr	*nlmsg;
	__u16		*len, out, len_l, no_room = 0;
	int		ret, buf_size;;

	printk(DYSCO_ALERT "dysco_get_mapping\n");
	skb_out = nlmsg_new(MAX_NETLINK_MSG, GFP_ATOMIC);
	if (skb_out == NULL) {
		printk(DYSCO_ALERT "Could not allocate sk_buff for netlink\n");
		return;
	}
	nlmsg = nlmsg_put(skb_out, nlh->nlmsg_pid, nlh->nlmsg_seq,
			  NLMSG_DONE, 2 * sizeof(__u16), 0);

	buf_size = MAX_NETLINK_MSG - 2 * sizeof(__u16);
	len = (__u16 *)nlmsg_data(nlmsg);
	ret = dysco_walk_hash_table(&dh->dysco_hash_out, skb_out,
				    offsetof(struct dysco_cb_out, super),
				    sizeof(struct dysco_mapping),
				    buf_size, &no_room);
	if (ret < 0)
		goto free_ret;

	if (no_room) {
		len_l  = ret;
		len[0] = len[1] = htons(ret);
	}
	else {
		out = ret;
		buf_size -= ret * sizeof(struct dysco_mapping);
		ret = dysco_walk_hash_table(&dh->dysco_hash_in, skb_out,
					    offsetof(struct dysco_cb_out, super),
					    sizeof(struct dysco_mapping),
					    buf_size, &no_room);
		if (ret < 0)
			goto free_ret;
		
		len_l  = out + ret;
		len[0] = htons(len_l);
		len[1] = htons(out);
	}
	
	nlmsg->nlmsg_len += len_l * sizeof(struct dysco_mapping);
	ret = nlmsg_unicast(dh->nl_sk, skb_out, nlh->nlmsg_pid);
	if (ret < 0)
		printk(DYSCO_ALERT "Error sending mapping via netlink  to user\n");
	return;
	
 free_ret:
	printk(DYSCO_ALERT "Error retrieving hash table information\n");
	nlmsg_free(skb_out);
}
/* dysco_get_mapping */


#define REC_SIZE sizeof(struct tcp_session) + sizeof(__u64)

/*********************************************************************
 *
 *	dysco_walk_rec_table:   this   function  iterates   over   the
 *	reconfiguration hash table.
 *
 *********************************************************************/
int dysco_walk_rec_table(struct rhashtable *ht, struct sk_buff *skb)
{
	struct rhashtable_iter		iter;
	struct dysco_cb_reconfig	*rec;
	unsigned char			*data;
	int				ret, buf_size;

	printk(DYSCO_ALERT "dysco_walk_rec_table\n");
	ret = rhashtable_walk_init(ht, &iter);
	if (ret) {
		printk(DYSCO_ALERT "Error creating hash table iterator\n");
		return ret;
	}
	
	ret = rhashtable_walk_start(&iter);
	if (ret && ret != -EAGAIN) {
		printk(DYSCO_ALERT "error starting hash walk\n");
		goto rel_ret;
	}

	ret = 0;
	rec = rhashtable_walk_next(&iter);
	buf_size = MAX_NETLINK_MSG - 2 * sizeof(__u16);
	for ( ; rec && buf_size > REC_SIZE; rec = rhashtable_walk_next(&iter)) {
		__u64 sec, nsec, rec_time;
		
		if (IS_ERR(rec)) {
			if (PTR_ERR(rec) == -EAGAIN)
				continue;
			break;
		}
		data = skb_put(skb, sizeof(struct tcp_session)+sizeof(__u64));
		
		sec  = rec->rec_end.tv_sec - rec->rec_begin.tv_sec;
		nsec = rec->rec_end.tv_nsec - rec->rec_begin.tv_nsec;
		rec_time = sec * 1000000000 + nsec;
		rec_time = cpu_to_be64(rec_time);
		
		memcpy(data, &rec->super, sizeof(struct tcp_session));
		data += sizeof(struct tcp_session);
		memcpy(data, &rec_time, sizeof(__u64));
		ret++;
		buf_size -= REC_SIZE;
	}
	printk(DYSCO_ALERT "dysco_walk_hash_table returning %d elements\n", ret);
 rel_ret:
	rhashtable_walk_stop(&iter);
	rhashtable_walk_exit(&iter);
	return ret;
}
/* dysco_walk_rec_table */


/*********************************************************************
 *
 *	dysco_get_rec_time: return  to user space  the reconfiguration
 *	times.
 *
 *********************************************************************/
void dysco_get_rec_time(struct dysco_hashes *dh, struct nlmsghdr *nlh)
{
	struct sk_buff	*skb_out;
	struct nlmsghdr	*nlmsg;
	__u16		*len;
	int		ret;

	printk(DYSCO_ALERT "dysco_get_rec_time\n");
	skb_out = nlmsg_new(MAX_NETLINK_MSG, GFP_ATOMIC);
	if (skb_out == NULL) {
		printk(DYSCO_ALERT "Could not allocate sk_buff for netlink\n");
		return;
	}
	nlmsg = nlmsg_put(skb_out, nlh->nlmsg_pid, nlh->nlmsg_seq,
			  NLMSG_DONE, 2*sizeof(__u16), 0);
	
	len = (__u16 *)nlmsg_data(nlmsg);
	ret = dysco_walk_rec_table(&dh->dysco_hash_reconfig, skb_out);
	if (ret < 0)
		goto free_ret;
	
	len[0] = htons(ret);
	len[1] = 0;		// Not used.
	
	nlmsg->nlmsg_len += ret * (sizeof(struct tcp_session) + sizeof(__u64));
	ret = nlmsg_unicast(dh->nl_sk, skb_out, nlh->nlmsg_pid);
	if (ret < 0)
		printk(DYSCO_ALERT "Error sending mapping via netlink  to user\n");
	return;
	
 free_ret:
	printk(DYSCO_ALERT "Error retrieving hash table information\n");
	nlmsg_free(skb_out);
}
/* dysco_get_rec_time */


/*********************************************************************
 *
 *	dysco_user_kernel_com:  this function  does the  communication
 *	between kernel and user space.
 *
 *********************************************************************/
void dysco_user_kernel_com(struct sk_buff *skb)
{
	struct nlmsghdr		*nlh;
	unsigned char		*payload;
	struct dysco_hashes	*dh;
	struct net		*net;

	/*
	printk(DYSCO_ALERT "dysco_user_kernel_com running\n");
	return;
	*/
	
	payload = (unsigned char *)skb->data;
	net = sock_net(skb->sk);
	
	dh = dysco_get_hashes(net);
	if (dh == NULL) {
		printk(DYSCO_ALERT "BUG: no namespace found\n");
		return;
	}
	payload += NETLINK_HEADER_LENGTH;
	nlh = (struct nlmsghdr *)skb->data;
	printk(DYSCO_ALERT "Netlink received msg: {%d,%d,%d,%d,%d}\n",
	       nlh->nlmsg_len, nlh->nlmsg_type, nlh->nlmsg_flags,
	       nlh->nlmsg_seq, nlh->nlmsg_pid);
	
	switch(nlh->nlmsg_type) {
	case DYSCO_POLICY:
		printk(DYSCO_ALERT "Received a DYSCO_POLICY message\n");
		dysco_add_policy(dh, payload);
		break;
		
	case DYSCO_REM_POLICY:
		dysco_del_policies(dh);
		break;
		
	case DYSCO_CLEAR_ALL:
		dysco_clear_all();
		break;
		
	case DYSCO_BUFFER_PACKET:
		break;

	case DYSCO_TCP_SPLICE: 
		break;

	case DYSCO_GET_MAPPING:
		dysco_get_mapping(dh, nlh);
		break;

	case DYSCO_GET_REC_TIME:
		dysco_get_rec_time(dh, nlh);
		break;
		
	default:
		printk(DYSCO_ALERT "Invalid Dysco message type: %d\n",
		       nlh->nlmsg_type);
	}
}
/* dysco_user_kernel_com */


/*********************************************************************
 *
 *	dysco_create_hashes:  creates a  hash table  for handling  the
 *	output and  input paths, reconfiguration, and  pending entries
 *	in a middlebox.
 *
 *********************************************************************/
struct dysco_hashes *dysco_create_hashes(struct net *net_ns, gfp_t gfp)
{
	struct dysco_hashes	*dh_aux;
	
	dh_aux = kmalloc(sizeof(struct dysco_hashes), gfp);
	if (dh_aux == NULL) 
		return NULL;

	if (rhashtable_init(&dh_aux->dysco_hash_out,
			    &dysco_rhashtable_params_out) < 0) {
		printk(DYSCO_ALERT "Error initializing hash out table\n");
		goto free_nh_aux;
	}
	
	if (rhashtable_init(&dh_aux->dysco_hash_in,
			    &dysco_rhashtable_params_in) < 0) {
		printk(DYSCO_ALERT "Error initializing hash in table\n");
		goto free_hash_out;
	}
	
	if (rhashtable_init(&dh_aux->dysco_hash_pen,
			    &dysco_rhashtable_params_out) < 0) {
		printk(DYSCO_ALERT "Error initializing hash pending table\n");
		goto free_hash_in;
	}
	
	if (rhashtable_init(&dh_aux->dysco_hash_reconfig,
			    &dysco_rhashtable_params_reconfig) < 0) {
		printk(DYSCO_ALERT "Error initializing hash reconfig table\n");
		goto free_hash_pen;
	}
	
	if (rhashtable_init(&dh_aux->dysco_hash_pen_tag,
			    &dysco_rhashtable_params_pen) < 0) {
		printk(DYSCO_ALERT "Error initializing hash pen tag table\n");
		goto free_hash_tag;
	}
	
	// Initialize policies
	INIT_LIST_HEAD(&dh_aux->policies);
	spin_lock_init(&dh_aux->policy_lock);

	// Initialize Dysco tag lock
	spin_lock_init(&dh_aux->tag_lock);

	// Set namespace
	dh_aux->net_ns = net_ns;
	if (net_ns == NULL)
		dh_aux->nl_sk = NULL;
	else {	
		// Create netlink socket
		dh_aux->nl_sk = netlink_kernel_create(net_ns, DYSCO_NETLINK_USER,
						      &dysco_nl_cfg);
		if (dh_aux->nl_sk == NULL)
			printk(DYSCO_ALERT "Error creating netlink socket\n");
	}
	
	return dh_aux;

	/* Free hash tables if an error occurs */
 free_hash_tag:
	rhashtable_destroy(&dh_aux->dysco_hash_reconfig);
	
 free_hash_pen:
	rhashtable_destroy(&dh_aux->dysco_hash_pen);
	
 free_hash_in:
	rhashtable_destroy(&dh_aux->dysco_hash_in);
	
 free_hash_out:
	rhashtable_destroy(&dh_aux->dysco_hash_out);
	
 free_nh_aux:
	kfree(dh_aux);	
	return NULL;
}
/* dysco_create_hashes */


/*********************************************************************
 *
 *	dysco_dev_event:   this  function   handles  all   the  events
 *	generated by  the Linux network  subsystem.  The main  goal of
 *	this function  is to determine  if a new network  namspace was
 *	created.  This  function  creates  the  hash  tables  for  new
 *	namespaces when they are created.
 *
 *	 Should handle the following events:
 *	 NETDEV_REGISTER
 *	 NETDEV_UNREGISTER
 *
 *********************************************************************/
int dysco_dev_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device	*dev;
	struct net		*net_ns;
	struct dysco_hashes	*dh_aux = NULL;

	if (NETDEV_POST_INIT == event) {
		dev    = netdev_notifier_info_to_dev(ptr);
		net_ns = dev_net(dev);
		
		spin_lock_bh(&new_ns_lock);		
		dh_aux = rhashtable_lookup_fast(&ns_hash_table, &net_ns,
						dysco_rhashtable_params_ns);
		if (dh_aux != NULL) {
			spin_unlock_bh(&new_ns_lock);
			return NOTIFY_DONE;
		}
		
		dh_aux = list_first_or_null_rcu(&ns_hashes, struct dysco_hashes,
						list);
		if (unlikely(dh_aux == NULL)) {
			printk(DYSCO_ALERT "list of namespace hashes is empty\n");
			spin_unlock_bh(&new_ns_lock);
			return NOTIFY_DONE;
		}
		
		list_del_rcu(&dh_aux->list);
		dh_aux->net_ns = net_ns;
		rhashtable_insert_fast(&ns_hash_table, &dh_aux->node,
				       dysco_rhashtable_params_ns);
		
		spin_unlock_bh(&new_ns_lock);
		
		printk(DYSCO_ALERT
		       "created a new namespace (event=%lu, net_ns=%p)\n",
		       event, net_ns);
		tot_namespaces++;
		dh_aux->nl_sk = netlink_kernel_create(net_ns, DYSCO_NETLINK_USER,
						      &dysco_nl_cfg);
		if (dh_aux->nl_sk == NULL)
			printk(DYSCO_ALERT "Error creating netlink socket\n");
	}
	
	return NOTIFY_DONE;
}
/* dysco_dev_event */


/*********************************************************************
 *
 *	dysco_init_hashes: initializes  the hash  tables and  create a
 *	cache for new namespaces.
 *
 *********************************************************************/
void dysco_init_hashes(void)
{
	int i;
	struct dysco_hashes	*dh_aux;
	struct net		*net_ns = &init_net;

	if (rhashtable_init(&ns_hash_table, &dysco_rhashtable_params_ns) < 0)
		return;
	
	dh_aux = dysco_create_hashes(net_ns, GFP_KERNEL);
	if (dh_aux) {
		rhashtable_insert_fast(&ns_hash_table, &dh_aux->node,
				       dysco_rhashtable_params_ns);
		printk(DYSCO_ALERT "created hashes for init_net=%p\n", net_ns);
	}
	
	// The dysco_create_hashes  function can block, so  I create a
	// cache for  the hash tables  because the event  handler that
	// handles new namespaces cannot block.
	for (i = 0; i < MAX_NAMESPACES; i++) {
		dh_aux = dysco_create_hashes(NULL, GFP_KERNEL);
		if (likely(dh_aux))
			list_add_rcu(&dh_aux->list, &ns_hashes);
		else {
			printk(DYSCO_ALERT
			       "could not create namespace hashes (%d created)\n", i);
			break;
		}
	}
}
/* dysco_init_hashes */


/*********************************************************************
 *
 *	dysco_free_hash: frees  a hash  table entry. This  function is
 *	called when the rhashtable destroy function is running.
 *
 *********************************************************************/
void dysco_free_hash(void *ptr, void *arg)
{
	if (ptr == NULL)
		printk(DYSCO_ALERT "WRONG: trying to free a null pointer\n");
	else
		kfree(ptr);
}
/* dysco_free_hash */


/*********************************************************************
 *
 *	dysco_free_dcb_out: free one output control block.
 *
 *********************************************************************/
void dysco_free_dcb_out(void *ptr, void *arg)
{
	struct dysco_cb_out *dcb_out = (struct dysco_cb_out *)ptr;
	
	if (dcb_out == NULL)
		printk(DYSCO_ALERT "WRONG: trying to free a null pointer to dcb_out\n");
	else {
		// This   if    statement   is   necessary    when   a
		// reconfiguration  is  performed  to  free  the  data
		// structre allocated to the other path.
		if (dcb_out->other_path)
			kfree(dcb_out->other_path);

		// This if statemenet is necessary on a host that holds a policy.
		if (dcb_out->free_sc)
			kfree(dcb_out->sc);
		kfree(dcb_out);
	}
}
/* dysco_free_dcb_out */


/*********************************************************************
 *
 *	dysco_destroy_hashes: frees all hash tables.
 *
 *********************************************************************/
void dysco_destroy_hashes(void *ptr, void *arg)
{
	struct dysco_hashes *dh = (struct dysco_hashes *)ptr;

	rhashtable_free_and_destroy(&dh->dysco_hash_out, dysco_free_dcb_out, NULL);
	rhashtable_free_and_destroy(&dh->dysco_hash_in, dysco_free_hash, NULL);
	rhashtable_free_and_destroy(&dh->dysco_hash_pen, dysco_free_hash, NULL);
	rhashtable_free_and_destroy(&dh->dysco_hash_reconfig, dysco_free_hash, NULL);
	rhashtable_free_and_destroy(&dh->dysco_hash_pen_tag, dysco_free_hash, NULL);
	if (dh->nl_sk)
		netlink_kernel_release(dh->nl_sk);
			
	synchronize_rcu();
			
	dysco_del_policies(dh);	
	kfree(dh);
}
/* dysco_destroy_hashes */



/*********************************************************************
 *
 *	dysco_destroy_namespaces: frees all  active namespaces and the
 *	ones in the cache.
 *
 *********************************************************************/
void dysco_destroy_namespaces(void)
{
	struct dysco_hashes *dh;
	
	rhashtable_free_and_destroy(&ns_hash_table, dysco_destroy_hashes, NULL);

	// Destroy the entries still in the cache
	while (!list_empty(&ns_hashes)) {
		dh = list_first_entry(&ns_hashes, typeof(*dh), list);
		list_del_rcu(&dh->list);
		dysco_destroy_hashes(dh, NULL);
	}
}


/*********************************************************************
 *
 *	dysco_deinit: cleans the Dysco  module, i.e., frees memory and
 *	unregister  the handlers.  Called  when the  kernel module  is
 *	removed.
 *
 *********************************************************************/
int dysco_deinit(void)
{
	// Unregister device notifier
	unregister_netdevice_notifier(&dysco_dev_notifier);
	if (dysco_nlh)
		kfree(dysco_nlh);

	// Destroy hash tables
	printk(DYSCO_ALERT "Destroying hash tables\n");
	//dysco_destroy_hashes(); call this function for the list implementation

	dysco_destroy_namespaces();

	// Remove the /proc entries
	//dysco_proc_cleanup();

	// Wait for pending rcu callbacks to end.
	rcu_barrier();

	printk(DYSCO_ALERT "NAMESPACES: %u\n", tot_namespaces);
	
	printk(DYSCO_ALERT "TCP SACK REWRITES: %lu\n", tcp_sack_rewrites);
	printk(DYSCO_ALERT "TCP TS   REWRITES: %lu\n", tcp_ts_rewrites);
	printk(DYSCO_ALERT "TCP TSR  REWRITES: %lu\n", tcp_tsr_rewrites);	
	printk(DYSCO_ALERT "Dysco removed successfully\n");
	return 0;
}
/* dysco_deinit */


/*********************************************************************
 *
 *	dysco_init: initializes  the Dysco kernel module.  Creates the
 *	hash tables and register the event handler.
 *
 *********************************************************************/
int dysco_init(void)
{
	// Initialize global list of name spaces.
	INIT_LIST_HEAD(&ns_hashes);	// This is for the list implementation only.

	spin_lock_init(&new_ns_lock);
	
	// Initialize input and output hash tables
	dysco_init_hashes();
	
	// Create the /proc entries
	//dysco_proc_init();

	printk(DYSCO_ALERT "Dysco initialized\n");
	printk(DYSCO_ALERT "Space overhead\n");
	printk(DYSCO_ALERT "     dysco_cb_out=%lu bytes\n", sizeof(struct dysco_cb_out));
	printk(DYSCO_ALERT "     dysco_cb_in=%lu bytes\n", sizeof(struct dysco_cb_in));
	printk(DYSCO_ALERT "     control_message=%lu\n", sizeof(struct control_message));
	printk(DYSCO_ALERT "     dysco_cb_reconfig=%lu\n", sizeof(struct dysco_cb_reconfig));
	printk(DYSCO_ALERT "HZ=%d\n", HZ);

	// Register device notifier
	register_netdevice_notifier(&dysco_dev_notifier);	
	return 0;
}
/* dysco_init */

