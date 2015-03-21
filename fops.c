#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/cdev.h>
#include <linux/semaphore.h>
#include <linux/sched.h>
#include <linux/ioctl.h>
#include <asm/uaccess.h>
#include "scull.h"

#define CHECK_PERM			\
	if(!capable(CAP_SYS_ADMIN))	\
		return -EPERM;


int scull_open(struct inode *inode,struct file *filp)
{
	struct scull_pipe *dev;
	dev = container_of(inode->i_cdev,struct scull_pipe,cdev);
	filp->private_data = dev;
	
	if((filp->f_flags & O_ACCMODE) == O_RDONLY) {

		dev->nreaders++;
		if(!dev->nwriters){
			if(filp->f_flags & O_NONBLOCK)
				return -EWOULDBLOCK;

			if(wait_event_interruptible(dev->inq,dev->nwriters))
				return -ERESTARTSYS;
		}
		goto out;
	}
		
	if(filp->f_flags & O_WRONLY) {
		dev->nwriters++;
		wake_up_interruptible(&dev->inq);
		goto out;
	}

	if(filp->f_flags & O_RDWR) {
		dev->nwriters++;
		dev->nreaders++;
		wake_up_interruptible(&dev->inq);
		goto out;
	}
	out:
	return 0;
}

int scull_release(struct inode *inode,struct file *filep)
{
	return 0;
}
ssize_t scull_read(struct file *filp,char __user *buf,size_t count,loff_t *f_pos)
{
	struct scull_pipe *dev = filp->private_data;

	/* Critical Section: using seamphore to 
	   avoid race conditions b/w threads 
	   accessing same data structures. Since our
	   scull device is not holding any other resource
	   which it need to release so it can go to sleep.
	   Therefore that locking mechanism should be used 
	    in which process can sleep hence SEMAPHORES.
	
          P functions are called "down" which has 3 variations:
	   => down() : decrements the value of semaphore and wait
		       as long as needed.
           => down_interruptible() : decrements the value of semaphore
				     but the operation is interruptible
	   => down_trylock : if semaphore not availble at time of call
			    , returns immediately.
	   P functions calling indicate that thread is ready to execute
	   in critical section */	

	if(down_interruptible(&dev->sem)) 
		return -ERESTARTSYS;

	if(!dev->nwriters)
		return 0;

	while(dev->rp == dev->wp) {
		up(&dev->sem);
			
		if(filp->f_flags & O_NONBLOCK) 
			return -EAGAIN;
		printk(KERN_WARNING "\"%s\" Reading: going to sleep\n", current->comm);
		
		if(wait_event_interruptible(dev->inq,dev->rp != dev->wp))
			return -ERESTARTSYS;
		
		if(down_interruptible(&dev->sem))
			return -ERESTARTSYS;
	}
	if(dev->wp > dev->rp) 
		count = min(count,(size_t)(dev->wp - dev->rp));
	else
		count = min(count,(size_t)(dev->end - dev->rp));
	
	if(copy_to_user(buf,dev->rp,count)) {
		up(&dev->sem);
		return -EFAULT;
	}
	dev->rp += count;

	if(dev->rp == dev->end)
		dev->rp = dev->buffer;
	up(&dev->sem);

	wake_up_interruptible(&dev->outq);
	printk(KERN_INFO "\"%s\" did read %li bytes \n",current->comm,(long)count);
	return count;
}

static int spacefree(struct scull_pipe *dev) 
{
	if(dev->rp == dev->wp)
		return dev->buffersize - 1;
	return  ((dev->rp + dev->buffersize - dev->wp) % dev->buffersize - 1);
}
/*
 * rp = 4 
 * bz = 10
 * wp = 6

 * (4 + 10 - 6 ) % 10 - 1 = 7
          
 * f f f f r   w f f f
 * 0 1 2 3 4 5 6 7 8 9
 */

static int get_writespace(struct scull_pipe *dev,struct file *filp)
{
	while(spacefree(dev) == 0) {
		DEFINE_WAIT(wait); 	/* can also use :
					 *		wait_queue_t wait;
					 *		init_wait(&wait);
					 */

	
		up(&dev->sem);
		if(filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		printk(KERN_INFO "\"%s\" writing: going to sleep\n",current->comm);
		
		prepare_to_wait(&dev->outq,&wait, TASK_INTERRUPTIBLE);	
		if(spacefree(dev) == 0)
			schedule();
		finish_wait(&dev->outq, &wait);
		if(signal_pending(current))
			return -ERESTARTSYS;
		if(down_interruptible(&dev->sem))
			return -ERESTARTSYS;
	}
	return 0;
}

ssize_t scull_write(struct file *filp,const char __user *buf,size_t count,
			loff_t *f_pos)
{
	struct scull_pipe *dev = filp->private_data;
	int result;
		
	if(down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	result = get_writespace(dev, filp);
	if(result)
		return result;
	
	count = min(count, (size_t)spacefree(dev));
	if(dev->wp >= dev->rp)
		count = min(count, (size_t)(dev->end - dev->wp));
	else 
		count = min(count, (size_t)(dev->rp - dev->wp -1));
	printk(KERN_WARNING "Accepting %li bytes to %p from %p\n",(long)count,dev->wp,buf);

	if(copy_from_user(dev->wp, buf, count)) {
		up(&dev->sem);
		return -EFAULT;
	}
	dev->wp += count;
	if(dev->wp == dev->end)	
		dev->wp = dev->buffer;
	up(&dev->sem);
		
	if(dev->async_queue)
		kill_fasync(&dev->async_queue,SIGIO,POLL_IN);
	printk(KERN_WARNING "\"%s\" did write %li bytes\n",current->comm,(long)count);
	return count;
}

struct file_operations scull_fops = {
	.owner = THIS_MODULE,
	.read  = scull_read,
	.write = scull_write,
	.open  = scull_open,
	.release = scull_release
};
