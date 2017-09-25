/*
 *
 *	Dynamic Service Chaining with Dysco
 *	Dysco kernel agent: dysco.h
 *
 *	Definitions of common constants, variables, and functions.
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

#include <linux/netdevice.h>
#include <linux/rhashtable.h>
#include <linux/time.h>
#include <linux/filter.h>
#include <net/tcp.h>
/* */
#define TRUE	1
#define FALSE	0
/* */  
#define DYSCO_MEASURE_TIME
#define DYSCO_MEASUREMENT_SAMPLES		1100
/*
  This constant was defined based on the values in <include/uapi/linux/sockios.h>.
  Search for constant SIOCBRADDIF in the file sockios.h.
*/
#define DYSCO_ATTACH_FILTER	0x9000
/* */
#define printk_dysco(x)		;
/* 
#ifdef _DYSCO_DEBUG_
#define printk_dysco(x) printk(x)
#elseif
#define printk_dysco(x) 
#endif
*/
/* */
#define ETHER_HDR_LEN	14
#define ETHER_ADDR_LEN	6
/* */
// Hash parameters
#define DYSCO_NELEM_HINT	200000
#define DYSCO_MIN_SIZE		100000
/* */
// Dysco control messages
enum {
	// Locking protocol
	DYSCO_REQUEST_LOCK = 1,
	DYSCO_ACK_LOCK,
	DYSCO_NACK_LOCK,
	
	// Reconfiguration
	DYSCO_SYN,
	DYSCO_SYN_ACK,
	DYSCO_ACK,
	DYSCO_FIN,
	DYSCO_FIN_ACK,
	
	// Management
	DYSCO_POLICY,
	DYSCO_REM_POLICY,
	DYSCO_CLEAR,
	DYSCO_CLEAR_ALL,
	DYSCO_BUFFER_PACKET,
	DYSCO_TCP_SPLICE,
	DYSCO_COPY_STATE,
	DYSCO_PUT_STATE,
	DYSCO_STATE_TRANSFERRED,
	DYSCO_ACK_ACK,
	DYSCO_GET_MAPPING,
	DYSCO_GET_REC_TIME
};
/* */
#define NOSTATE_TRANSFER	0
#define STATE_TRANSFER		1
/* */
// States of a connection
enum {
	DYSCO_ONE_PATH = 0,
	DYSCO_ADDING_NEW_PATH,
	DYSCO_ACCEPTING_NEW_PATH,
	DYSCO_INITIALIZING_NEW_PATH,
	DYSCO_MANAGING_TWO_PATHS,
	DYSCO_FINISHING_OLD_PATH,
	DYSCO_UNLOCKED,
	DYSCO_LOCK_PENDING,
	DYSCO_LOCKED
};
/* */
#define DYSCO_SYN_SENT			DYSCO_ADDING_NEW_PATH
#define DYSCO_SYN_RECEIVED		DYSCO_ACCEPTING_NEW_PATH
#define DYSCO_ESTABLISHED		DYSCO_INITIALIZING_NEW_PATH
/* */
#define NETLINK_HEADER_LENGTH		16
/* */
#define DYSCO_NETLINK_USER		27
#define DYSCO_SERVER_PORT		2016
#define DYSCO_MANAGEMENT_PORT		2017
/* */
#define DYSCO_TCP_OPTION	253	// TCP option reserved for experimentation
#define DYSCO_TCP_OPTION_LEN	8	// Eight bytes including padding for alignment
/* */
typedef struct next_hop {
	__be32	ip;
	//__u8	mac[6];
} __attribute__((packed)) nh_t;
/* */

struct service_chain {
	__u16	len;
	__u16	pos;
	nh_t	hops[0];
};
/* */

struct dysco_policy {
	struct sock_fprog	filter;
	struct service_chain	*sc;
};
/* */

struct tcp_session {
	__be32	sip;
	__be32	dip;
	__be16	sport;
	__be16	dport;
};
/* */

struct syn_packet {
	struct tcp_session	ss;
	struct service_chain	sc;
};
/* */

struct dysco_tcp_option {
	__u8	kind;		// 253 reserved for experimentation
	__u8	len;		// 8 bytes
	__u16	padding;	// Used for padding and alignment
	__u32	tag;		// Dysco tag
};
/* */

