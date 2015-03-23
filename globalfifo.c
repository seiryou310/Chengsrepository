
struct globalfifo_dev{
	struct cdev cdev;
	unsigned int current_len;
	unsigned char mem[GLOBALFIFO_SIZE];
	struct semaphore sem;
	wait_queue_head_t r_wait;
	wait_queue_head_t w_wait;
}

int gobalfifo_init(void){
	int ret;
	dev_t devno = MKDEV(globalfifo_major,0);	//获取设备号

	if(globalfifo_major)
		ret = register_chrdev_regin(devno,1,"globalfifo");	//注册字符设备
	else{
		ret = alloc_chrdev_region(&devno,0,1,"globalfifo");
		globalfifo_major = MAJOR(devno);
	}
	if(ret<0)
		return ret;
	globalfifo_devp = kmalloc(sizeof(struct globalfifo_dev));
	if(!globalfifo_devp){
		ret = -ENOMEN;
		goto fail_malloc;
	}

	memset(globalfifo_devp,0,sizeof(struct globalfifo_dev));

	globalfifo_setup_cdev(globalfifo_devp,0);

	init_MUTEX(&globalfifo_devp->sem);
	init_waitqueue_head(&globalfifo_devp->r_wait);
	init_waitqueue_head(&globalfifo_devp->w_wait);

	return 0;

	fail_malloc:
		unregister_chrdev_region(devno,1);
	return ret;
}

static ssize_t globalfifo_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos){
	int ret;
	struct globalfifo_dev *dev = filp->private_data;
	DECLARE_WAITQUEUE(wait,current);

	down(&dev->sem);
	add_wait_queue(&dev->r_wait,&wait);

	while(dev->current_len != 0){
		if(filp->f_flags & O_NONBLOCK){
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

	if(count>dev->current_len)
		count = dev->current_len;

	if(copy_to_user(buf, dev->mem, count)){
		ret = -EFAULT;
		goto out;
	}else{
		memcpy(dev->mem, dev->mem + count, dev->current_len - count);
		dev->current_len -= count;
		printk(KERN_INFO "read %d bytes(s), current_len: %d\n",count,dev->current_len);
	
		wake_up_interruptible(&dev->w_wait);

		ret = count;
	}
	out:
		up(&dev->sem);
	out2:
		remove_wait_queue(&dev->w_wait, &wait);
	set_current_state(TASK_RUNNING);
	return ret;
}

static ssize_t globalfifo_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos){
	int ret;
	struct globalfifo_dev *dev = filp->private_data;
	DECLARE_WAITQUEUE(wait,current);

	down(&dev->sem);
	add_wait_queue(&dev->r_wait,&wait);

	while(dev->current_len != GLOBALFIFO_SIZE){
		if(filp->f_flags & O_NONBLOCK){
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

	if(copy_to_user(dev->men + dev->current_len, buf, count)){
		ret = -EFAULT;
		goto out;
	}else{
		dev->current_len += count;
		printk(KERN_INFO "written %d bytes(s), current_len: %d\n",count,dev->current_len);
	
		wake_up_interruptible(&dev->r_wait);

		ret = count;
	}
	out:
		up(&dev->sem);
	out2:
		remove_wait_queue(&dev->w_wait, &wait);
	set_current_state(TASK_RUNNING);
	return ret;
}