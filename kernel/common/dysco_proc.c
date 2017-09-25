/*
 *
 *	Dynamic Service Chaining with Dysco
 *	Dysco kernel agent: dysco_proc.c
 *
 *	This module  implements the  interface with /proc  file system
 *	for measurement data.
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
#include <linux/kernel.h>	
#include <linux/module.h>	
#include <linux/proc_fs.h>	
#include <linux/seq_file.h>	
#include <linux/netdevice.h>
/* */
#include "dysco.h"
#define PROC_NAME	"dysco"
/* */
unsigned rewrite_output_samples[DYSCO_MEASUREMENT_SAMPLES];
unsigned translate_output_samples[DYSCO_MEASUREMENT_SAMPLES];
unsigned rewrite_input_samples[DYSCO_MEASUREMENT_SAMPLES];
unsigned translate_input_samples[DYSCO_MEASUREMENT_SAMPLES];
/* */
// For debugging only, but the variables should be per session.
unsigned long tcp_sack_rewrites = 0;
unsigned long tcp_ts_rewrites   = 0;
unsigned long tcp_tsr_rewrites  = 0;
/* */
unsigned tot_namespaces = 0;
/* */
struct proc_dir_entry *dysco_dir_entry;
/* */


/*********************************************************************
 *
 *	dysco_rewrite_output_seq_start: This function is called at the
 *	beginning of a sequence.  ie, when:
 *
 *	- the /proc  file is read (first time)
 *	- after the function stop (end of sequence)
 *
 *********************************************************************/
static void *dysco_rewrite_output_seq_start(struct seq_file *s, loff_t *pos)
{
	if (*pos == 0) {	
		return &rewrite_output_samples[0]; 
	}
	else {
		*pos = 0; 
		return NULL;
	}
}
/* dysco_rewrite_output_seq_start */


/*********************************************************************
 *
 *	dysco_translate_output_seq_start: same as dysco_rewrite_output_seq_start
 *
 *********************************************************************/
static void *dysco_translate_output_seq_start(struct seq_file *s, loff_t *pos)
{
	if (*pos == 0) {	
		return &translate_output_samples[0]; 
	}
	else {
		*pos = 0; 
		return NULL;
	}
}
/* dysco_translate_output_seq_start */


/*********************************************************************
 *
 *	dysco_rewrite_input_seq_start: same as dysco_rewrite_output_seq_start
 *
 *********************************************************************/
static void *dysco_rewrite_input_seq_start(struct seq_file *s, loff_t *pos)
{
	if (*pos == 0) {	
		return &rewrite_input_samples[0]; 
	}
	else {
		*pos = 0; 
		return NULL;
	}
}
/* dysco_rewrite_input_seq_start */


/*********************************************************************
 *
 *	dysco_translate_input_seq_start: same as dysco_rewrite_output_seq_start
 *
 *********************************************************************/
static void *dysco_translate_input_seq_start(struct seq_file *s, loff_t *pos)
{
	if (*pos == 0) {	
		return &translate_input_samples[0]; 
	}
	else {
		*pos = 0; 
		return NULL;
	}
}
/* dysco_translate_input_seq_start */


/*********************************************************************
 *
 *	dysco_seq_show: this function  is called for each  "step" of a
 *	sequence
 *
 *********************************************************************/
static int dysco_seq_show(struct seq_file *s, void *v)
{
	int		i;
	unsigned	*n = (unsigned *)v;
	
	for (i = 0; i < DYSCO_MEASUREMENT_SAMPLES; i++)
		seq_printf(s, "%u\n", n[i]);
	return 0;
}
/* dysco_seq_show */


/*********************************************************************
 *
 *	dysco_seq_next: this function is called after the beginning of
 *	a sequence.  It's called until  the return is NULL (this ends
 *	the sequence).
 *
 *********************************************************************/
static void *dysco_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	return NULL;
}
/* dysco_seq_next */


/*********************************************************************
 *
 *	dysco_seq_stop:  this  function is  called  at  the end  of  a
 *	sequence
 * 
 *********************************************************************/
static void dysco_seq_stop(struct seq_file *s, void *v)
{
	/* nothing to do, we use global variables */
}
/* dysco_seq_stop */


/* Data structure to initialize the handlers for the proc file system */
static struct seq_operations dysco_rewrite_output_seq_ops = {
	.start = dysco_rewrite_output_seq_start,
	.next  = dysco_seq_next,
	.stop  = dysco_seq_stop,
	.show  = dysco_seq_show
};
/* */


static struct seq_operations dysco_translate_output_seq_ops = {
	.start = dysco_translate_output_seq_start,
	.next  = dysco_seq_next,
	.stop  = dysco_seq_stop,
	.show  = dysco_seq_show
};
/* */


