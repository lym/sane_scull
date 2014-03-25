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
module_param(scull_nr_devs, int, S_IRUGO);
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
	dev->qset     = scull_qset;
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
int scull_open(struct inode *inode, struct file *filp)
{
	struct scull_dev *dev;		/* device information */

	dev = container_of(inode->i_cdev, struct scull_dev, cdev);
	filp->private_data = dev;	/* store dev for quick access in future */

	/* Now trim to 0 the length of the device if open was write-only */
	if ((filp->f_flags & O_ACCMODE) == O_WRONLY) {
		if (down_interruptible(&dev->sem))
			return -ERESTARTSYS;
		scull_trim(dev);	/* ignore errors */
		up(&dev->sem);
	}
	return 0;
}

int scull_release(struct inode *inode, struct file *filp)
{
	return 0;
}

/*
 * Follow the list
 */
struct scull_qset *scull_follow(struct scull_dev *dev, int n)
{
	struct scull_qset *qs = dev->data;

	/* Allocate first qset explicitly if need be */
	if (!qs) {
		qs = dev->data = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
		if (qs == NULL)
			return NULL;	/* Never mind */
		memset(qs, 0, sizeof(struct scull_qset));
	}

	/* Then follow the list */
	while (n--) {
		if (!qs->next) {
			qs->next = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
			if (qs->next == NULL)
				return NULL;
			memset(qs->next, 0, sizeof(struct scull_qset));
		}
		qs = qs->next;
		continue;
	}
	return qs;
}

/*
 * Data Management: read and write
 *
 * Each invocation of `scull_read` deals only with a single data quantum,
 * without implementing a loop to gather all the data. If the reading program
 * really wants more data, it reiterates the call.
 */

ssize_t scull_read(struct file *filp, char __user *buf, size_t count,
		   loff_t *f_pos)
{
	struct scull_qset *dptr;	/* the first listitem */
	struct scull_dev *dev = filp->private_data;
	int item;
	int itemsize;
	int s_pos;
	int q_pos;
	int rest;
	int quantum	= dev->quantum;
	int qset	= dev->qset;
	ssize_t retval	= 0;

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	if (*f_pos >= dev->size)
		goto out;
	if (*f_pos + count > dev->size)
		count = dev->size - *f_pos;

	/* find listitem, qset, index, and offset in the quantum */
	item = (long) *f_pos / itemsize;
	rest = (long) *f_pos % itemsize;
	s_pos	= rest / quantum;
	qpos	= rest % quantum;

	/* follow the list up to the right position */
	dptr	= scull_follow(dev, item);

	if (dptr == NULL || !dptr->data || !dptr->data[s_pos])
		goto out;	/* don't fill holes */

	/* read only up to the end of this quantum */
	if (count > quantum - q_pos)
		count = quantum - q_pos;

	if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count)) {
		retval = -EFAULT;
		goto out;
	}
	*f_pos	+= count;

out:
	up(&dev->sem);
	return retval;
}

ssize_t scull_write(struct file *filp, const char __user *buf, size_t count,
		    loff_t *f_pos)
{
	struct scull_dev *dev = filp->private_data;
	struct scull_qset *dptr;
	int quantum	= dev->quantum;
	int qset	= dev->qset;
	int itemsize	= quantum * qset;
	int item;
	int s_pos;
	int q_pos;
	int rest;
	ssize_t retval	= -ENOMEM;	/* value used in "goto out" statements */

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	/* find listitem, q_set, index and offset in the quantum */
	item	= (long) *f_pos / itemsize;
	rest	= (long) *f_pos % itemsize;
	s_pos	= rest / quantum;
	q_pos	= rest % quantum;

	/* follow the list up to the right position */
	dptr = scull_follow(dev, item);
	if (dptr == NULL)
		goto out;
	if (!dptr->data) {
		dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
		if (!dptr->data)
			goto out;
		memset(dptr->data, 0, qset * sizeof(char *));
	}
	if (!dptr->data[s_pos]) {
		dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
		if (!dptr->data[s_pos])
			goto out;
	}

	/* write only upto the end of this quantum */
	if (count > (quantum - q_pos))
		count = quantum - q_pos;

	if (copy_from_user((dptr->data[s_pos] + q_pos), buf, count)) {
		retval = -EFAULT;
		goto out;
	}
	*f_pos += count;
	retval = count;

	/* update the size */
	if (dev->size < *f_pos)
		dev->size = *f_pos;

out:
	up(&dev->sem);
	return retval;
}

