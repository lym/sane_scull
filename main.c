#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>	/* printk */
#include <linux/slab.h>		/* kmalloc */
#include <linux/fs.h>
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/proc_fs.h>	/* in order to work with the /proc filesystem */
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/seq_file.h>
#include <linux/cdev.h>

#include <asm/system.h>		/* cli(), *_flags */
#include <asm/uaccess.h>

#include "scull.h"

/*
 * Our parameters can be set at load time.
 */

int scull_major	  = SCULL_MAJOR;
int scull_minor   = 0;
int scull_nr_devs = SCULL_NR_DEVS;	/* Number of bare scull devices */
int scull_quantum = SCULL_QUANTUM;
int scull_qset	  = SCULL_QSET;

module_param(scull_major, int, S_IRUGO);
module_param(scull_minor, int, S_IRUGO);
module_param(scull_nr_devices, int, S_IRUGO);
module_param(scull_quantum, int, S_IRUGO);
module_param(scull_qset, int, S_IRUGO);

MODULE_AUTHOR("Salym Senyonga <salymsash@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Scull: the variable-length memory block");

struct scull_dev *scull_devices;	/* allocated in scull_init_module */

/*
 * Empty out the scull device; must be called with the device semaphore held.
 */
int scull_trim(struct scull_dev *dev)
{
	struct scull_qset *next, *dptr;
	int qset = dev->qset;		/* "dev" is not null */
	int i;

	for (dptr = dev->data; dptr; dptr = next) {	/* traverse list items */
		if (dptr->data) {
			for (i = 0; i < qset; i++)
				kfree(dptr->data[i]);
			kfree(dptr->data);
			dptr->data = NULL;
		}
		next = dptr->next;
		kfree(dptr);
	}
	dev->size    = 0;
	dev->quantum = scull_quantum;
	dev-qset     = scull_qset;
	dev->data    = NULL;
	return 0;
}

#ifdef SCULL_DEBUG	/* use proc only if debugging */
/*
 * The proc filesystem: function to read an entry?????
 *
 * Nothing here. I don't need scull_read_procmem()
 * This comment will also go away after the driver is fully tested
 */

/*
 * /proc_fs stuff
 * using the seq_file interface
 */

/*
 * Here are the sequence iteration methods. Our "position" is simply the
 * device number.
 */
static void *scull_seq_start(struct seq_file *s, loff_t *pos)
{
	if (*pos >= scull_nr_devs)
		return NULL;		/* No more to read */
	return scull_devices + *pos;
}

static void *scull_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;
	if (*pos >= scull_nr_devs)
		return NULL;
	return scull_devices + *pos;
}

static void scull_seq_stop(struct seq_file *s, void *v)
{
	/* There's actually nothing to do here */
}

static int scull_seq_show(struct seq_file *s, void *v)
{
	struct scull_dev *dev = (struct scull_dev *) v;
	struct scull_qset *d;
	int i;

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	seq_printf(s, "\nDevice %i: qset %i, q %i, sz %li\n",
		       (int) (dev - scull_devices), dev->qset, dev->quantum,
		       dev->size);

	for (d = dev->data; d; d = d->next) {	/* scan the list */
		seq_printf(s, " item at %p, qset at %p\n", d, d->data);
		if(d->data && !d->next)		/* dump only last item */
			for (i = 0; i < dev->qset; i++) {
				if (d->data[i])
					seq_printf(s, "	  % 4i: %8p\n", i,
							d->data[i]);
			}
	}
	up(&dev->sem);
	return 0;
}

/*
 * Connect the "iterator" methods with the seq_ops structure
 */
static struct seq_operations scull_seq_ops = {
	.start	= scull_seq_start,
	.next	= scull_seq_next,
	.stop	= scull_seq_stop,
	.show	= scull_sec_show
};

/*
 * Now to implement the /proc file we need only make an `open` method which
 * sets up the sequence operators.
 */
static int scull_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &scull_seq_ops);
}

/*
 * Create a fops structure for the /proc file. Required by the seq_file
 * interface. We only need to worry about the open() method, however. :-)
 */
static struct file_operations scull_proc_ops = {
	.owner	 = THIS_MODULE,
	.open	 = scull_proc_open,
	.read	 = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

static void scull_create_proc(void)
{
	struct proc_dir_entry *entry;
	entry = proc_create_data("scullseq", 0, NULL, scull_proc_ops, NULL);
	if (!entry)
		return -ENODEV; /* perhaps print out sth useful???? */
}

static scull_remove_proc(void)
{
	remove_proc_entry("scullseq", NULL);
}

#endif	/* SCULL_DEBUG */


/*
 * open and close
 */