struct dysco_policies {
	struct list_head	list;			// Connect to the list of policies.
	struct bpf_prog		*filter;		// Filter associated with the policy.
	struct service_chain	*sc;			// Service chain associated with the policy.
};
/* */

struct dysco_hashes {
	struct rhash_head	node;			// Node in the RCU hash table.
	struct list_head	list;			// Connect to the list of name spaces. This
							// must be removed as we move to a hash table.
	
	struct net		*net_ns;		// Name space ID
	
	struct sock		*nl_sk;			// Netlink socket to communicate with user space
	struct nlmsghdr		*dysco_nlh;		// Currently not used. Need to remove it.
	
	struct list_head	policies;		// List of policies
	spinlock_t		policy_lock;		// Lock for the policy list

	u32			dysco_tag;		// Per namespace Dysco tag
	spinlock_t		tag_lock;		// Lock for the Dysco tag
	
	struct rhashtable	dysco_hash_out;		// Hash table used in the packet output
	struct rhashtable	dysco_hash_in;		// Hash table used in the packet input
	struct rhashtable	dysco_hash_pen;		// For pending output at MBs
	struct rhashtable	dysco_hash_pen_tag;	// For pending output at MBs using Dysco tag
	struct rhashtable	dysco_hash_reconfig;	// For reconfiguration
};
/* */

/*
  About spinlocks

  With softirqs, regardless of wether it  is the same softirq type, if
  data is shared by softirqs, it must be protected with a lock. Recall
  that softirqs, even  two of the same type,  might run simultaneously
  on  multiple processors  in  the system.  A  softirq never  preempts
  another softirq running on the same processor, however, so disabling
  bottom halves is not needed.

*/
/* */
struct dysco_cb_in;
/* */


// This struct is 160-byte long with padding and bit fields.
struct dysco_cb_out { 
	struct rhash_head	node;		// Node in the RCU hash table.					(8 bytes)
	struct dysco_cb_in	*dcb_in;	// Pointer to the input control block of the same interface.	(8 bytes)
	
	struct tcp_session	super;		// TCP super session.						(12 bytes)
	struct tcp_session	sub;		// TCP sub session.						(12 bytes)
	
// Sequence number stuff. The mapping is different from
// "TCP Splicing to Application Layer Proxy Performance," because 
// Dysco does the translation only in the output of the anchor
// sending the packet.	
// seq_out = (seq_in - in_iss) + out_iss after a tcp splice
// ack_out = (ack_in - in_irs) + out_irs after a tcp splice
// If you don't want to test, we can always do the assignments above
// by setting the out_* values to the in_* values at session setup
// The initial ack sequences coming from app or going to net
// are set in the input path.
// All sequence numbers are stored locally in host byte order.

	__u32			in_iseq;	// Initial sending sequence coming from app.
	__u32			in_iack;	// Initial ack sequence coming from app.
	__u32			out_iseq;	// Initial sending sequence going to net.
	__u32			out_iack;	// Initial ack sequence going to net.
	__u32			ack_delta;	// Pre-computed difference between the ack numbers
	__u32			seq_delta;	// Pre-computed difference between the sequence numbers
						// The delta variables (ack, seq, ts) are used to avoid recomputing
						// the difference at every packet transmission.
	
	__u32			seq_cutoff;	// Cutoff seq number for the new path sequence number.
	__u32			ack_cutoff;	// Cutoff ack number for the new path acks.
	
	spinlock_t		seq_lock;	// Lock for updating sequence number				(8 bytes)

	struct tcp_sock		*my_tp;		// sock of the my TCP session being spliced (sending ack).
	struct tcp_sock		*other_tp;	// sock of the other TCP session being spliced (sending data).
	
	struct service_chain	*sc;		// Service chain of the TCP session.				(8 bytes)
	struct dysco_cb_out	*other_path;	// Used to connect old and new path during reconfiguration.	(8 bytes)

	
	// Variables to handle the TCP timestamp option
	__u32			ts_in;		// Initial timestamp coming from the app.
	__u32			ts_out;		// Initial timestamp going to the net.
	__u32			ts_delta;	// Pre-computed difference between the two timestamps