/*
 * The ioctl() implementation
 */

int scull_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	int err = 0;
	int tmp;
	int retval = 0;

	/*
	 * extract the type and number bitfields and don't decode
	 * wrong cmds: return ENOTTY (inapproprate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != SCULL_IOC_MAGIC)
		return -ENOTTY;
	if (_IOC_NR(cmd) > SCULL_IOC_MAXNR)
		return -ENOTTY;

	/*
	 * the direction is a bitmask, and VERIFY_WRITE catches R/W transfers.
	 * `Type` is user-oriented, while access_ok is kernel-oriented, so the
	 * concept of "read" and "write" is reversed
	 */
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, (void __user *) arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok(VERIFY_READ, (void __user *) arg, _IOC_SIZE(cmd));
	if (err)
		return -EFAULT;
	switch (cmd) {
		case SCULL_IOCRESET:
			scull_quantum	= SCULL_QUANTUM;
			scull_qset	= SCULL_QSET;
			break;

		case SCULL_IOCSQUANTUM:		/* Set: arg points to the value */
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
			retval	= __get_user(scull_quantum, (int __user *) arg);
			break;
		case SCULL_IOCTQUANTUM:		/* Tell: arg is the value */
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
			scull_quantum = arg;
			break;

		case SCULL_IOCGQUANTUM:		/* Get: arg is pointer to result */
			retval	= __put_user(scull_quantum, (int __user *) arg);
			break;

		case SCULL_IOCQQUANTUM:		/* Query: return it (it's positive */
			return scull_quantum;

		case SCULL_IOCXQUANTUM:		/* eXchange: use arg as pointer */
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
			tmp	= scull_quantum;
			retval	= __get_user(scull_quantum, (int __user *) arg);
			if (retval == 0)
				retval = __put_user(tmp, (int __user *) arg);
			break;

		case SCULL_IOCHQUANTUM:		/* sHift: like Tell + Query */
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
			tmp = scull_quantum;
			scull_quantum = arg;
			return tmp;

		case SCULL_IOCSQSET:
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
			retval	= __get_user(scull_qset, (int __user *) arg);
			break;

		case SCULL_IOCTQSET:
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
			scull_qset = arg;
			break;

		case SCULL_IOCGQSET:
			retval = __put_user(scull_qset, (int __user *) arg);
			break;

		case SCULL_IOCQQSET:
			return scull_qset;

		case SCULL_IOCXQSET:
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
			tmp = scull_qset;
			retval = __get_user(scull_qset, (int __user *) arg);
			if (retval == 0)
				retval = put_user(tmp, (int __user *)arg);
			break;

		case SCULL_IOCHQSET:
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
			tmp	= scull_qset;
			scull_qset = arg;
			return tmp;

			/*
			 * The following two change the buffer size for
			 * scullpipe. The scullpipe device uses this same ioctl
			 * method, just to write less code. Actually, its the
			 * same driver.
			 */

		case SCULL_P_IOCTSIZE:
			scull_p_buffer = arg;
			break;

		case SCULL_P_IOCQSIZE:
			return scull_p_buffer;

		default:	/* redundant as cmd was checked against MAXNR */
			return -ENOTTY;
	}
	return retval;
}

/*
 * The "extended" operations -- only seek
 */

