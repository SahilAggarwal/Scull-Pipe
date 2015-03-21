#include <linux/cdev.h>
#include <linux/semaphore.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "scull.h"

dev_t create_dev(void)
{
	dev_t dev;
	int result = -1;
	if(scull_major) {
		dev = MKDEV(scull_major,scull_minor);
		result = register_chrdev_region(dev, scull_nr_devs, "scull-pipe");
	} else {
		result = alloc_chrdev_region(&dev, scull_minor, scull_nr_devs,"scull-pipe" );
		scull_major = MAJOR(dev);
	}

	if(result < 0 ) {
		printk(KERN_WARNING "scull-pipe: could not get major number");
		return result;
	}
	return dev;
}

static void *scull_seq_start(struct seq_file *s,loff_t *pos)
{
	if(*pos >= scull_nr_devs )
		return NULL;
	return scull_devices + *pos;
}

static void *scull_seq_next(struct seq_file *s,void *v,loff_t *pos)
{
	(*pos)++;
	if(*pos >= scull_nr_devs)
		return NULL;
	return scull_devices + *pos;
}

static void scull_seq_stop(struct seq_file *s,void *v)
{

}

static int scull_seq_show(struct seq_file *s, void *v)
{
	struct scull_pipe *dev = (struct scull_pipe *)v;
	
	if(down_interruptible(&dev->sem)) 
		return -ERESTARTSYS;
	
	seq_printf(s,"\nDevice %i buffersize %i\n",
		   (int)(dev - scull_devices),dev->buffersize);

	up(&dev->sem);
	return 0;
}

static struct seq_operations scull_seq_ops = {
	.start = scull_seq_start,
	.stop  = scull_seq_stop,
	.show  = scull_seq_show,
	.next  = scull_seq_next
};

static int scull_proc_open(struct inode *inode,struct file *file)
{
	return seq_open(file,&scull_seq_ops);
}

static struct file_operations scull_proc_ops = {
	.owner = THIS_MODULE,
	.open = scull_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};

void procfile_setup(void)
{	
	proc_create_data("scullseq",0,NULL,&scull_proc_ops,NULL);
}