	__u32			tsr_in;		// Initial timestamp response coming from the app.
	__u32			tsr_out;	// Initial timestamp response going to the net.
	__u32			tsr_delta;	// Pre-computed difference between the timestamp responses.

	__u32			dysco_tag;	// Used to search the pending table				(4 bytes)
	
	// Variables to handle the TCP window scaling option
	__u16			ws_in;		// Window scaling coming from app.
	__u16			ws_out;		// Window scaling going to net.
	__u16			ws_delta;	// Pre-computed difference between the two window scaling factors.

	
	__u8			nh_mac[6];	// Next hop mac address						(8 bytes until the end)
	__u8			state;		// Connection state
	
	__u8			old_path:1,	// Flag that indicates if it is the old path.
				valid_ack_cut:1,// Indicates if we know the ack cutoff.
				use_np_seq:1,	// Use new path only. This is to implement PAWS.
				use_np_ack:1,	// Use new path only.
				state_t:1,	// Reconfiguration with state transfer.
				free_sc:1;	// The service chain was allocated for this cb in the input
						// for dysco_handle_mb_out.
	
	__u8			ack_add:1,	// Must add the ack_delta.
				seq_add:1,	// Must add the seq_delta.
				sack_ok:1,	// Indicates if TCP sack is negotiated in the syn packet.
				ts_ok:1,	// Indicates if timestamp is negotiated in the syn packet
				ts_add:1,	// Must add the ts_delta. 
				ws_ok:1,	// Indicates if window scaling is negotiated in the syn packet
				tsr_add:1,	// Must add the tsr_delta.
				tag_ok:1;	// Indicates if the Dysco tag is present in the syn packet. 

	__u8			padding;	// Padding for 16-bit alignment.
		
	__u32			ack_ctr;	// This is just a test to make sure that at least
						// three acks are sent in the old path.
};
/* */

struct dysco_tcp_opt_r {
	u32	tsval;		// Initial timestamp value
	u16	tstamp_ok : 1,  // Saw timestamp on SYN packet
		wscale_ok : 1,  // Wscale seen on SYN packet
		sack_ok : 1,	// SACK seen on SYN packet
		snd_wscale : 4, // Window scaling received from sender
		rcv_wscale : 4; // Window scaling to send to receiver
};
/* */

struct dysco_tcp_opt_s {
	u32	tsval;		// Initial timestamp value
	u16	tstamp_ok : 1,  // Saw timestamp on SYN packet
		wscale_ok : 1,  // Wscale seen on SYN packet
		sack_ok : 1,	// SACK seen on SYN packet
		tag_ok : 1,
		snd_wscale : 4, // Window scaling received from sender
		rcv_wscale : 4; // Window scaling to send to receiver
	
	u32	dysco_tag;	// Dysco tag
};
/* */

// This struct is 104-byte long with padding.
struct dysco_cb_in { 
	struct rhash_head	node;		// Node in the RCU hash table.					(8 bytes)
	struct dysco_cb_out	*dcb_out;	// Pointer to the output control block of the same interface.	(8 bytes)
	
	struct tcp_session	sub;		// TCP sub session.						(12 bytes)
	struct tcp_session	super;		// TCP super session.						(12 bytes)

	// sequence number stuff
	__u32			in_iseq;	// Initial seq coming from the network.
	__u32			in_iack;	// Initial ack coming from the network.
	__u32			out_iseq;	// Initial seq going to the app.
	__u32			out_iack;	// Initial ack going to the app.
	__u32			ack_delta;	// Pre-computed difference between the ack numbers.
	__u32			seq_delta;	// Pre-computed difference between the sequence numbers.
	
	// Variables to handle the TCP timestamp option
	__u32			ts_in;		// Initial timestamp coming from the network.
	__u32			ts_out;		// Initial timestamp going to the app.
	__u32			ts_delta;	// Pre-computed difference between the timestamps.

	__u32			tsr_in;		// Initial timestamp response coming from the network.
	__u32			tsr_out;	// Initial timestamp response going to the app.
	__u32			tsr_delta;	// Pre-computed difference between the timestamp responses.

