#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kdev_t.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/semaphore.h>
/*<linux/rwsem.h> : can be used to use sem only
		    when concurrent read task is allowed 

                   1) void down_read(struct rw_semaphore *)
		   2) int down_read_trylock(struct rw_semaphore *)
		   3) void up_read (struct rw_semaphore *) 
     
		   1,2,3 similar for write.
		   
                  4) void downgrade_write(struct rw_semaphore *) 
		     Allow readers to read once write is completed.
		     Mostly in situation when writer lock is needed
		     for quick change.
                   */ 
#include "scull.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sahil Aggarwal <sahil.agg15@gmail.com>");
MODULE_DESCRIPTION("Scull Driver");

unsigned int scull_major = SCULL_MAJOR;
unsigned int scull_minor = SCULL_MINOR;
unsigned int scull_nr_devs = SCULL_NR_DEVS;

struct scull_pipe *scull_devices;
static dev_t dev;

static int scull_fail_dev(struct scull_pipe *dev)
{
	kfree(dev->buffer);
	cdev_del(&dev->cdev);
	return 0;
}

static void scull_setup_dev(struct scull_pipe *dev,int index)
{
	int err, devno = MKDEV(scull_major,scull_minor + index);
	cdev_init(&dev->cdev,&scull_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops   = &scull_fops;
	init_waitqueue_head(&dev->inq);
	init_waitqueue_head(&dev->outq);

	dev->buffer = kmalloc(SCULL_BUFFER,GFP_KERNEL);
	if(!dev->buffer) {
		printk(KERN_NOTICE "Error allocating memory to the device %d\n",index);
		return;
	}

	dev->end = dev->buffer + SCULL_BUFFER;
	dev->buffersize = SCULL_BUFFER;
	dev->rp = dev->buffer;
	dev->wp = dev->buffer;
	dev->nwriters = 0;
	dev->nreaders = 0;

	err = cdev_add(&dev->cdev, devno, 1);
	
	if(err) {
		printk(KERN_NOTICE "Error %d adding scull %d",err,index);
		scull_fail_dev(dev);
	}
	
}

static int __init scull_init(void)
{
	int i;
	printk(KERN_INFO "scull-pipe: initializing ...");
	if((dev = create_dev()) == -1){
		printk(KERN_INFO "scull-pipe: initialization failed\n");
		goto out;
	}
	
	scull_devices = kmalloc(sizeof(struct scull_pipe)*scull_nr_devs,GFP_KERNEL);
	memset(scull_devices,0,sizeof(struct scull_pipe)*scull_nr_devs);

	for(i=0;i<scull_nr_devs;i++) {
		/* initialize semaphore: set value to 1 */
		sema_init(&scull_devices[i].sem,1);
		scull_setup_dev(&scull_devices[i],i);
	}
	procfile_setup();
	
	printk(KERN_INFO "scull-pipe: initialized\n");
	
	return 0;
	out:
		return -1;
}

void __exit scull_exit(void)
{
	printk(KERN_INFO "scull: unitializiing ...\n");
	if(scull_devices) {
		int i;
		for(i=0;i<scull_nr_devs;i++) {
			printk(KERN_WARNING "Removing dev %i.\n",i);
			unregister_chrdev_region(dev,scull_nr_devs);
			cdev_del(&scull_devices[i].cdev);
		}
	}
	printk(KERN_INFO "scull: unitialized\n");
	
}

module_init(scull_init);
module_exit(scull_exit);