static struct seq_operations dysco_rewrite_input_seq_ops = {
	.start = dysco_rewrite_input_seq_start,
	.next  = dysco_seq_next,
	.stop  = dysco_seq_stop,
	.show  = dysco_seq_show
};
/* */


static struct seq_operations dysco_translate_input_seq_ops = {
	.start = dysco_translate_input_seq_start,
	.next  = dysco_seq_next,
	.stop  = dysco_seq_stop,
	.show  = dysco_seq_show
};
/* */


/*********************************************************************
 *
 *	dysco_rewrite_output_proc_open: this  function is  called when
 *	the /proc file is open.
 *
 *********************************************************************/
static int dysco_rewrite_output_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &dysco_rewrite_output_seq_ops);
};
/* dysco_rewrite_output_proc_open */


/*********************************************************************
 *
 *	dysco_translate_output_proc_open: this function is called when
 *	the /proc file is open.
 *
 *********************************************************************/
static int dysco_translate_output_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &dysco_translate_output_seq_ops);
};
/* dysco_translate_output_proc_open */


/*********************************************************************
 *
 *	dysco_rewrite_input_proc_open:  this function  is called  when
 *	the /proc file is open.
 *
 *********************************************************************/
static int dysco_rewrite_input_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &dysco_rewrite_input_seq_ops);
};
/* dysco_rewrite_input_proc_open */


/*********************************************************************
 *
 *	dysco_translate_input_proc_open: this function  is called when
 *	the /proc file is open.
 *
 *********************************************************************/
static int dysco_translate_input_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &dysco_translate_input_seq_ops);
};
/* dysco_translate_input_proc_open */


/*********************************************************************
 *
 * This structure gather "function" that manage the /proc file
 *
 */
static struct file_operations dysco_rewrite_output_file_ops = {
	.owner   = THIS_MODULE,
	.open    = dysco_rewrite_output_proc_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};
/* */	


static struct file_operations dysco_translate_output_file_ops = {
	.owner   = THIS_MODULE,
	.open    = dysco_translate_output_proc_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};
/* */	


static struct file_operations dysco_rewrite_input_file_ops = {
	.owner   = THIS_MODULE,
	.open    = dysco_rewrite_input_proc_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};
/* */	


static struct file_operations dysco_translate_input_file_ops = {
	.owner   = THIS_MODULE,
	.open    = dysco_translate_input_proc_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};
/* */


/*********************************************************************
 *
 *	dysco_proc_init: initializes  the /proc file system  to create
 *	the variables used for Dysco measurements.
 *
 *********************************************************************/
int dysco_proc_init(void)
{
	struct proc_dir_entry *entry;
	
	dysco_dir_entry = proc_net_mkdir(&init_net, PROC_NAME, init_net.proc_net);
	if (!dysco_dir_entry) {
		printk(DYSCO_ALERT "Error creating dysco directory entry in /proc\n");
		return 0;
	}

	entry = proc_create("rewrite_output_samples", S_IRUGO, dysco_dir_entry,
			    &dysco_rewrite_output_file_ops);
	if (!entry)
		printk(DYSCO_ALERT
		       "Error creating proc_dir_entry rewrite_output_samples\n");
	
	entry = proc_create("translate_output_samples", S_IRUGO, dysco_dir_entry,
			    &dysco_translate_output_file_ops);
	if (!entry)
		printk(DYSCO_ALERT
		       "Error creating proc_dir_entry translate_output_samples\n");
	
	entry = proc_create("rewrite_input_samples", S_IRUGO, dysco_dir_entry,
			    &dysco_rewrite_input_file_ops);
	if (!entry)
		printk(DYSCO_ALERT
		       "Error creating proc_dir_entry rewrite_input_samples\n");
	
	entry = proc_create("translate_input_samples", S_IRUGO, dysco_dir_entry,
			    &dysco_translate_input_file_ops);
	if (!entry)
		printk(DYSCO_ALERT
		       "Error creating proc_dir_entry translate_input_samples\n");
	
	return 0;
}
/* dysco_proc_init */


/*********************************************************************
 *
 *	dysco_proc_cleanup:  removes  the   /proc  entries  for  Dysco
 *	measurements.
 *
 *********************************************************************/
void dysco_proc_cleanup(void)
{
	if (dysco_dir_entry) {
		remove_proc_entry("rewrite_output_samples", dysco_dir_entry);
		remove_proc_entry("translate_output_samples", dysco_dir_entry);
		remove_proc_entry("rewrite_input_samples", dysco_dir_entry);
		remove_proc_entry("translate_input_samples", dysco_dir_entry);
		remove_proc_entry(PROC_NAME, init_net.proc_net);
	}
}
/* dysco_proc_cleanup */