	// Variables to handle the TCP window scaling option.
	__u16			ws_in;		// Initial window scaling coming from the network.
	__u16			ws_out;		// Initial window scaling going to the app.
	__u16			ws_delta;	// Pre-computed difference between the window scaling options.

	__u8			two_paths:1,	// There are two paths. Field is used to disable old path.
				ack_add:1,	// Must add the ack_delta.
				seq_add:1,	// Must add the seq_delta.
				sack_ok:1,	// Indicates if TCP sack is negotiated in the syn packet.
				ts_ok:1,	// Indicates if timestamp is negotiated in the syn packet.
				ts_add:1,	// Must add the ts_delta.
				tsr_add:1,	// Must add the tsr_delta.
				ws_ok:1;	// Indicates if window scaling is negotiated in the syn packet.
	
	__u8			padding;	// For alignment purposes.

	int			skb_iif;	// ifindex of the input device. Used to fix rcv window. 
};
/* */


// The  size of  the struct  of  a TCP  session in  each interface  is
// output=152  + input=88  =  240  bytes. However,  this  size can  be
// decreased  significantly once  we  change the  implementation to  a
// single Dysco socket. The struct below (dysco_sk) should consolidate
// the fields in the two in and out structs to avoid duplicates.
/* */
struct dysco_sk {
	struct rhash_head	node;
	struct tcp_session	super;

	struct dysco_cb_out	*cur_path;
	struct dysco_cb_out	*new_path;

	spinlock_t		seq_lock;
};
/* */


// The struct below (dysco_cb_reconfig) is 152-byte long.
struct dysco_cb_reconfig { 
	struct tcp_session	super;
	struct tcp_session	leftSS;
	struct tcp_session	rightSS;
	struct tcp_session	sub_out;
	struct tcp_session	sub_in;
	struct rhash_head	node;

	struct dysco_cb_out	*old_dcb;	// Used at the anchors to avoid a new lookup
	struct dysco_cb_out	*new_dcb;	// Used at the anchors to avoid a new lookup
	
	struct timespec		rec_begin;	// Begining of the reconfiguation.
	struct timespec		rec_end;	// End of the reconfiguation.
	
	__u32			leftIseq;	// Initial sequence number at the left anchor.
	__u32			leftIack;	// Initial ACK number at the left anchor.
	
	__u32			leftIts;	// Initial TCP timestamp at the left anchor.
	__u32			leftItsr;	// Initial TCP timestamp response at the left anchor.
	
	__u16			leftIws;	// Initial TCP window scaling at the left anchor.
	__u16			leftIwsr;	// Initial  TCP window scaling    in   the
						// reverse   direction at the left anchor.
	
	__u8			nh_mac[6];

	__u8			sack_ok;	// Indicates if TCP sack was negotiated at the session setup.
	
	__u8			rec_done:1;	// Flag to indicate the reconfiguration time
						// has already been recorded.
};
/* */


// The  struct below  (control_message)  is 94-byte  long without  the
// bytes of the service chain. The service chain is variable.
struct control_message {
	__u16			mtype;
	struct tcp_session	super;
	struct tcp_session	leftSS;
	struct tcp_session	rightSS;
	__be32			leftA;
	__be32			rightA;
	
        __be16			sport;
	__be16			dport;
	
	__be32			leftIseq;
	__be32			leftIack;
	
	__be32			rightIseq;
	__be32			rightIack;
	
	__be32			seqCutoff;
	
	__be32			leftIts;	// Initial TCP timestamp at the left anchor.
	__be32			leftItsr;	// Initial TCP timestamp response at the left anchor.
	
	__be16			leftIws;	// Initial TCP window scaling at the left anchor.
	__be16			leftIwsr;	// Initial  TCP window scaling    of   the
						// reverse   direction at the left anchor.
	__be16			sackOk;		// Using 16 bits for alignment purpose.
	
	__be16			semantic;	// 16 bits for alignment. It could be smaller.
	
	__be32			srcMB;		// Middlebox that is the source of the state.
	__be32			dstMB;		// Middlebox that is the destination of the state.

	
	struct service_chain	sc[0];
} __attribute__((packed));
/* */


struct dysco_mapping {
	struct tcp_session	super;
	struct tcp_session	sub;
};
/* */