loff_t scull_llseek(struct file *filp, loff_t off, int whence)
{
	struct scull_dev *dev = filp->private_data;
	loff_t newpos;

	switch (whence) {
		case 0:		/* SEEK_SET */
			newpos = off;
			break;

		case 1:		/* SEEK_CUR */
			newpos = filp->f_pos + off;
			break;

		case 2:		/* SEEK_END */
			newpos = dev->size + off;
			break;

		default:	/* can't happen */
			return -EINVAL;
	}
	if (newpos < 0)
		return -EINVAL;
	filp->f_pos = newpos;
	return newpos;
}

struct file_operations scull_fops = {
	.owner	= THIS_MODULE,
	.llseek	= scull_llseek,
	.read	= scull_read,
	.write	= scull_write,
	.ioctl	= scull_ioctl,
	.open	= scull_open,
	.release = scull_release
};

/*
 * Finally, the module stuff
 */

/* The cleanup function is used to handle initialization failures as well therefore,
 * it must be careful to work correctly even if some of the items have not been
 * initialized
 */
void scull_cleanup_module(void)
{
	int i;
	dev_t dev_no = MKDEV(scull_major, scull_minor);

	/* Get rid of our char dev entries */
	if (scull_devices) {
		for (i = 0; i < scull_nr_devices; i++) {
			scull_trim(scull_devices + i);
			cdev_del(&scull_devices[i].cdev);
		}
		kfree(scull_devices);
	}

#ifdef SCULL_DEBUG	/* use proc only if debugging */
	scull_remove_proc();
#endif

	/* cleanup_module is never called if registering failed */
	unregister_chrdev_region(devno, scull_nr_devs);

	/* and call the cleanup functions for friend devices */
	scull_p_cleanup();
	scull_access_cleanup();
}

/*
 * Set up the char_dev structure for this device
 */
static void scull_setup_cdev(struct scull_dev *dev, int index)
{
	int err;
	int devno = MKDEV(scull_major, scull_minor + index);

	cdev_init(&dev->cdev, &scull_fops);
	dev->cdev.owner	= THIS_MODULE;
	dev->cdev.ops	= &scull_fops;
	err		= cdev_add(&dev->cdev, devno, 1);

	if (err)
		printk(KERN_NOTICE "Error %d adding scull%d" err, index);
}

int scull_init_module(void)
{
	int result;
	int i;
	dev_t dev = 0;

	/* Get a range of minor numbers to work with, asking for a dynamic
	 * major unless directed otherwise at load time.
	 */
	if (scull_major) {
		dev = MKDEV(scull_major, scull_minor);
		result = register_chrdev_region(dev, scull_nr_devices, "scull");
	}
	else {
		result = alloc_chrdev_region(&dev, scull_minor, scull_nr_devs,
					     "scull");
		scull_major = MAJOR(dev);
	}
	if (result < 0) {
		printk(KERN_WARNING "scull: can't get major %d\n", scull_major);
		return result;
	}

	/*
	 * allocate the devices -- we can't have them static, as the number can
	 * be specified at load time
	 */
	scull_devices	 = kmalloc(scull_nr_devices * sizeof(struct scull_dev),
				   GFP_KERNEL);
	if (!scull_devices) {
		result = -ENOMEM;
		goto fail;
	}
	memset(scull_devices, 0, scull_nr_devs * sizeof(struct scull_dev));

	/* Initialize each device */
	for (i = o; i < scull_nr_devs; i++) {
		scull_devices[i].quantum = scull_quantum;
		scull_devices[i].qset	 = scull_qset;
		init_MUTEX(&scull_devices[i].sem);
		scull_setup_cdev(&scull_devices[i], i);
	}

	/* At this point call the init function for any friend device */
	dev = MKDEV(scull_major, scull_minor + scull_nr_devs);
	dev += scull_p_init(dev);
	dev += scull_access_init(dev);

#ifdef SCULL_DEBUG
	scull_create_proc();
#endif

	return 0;

fail:
	scull_cleanup_module();
	return result;
}

module_init(scull_init_module);
module_exit(scull_cleanup_module);
