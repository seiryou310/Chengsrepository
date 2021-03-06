#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#define GLOBALFIFO_SIZE 0x1000
#define MEM_CLEAR 0x1
#define globalfifo_MAJOR 515

static int globalfifo_major = globalfifo_MAJOR;

struct globalfifo_dev {
	struct cdev cdev;
	unsigned int current_len;	//fifo有效数据长度
	unsigned char mem[GLOBALFIFO_SIZE];
	struct semaphore sem;
	wait_queue_head_t r_wait;
	wait_queue_head_t w_wait;
};

struct globalfifo_dev *globalfifo_devp;

int globalfifo_open(struct inode *inode, struct file *filp)
{
	struct globalfifo_dev *dev;
	dev = container_of(inode->i_cdev, struct globalfifo_dev, cdev);
	filp->private_data = dev;
	return 0;
}

int globalfifo_release(struct inode *inode, struct file *filp)
{
	return 0;
}

/*
static int globalfifo_ioctl(struct inode *inodep, struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case MEM_CLEAR:
		if(down_interruptible(&dev->sem))
			return -ERESTARTSYS;
			
		memset(dev->mem,0,GLOBALFIFO_SIZE);
		up(&dev->sem);
		
		printk(KERN_INFO "globalfifo is set to zero\n");
		break;
		
	default:
		return -EINVAL;
	}
	return 0;
}
*/
static ssize_t globalfifo_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	int ret;
	struct globalfifo_dev *dev = filp->private_data;
	DECLARE_WAITQUEUE(wait, current);	//定义等待队列
	
	down(&dev->sem);
	add_wait_queue(&dev->r_wait, &wait); //进入读等待队列头
	
	while(dev->current_len != 0){
		if(filp->f_flags &O_NONBLOCK){
			ret = -EAGAIN;
			goto out;
		}
		__set_current_state(TASK_INTERRUPTIBLE);			//改变进程状态

		up(&dev->sem);
		
		schedule();											//调度其他进程
		if(signal_pending(current)){						
			ret = -ERESTARTSYS;
			goto out2;
		}
		
		down(&dev->sem);
	}
	
	if (count > dev->current_len)
		count = dev->current_len;
		
	if (copy_to_user(buf,dev->mem, count)){
		ret = -EFAULT;
		goto out;
	} else {	
		memcpy(dev->mem, dev->mem + count, dev->current_len -count);
		dev->current_len -= count;
		printk(KERN_INFO "read %d bytes(s),current_len:%d\n", count, dev->current_len);
		
		wake_up_interruptible(&dev->w_wait);
		
		ret = count;
	}
	out:up(&dev->sem);
	out2:remove_wait_queue(&dev->w_wait, &wait);
	set_current_state(TASK_RUNNING);
	return ret;	
}   
    
static ssize_t globalfifo_write(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{   
	struct globalfifo_dev *dev = filp->private_data;	//デバイスポイントを取得
	int ret;
	DECLARE_WAITQUEUE(wait, current);
	
	down(&dev->sem);
	add_wait_queue(&dev->w_wait, &wait);
	
	while(dev->current_len != GLOBALFIFO_SIZE){
		if(filp->f_flags &O_NONBLOCK){
			ret = -EAGAIN;
			goto out;
		}
		
		__set_current_state(TASK_INTERRUPTIBLE);
		up(&dev->sem);
		
		schedule();
		if(signal_pending(current)){
			ret = -ERESTARTSYS;
			goto out2;
		}
		
		down(&dev->sem);
	}
	
	if(count > GLOBALFIFO_SIZE - dev->current_len)
		count = GLOBALFIFO_SIZE - dev->current_len;
		
	if(copy_from_user(dev->mem + dev->current_len,buf,count)){
		ret = -EFAULT;
		goto out;
	} else {
		dev->current_len = count;
		printk(KERN_INFO "written %d bytes(s),current_len:%d\n", count, dev->current_len);
		
		wake_up_interruptible(&dev->r_wait);
		
		ret = count;
	}
	
	out: up(&dev->sem);
	out2:remove_wait_queue(&dev->w_wait,&wait);
	set_current_state(TASK_RUNNING);
	return ret;
}

static loff_t globalfifo_llseek(struct file *filp, loff_t offset, int orig)
{
	loff_t ret = 0;
	switch (orig) {
	case 0:
			if (offset < 0) {
				ret = -EINVAL;
				break;
			}
			if ((unsigned int)offset > GLOBALFIFO_SIZE){
				ret = -EINVAL;
				break;
			}
			filp->f_pos = (unsigned int) offset;
			ret = filp->f_pos;
			break;
	case 1:
			if ((filp->f_pos + offset) > GLOBALFIFO_SIZE){
				ret = -EINVAL;
				break;
			}
			if ((filp->f_pos + offset) < 0) {
				ret = -EINVAL;
				break;
			}
			filp->f_pos += offset;
			ret = filp->f_pos;
			break;
	case 2:
			if (offset > GLOBALFIFO_SIZE){
				ret = -EINVAL;
				break;
			}
			if (offset< 0) {
				ret = -EINVAL;
				break;
			}
			filp->f_pos = GLOBALFIFO_SIZE - offset;
			ret = filp->f_pos;
			break;
	default:
			ret = -EINVAL;
	}
	return ret;
}

static const struct file_operations globalfifo_fops = {
	.owner = THIS_MODULE,
	.llseek = globalfifo_llseek,
	.read = globalfifo_read,
	.write = globalfifo_write,
//	.ioctl = globalfifo_ioctl,
	.open = globalfifo_open,
	.release = globalfifo_release
};

static void globalfifo_setup_cdev(struct globalfifo_dev *dev, int index){
	int err,devno = MKDEV(globalfifo_major,index);
	
	cdev_init(&dev->cdev,&globalfifo_fops);
	dev->cdev.owner = THIS_MODULE;
	err = cdev_add(&dev->cdev,devno,1);
	if(err)
		printk(KERN_NOTICE"Error %d adding globalfifo",err);
}

int globalfifo_init(void)
{
	int result;
	dev_t devno = MKDEV(globalfifo_major,0);
	
	if(globalfifo_major)
		result = register_chrdev_region(devno,1,"globalfifo");
	else{
		result = alloc_chrdev_region(&devno,0,1,"globalfifo");
		globalfifo_major = MAJOR(devno);
	}
	if(result < 0)
		return result;
	globalfifo_devp = kmalloc(sizeof(struct globalfifo_dev), GFP_KERNEL);
	if(!globalfifo_devp){
		result = -ENOMEM;
		goto fail_malloc;
	}
	memset(globalfifo_devp, 0, sizeof(struct globalfifo_dev));
	
	globalfifo_setup_cdev(&globalfifo_devp, 0);
	init_MUTEX(&globalfifo_devp->sem);						//semaphore
	init_waitqueue_head(&globalfifo_devp->r_wait);			//初始化读等待队列头
	init_waitqueue_head(&globalfifo_devp->w_wait);			//初始化写等待队列头
	
	return 0;
	
fail_malloc:
	unregister_chrdev_region(devno,1);
	return result;	
}

void globalfifo_exit(void)
{
	cdev_del(&(globalfifo_devp[0].cdev));
	cdev_del(&(globalfifo_devp[1].cdev));
	kfree(globalfifo_devp);
	unregister_chrdev_region(MKDEV(globalfifo_major,0), 1);
}

MODULE_AUTHOR("Liang Cheng <seiryou310@163.com");
MODULE_LICENSE("Dual BSD/GPL");

module_param(globalfifo_major, int, S_IRUGO);

module_init(globalfifo_init);
module_exit(globalfifo_exit);
