struct dysco_mapping_vector {
	__u16			len;
	__u16			out;
	struct dysco_mapping	vector[0];
};


struct tcp_ts {
	__u32	ts;
	__u32	tsr;
};
/* */

#define UDP_HDR_LEN	8	// Length of the UDP header
#define RUDP_HDR_LEN	9	// Length of the reliable UDP header
/* */
#define SC_MEM_SZ(x) (sizeof(struct service_chain) + (x)->len * sizeof(struct next_hop))
/* */
#define DYSCO_ALERT KERN_ALERT "Dysco: "
/* */
extern __be16		dysco_control_port;
extern unsigned int	alloc_ports;
/* */
extern spinlock_t port_alloc_lock;
/* */
/* */

// Hash tables and their parameters
extern struct rhashtable dysco_hash_out;
extern struct rhashtable dysco_hash_in;
extern struct rhashtable dysco_hash_pen;
extern struct rhashtable dysco_hash_reconfig;
extern const struct rhashtable_params dysco_rhashtable_params_ns;
extern const struct rhashtable_params dysco_rhashtable_params_out;
extern const struct rhashtable_params dysco_rhashtable_params_in;
extern const struct rhashtable_params dysco_rhashtable_params_pen;
extern const struct rhashtable_params dysco_rhashtable_params_reconfig;
/* */

// Variables for recording time measurements
extern unsigned rewrite_output_samples[DYSCO_MEASUREMENT_SAMPLES];
extern unsigned translate_output_samples[DYSCO_MEASUREMENT_SAMPLES];
extern unsigned rewrite_input_samples[DYSCO_MEASUREMENT_SAMPLES];
extern unsigned translate_input_samples[DYSCO_MEASUREMENT_SAMPLES];
/* */
extern unsigned long tcp_sack_rewrites;
extern unsigned long tcp_ts_rewrites;
extern unsigned long tcp_tsr_rewrites;
/* */
extern unsigned tot_namespaces;
/* */
extern struct net_device *dev_measurement;
/* */
rx_handler_result_t dysco_input(struct sk_buff *skb);
rx_handler_result_t dysco_control_input(struct dysco_hashes *dh,
					struct sk_buff *skb);
/* */
netdev_tx_t dysco_output(struct sk_buff *skb, struct net_device *dev);
netdev_tx_t dysco_control_output(struct dysco_hashes *dh, struct sk_buff *skb);
/* */

int dysco_init(void);
int dysco_deinit(void);
int dysco_dev_event(struct notifier_block *this, unsigned long event, void *ptr);
int dysco_proc_init(void);
/* */

void dysco_proc_cleanup(void);
void dysco_add_policy(struct dysco_hashes *dk, unsigned char *);
void dysco_dev_init_module(void);
void dysco_dev_cleanup(void);
void dysco_print_add_sc(struct sk_buff *skb, char *stage);
void dysco_print_ss_sub_new(struct dysco_cb_out *cb);
void dysco_user_kernel_com(struct sk_buff *skb);
void dysco_insert_cb_out(struct dysco_hashes *dh,
			 struct sk_buff *skb,
			 struct dysco_cb_out *dcb,
			 __u8 disable_op);
void dysco_replace_cb_leftA(struct sk_buff *skb,
			    struct dysco_cb_reconfig *rcb,
			    struct control_message *cmsg);
void dysco_tcp_sack(struct sk_buff *skb, struct tcphdr *th,
		    __u32 delta, __u8 add);
void dysco_insert_tag(struct dysco_hashes *dh,
		      struct sk_buff *skb,
		      struct iphdr *iph,
		      struct tcphdr *th,
		      struct dysco_cb_in *dcb_in);
void dysco_arp(struct sk_buff *skb, struct dysco_cb_out *dcb);

//void dysco_parse_tcp_options(struct tcphdr *th, struct tcp_options *opt);
/* */
rx_handler_result_t dysco_handle_frame(struct sk_buff **pskb);
/* */
struct dysco_cb_in *dysco_insert_cb_out_reverse(struct dysco_hashes *dh,
						struct dysco_cb_out *dcb_out,
						__u8 two_paths);
/* */

void dysco_parse_tcp_syn_opt_r(struct tcphdr *th, struct dysco_cb_in  *dcb_in);
void dysco_parse_tcp_syn_opt_s(struct tcphdr *th, struct dysco_cb_out *dcb_out);
/* */
//struct dysco_hashes *dysco_get_hashes(struct net *net_ns);
/* */


/*********************************************************************
 *
 *	allocate_neighbor_port:   allocates  a   port  number   for  a
 *	neighbor.  This  function  should  be  per  neighbor  and  not
 *	global.  However, for  a prototype,  they can  generate enough
 *	port numbers.
 *
 *********************************************************************/
static inline unsigned short allocate_neighbor_port(void)
{
	unsigned short rport;

	spin_lock_bh(&port_alloc_lock);
	rport = htons(alloc_ports >> 16);
	spin_unlock_bh(&port_alloc_lock);
	return rport;
}
/* */


/*********************************************************************
 *
 *	allocate_local_port:  allocates  a  local  port  number.  This
 *	function should be per neighbor  and not global.  However, for
 *	a prototype, they can generate enough port numbers.
 *
 *********************************************************************/
static inline unsigned short allocate_local_port(void)
{
	unsigned short lport;
	
	spin_lock_bh(&port_alloc_lock);
	lport = alloc_ports-- & 0xffff;	
	if (lport == 10001) {
		// alloc_ports  is  10000.  By subtracting  10001,  we
		// decrement the remote port by one.
		alloc_ports = alloc_ports - 10001;
	}
	spin_unlock_bh(&port_alloc_lock);
	return htons(lport);
}
/* allocate_local_port */

extern struct list_head ns_hashes;
extern struct rhashtable ns_hash_table;
/* */


/*********************************************************************
 *
 *	dysco_get_hashes: returns  the hash  tables associated  with a
 *	network  namespace.   This  is  a  new   implementation  using
 *	rhashtables. The old version is right below.
 *
 *********************************************************************/
static inline struct dysco_hashes *dysco_get_hashes(struct net *net_ns)
{
	struct dysco_hashes *dh;
	
	dh = rhashtable_lookup_fast(&ns_hash_table, &net_ns,
				    dysco_rhashtable_params_ns);
	return dh;
}
/* dysco_get_hashes */


/*********************************************************************
 *
 *	dysco_get_hashes_list: returns the hash tables associated with
 *	a network namespace.
 *
 *********************************************************************/
static inline struct dysco_hashes *dysco_get_hashes_list(struct net *net_ns)
{
	struct dysco_hashes	*dh, *dh_aux = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(dh, &ns_hashes, list) {
		if (dh->net_ns == net_ns) {
			dh_aux = dh;
			break;
		}
	}
	rcu_read_unlock();
	return dh_aux;
}
/* dysco_get_hashes_list */


/*********************************************************************
 *
 *	dysco_csum_replace4: recomputes the  checksum when a four-byte
 *	word (from) is replaced by another (to).
 *
 *********************************************************************/
static inline void dysco_csum_replace4(__sum16 *sum, __be32 from, __be32 to)
{
	// The checksum is stored as the one complement of the 16-bit sum in one's complement.
	__wsum unfold = ~csum_unfold(*sum); 
	__u64  partial;
	__wsum result;

	partial  = ~from;	// from is being subtracted from the checksum. Assign -from to partial.
	partial += to;
	partial += unfold;
	
	partial = (partial & 0xffffffff) + (partial >> 32);
	result  = (partial & 0xffffffff) + (partial >> 32);
	
	*sum = csum_fold(result);	
}
/* dysco_csum_replace4 */


/*********************************************************************
 *
 *	dysco_csum_replace2: recomputes checksum for a two-byte word.
 *
 *********************************************************************/
static inline void dysco_csum_replace2(__sum16 *sum, __be16 from, __be16 to)
{
	dysco_csum_replace4(sum, (__force __be32)from, (__force __be32) to);
}
/* dysco_csum_replace2 */	


/*********************************************************************
 *
 *	dysco_inet_proto_csum_replace4:  recomputes   checksum  for  a
 *	four-byte word that may be part of the pseudohdr.
 *
 *********************************************************************/
static inline void dysco_inet_proto_csum_replace4(__sum16 *sum, struct sk_buff *skb,
						  __be32 from, __be32 to, int pseudohdr)
{
	__u64  partial, cpartial;
	__wsum unfold, result;
	
	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		// The first two statements below are equivalent to
		// csum_partial(). I cannot move the two statements
		// outside of the if because most of the time neither the if
		// nor the else statements are executed.  If I move them,
		// then the computation is wasted.
		partial  = ~from;
		partial += to;
		cpartial = partial;
		
		unfold   = ~csum_unfold(*sum);
		partial += unfold;
		
		partial = (partial & 0xffffffff) + (partial >> 32);
		result  = (partial & 0xffffffff) + (partial >> 32);
		
		*sum = csum_fold(result);
		
		if (skb->ip_summed == CHECKSUM_COMPLETE && pseudohdr) {			
			cpartial += ~skb->csum;
			cpartial  = (cpartial & 0xffffffff) + (cpartial >> 32);
			result    = (cpartial & 0xffffffff) + (cpartial >> 32);			
			skb->csum = ~result;
		}
	}
	else if (pseudohdr) {
		unfold   = csum_unfold(*sum);
		partial  = ~from;
		partial += to;		
		partial += unfold;
		
		partial = (partial & 0xffffffff) + (partial >> 32);
		result  = (partial & 0xffffffff) + (partial >> 32);
		
		*sum = ~csum_fold(result);
	}
}
/* dysco_inet_proto_csum_replace4 */


/*********************************************************************
 *
 *	dysco_rdtsc: reads the Time Stamp Counter (TSC) register.
 *
 *********************************************************************/
static inline uint64_t dysco_rdtsc(void)
{
	uint32_t low, high;
	asm volatile("rdtsc" : "=a" (low), "=d" (high));
	return low  | (((uint64_t )high ) << 32);
}
/* */

#ifdef __BIG_ENDIAN
#define IP_TO_STR(ip) (((ip) >> 24)), (((ip) >> 16) & 0xFF), (((ip) >> 8) & 0xFF), ((ip) & 0xFF)
#else
#define IP_TO_STR(ip) ((ip) & 0xFF), (((ip) >> 8) & 0xFF), (((ip) >> 16) & 0xFF), (((ip) >> 24))
#endif

#define IP_STR	"%d.%d.%d.%d "
#define MAC_STR	"%02X:%02X:%02X:%02X:%02X:%02X "
#define MAC_TO_STR(m) m[0], m[1], m[2], m[3], m[4], m[5]
/* */


/*********************************************************************
 *
 *	dysco_lookup_out_rev: performs a lookup  in the output hash by
 *	reversing IP addresses and port numbers.
 *
 *********************************************************************/
static inline struct dysco_cb_out *dysco_lookup_out_rev(struct rhashtable *tbl,
							struct tcp_session *ss)
{
	struct tcp_session local_ss;
	
	local_ss.sip   = ss->dip;
	local_ss.dip   = ss->sip;
	local_ss.sport = ss->dport;
	local_ss.dport = ss->sport;
	return rhashtable_lookup_fast(tbl, &local_ss, dysco_rhashtable_params_out);
}
/* dysco_lookup_out_rev */


/*********************************************************************
 *
 *	dysco_get_ts_option:  parse the  TCP options  just to  get the
 *	timestamp fields. 
 *
 *********************************************************************/
static inline unsigned char *dysco_get_ts_option(struct tcphdr *th)
{
	unsigned char	*ptr;
	int		length;

	length = (th->doff*4) - sizeof(struct tcphdr);
	ptr    = (unsigned char *)(th + 1);

	while (length > 0) {
		int opcode = *ptr++;
		int opsize;

		switch (opcode) {
		case TCPOPT_EOL:
			return NULL;
			
		case TCPOPT_NOP:
			length--;
			continue;

		default:
			opsize = *ptr++;
			if (opsize < 2)
				return NULL;
			
			if (opsize > length)
				return NULL;
			
			if (opcode == TCPOPT_TIMESTAMP &&
			    opsize == TCPOLEN_TIMESTAMP)
					return ptr;
			ptr += opsize-2;
			length -= opsize;			
		}
	}
	return NULL;
}
/* dysco_get_ts_option */
