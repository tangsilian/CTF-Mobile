#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/cred.h>

#ifndef MEMDEV_MAJOR
#define MEMDEV_MAJOR 0
#endif

#ifndef MEMDEV_NR_DEVS
#define MEMDEV_NR_DEVS 3
#endif

#ifndef MEMDEV_SIZE
#define MEMDEV_SIZE 4096
#endif

struct mem_init {
	uint32_t idx;
	uint32_t len;
};

struct mem_dev
{
	unsigned long size;
	char *data;
};

static uint32_t mem_major = MEMDEV_MAJOR;
static uint32_t mem_minor = 0;

static struct mem_dev *mem_devp;
static struct cdev cdev; 
static dev_t devno;
static struct class *dev_class = NULL;
static struct device *dev_device[MEMDEV_NR_DEVS];

int mem_open(struct inode *inode, struct file *filp)
{
	struct mem_dev *dev;

	int num = MINOR(inode->i_rdev);

	if (num >= MEMDEV_NR_DEVS) 
		return -ENODEV;

	dev = &mem_devp[num];

	filp->private_data = dev;
	filp->f_mode |= FMODE_UNSIGNED_OFFSET;

	return 0; 
}

int mem_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t mem_read(struct file *filp, char __user *buf, size_t size, loff_t *ppos)
{
	unsigned long p =  *ppos;
	unsigned int count = size;
	int ret = 0;
	struct mem_dev *dev = filp->private_data;

	if((dev->size >> 24 & 0xff) != 0x5a)
		return -EFAULT;

	if (p > dev->size)
		return -ENOMEM;

	if (count > dev->size - p)
		count = dev->size - p;

	if (copy_to_user(buf, (void*)(dev->data + p), count)) {
		ret =  -EFAULT;
	} else {
		*ppos += count;
		ret = count;
	}

	return ret;
}

static ssize_t mem_write(struct file *filp, const char __user *buf, size_t size, loff_t *ppos)
{
	unsigned long p =  *ppos;
	unsigned int count = size;
	int ret = 0;
	struct mem_dev *dev = filp->private_data;

	if((dev->size >> 24 & 0xff) != 0x5a)
		return -EFAULT;

	if (p > dev->size)
		return -ENOMEM;

	if (count > dev->size - p)
		count = dev->size - p;
	
	if (copy_from_user((void *)(dev->data + p), buf, count)) {
		ret =  -EFAULT;
	} else {
		*ppos += count;
		ret = count;
	}

	return ret;
}

static long mem_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct mem_init data;

	if(!arg)
		return -EINVAL;

	if(copy_from_user(&data, (void *)arg, sizeof(data))) {
		return -EFAULT;
	}
	
	if(data.len <= 0 || data.len >= 0x1000000)
		return -EINVAL;

	if(data.idx < 0)
		return -EINVAL;

	switch(cmd) {
		case 0:
			mem_devp[data.idx].size = 0x5a000000 | (data.len & 0xffffff);
			mem_devp[data.idx].data = kmalloc(data.len, GFP_KERNEL);
			if(!mem_devp[data.idx].data) {
				return -ENOMEM;
			}
			memset(mem_devp[data.idx].data, 0, data.len);
			break;
		default:
			return -EINVAL;
	}

	return 0;
}

static const struct file_operations mem_fops =
{
	.owner = THIS_MODULE,
	.open = mem_open,
	.read = mem_read,
	.write = mem_write,
	.unlocked_ioctl = mem_ioctl,
	.llseek = default_llseek,
	.release = mem_release,
};

static int __init memdev_init(void)
{
	int ret = -1;
	int i;

	if (mem_major) {
		devno = MKDEV(mem_major, mem_minor);
		ret = register_chrdev_region(devno, MEMDEV_NR_DEVS, "memdev");
		if (ret < 0) {
			pr_err("register_chrdev_region failed %d\n", ret);
			goto out;
		}
	} else {
		ret = alloc_chrdev_region(&devno, mem_minor, MEMDEV_NR_DEVS, "memdev");
		if (ret < 0) {
			pr_err("alloc_chrdev_region failed %d\n", ret);
			goto out;
		}
		mem_major = MAJOR(devno);
	}

	cdev_init(&cdev, &mem_fops);    
	cdev.owner = THIS_MODULE;

	ret = cdev_add(&cdev, devno, MEMDEV_NR_DEVS);
	if (ret < 0) {
		pr_err("alloc_chrdev_region failed %d\n", ret);
		goto fail_cdev_add;
	}

	dev_class = class_create(THIS_MODULE, "memdev");
	if(IS_ERR(dev_class)){
		printk("class_create failed!\n");
		ret = PTR_ERR(dev_class);
		goto fail_class_create;
	}

	for(i=0; i<MEMDEV_NR_DEVS; i++) {
		devno = MKDEV(mem_major, i);
		dev_device[i] = device_create(dev_class, NULL, devno, NULL, "memdev%d", i);
		if(IS_ERR(dev_device[i])){
			printk("device_create failed!\n");
			ret = PTR_ERR(dev_device[i]);
			goto fail_device_create;
		}
	}

	mem_devp = kmalloc(MEMDEV_NR_DEVS * sizeof(struct mem_dev), GFP_KERNEL);
	if (!mem_devp) {
		ret =  -ENOMEM;
		goto fail_device_create;
	}

	memset(mem_devp, 0, MEMDEV_NR_DEVS * sizeof(struct mem_dev));

	printk("mem dev driver init!\n");
	return 0;

fail_device_create:
	for(i=0; i<MEMDEV_NR_DEVS; i++) {
		devno = MKDEV(mem_major, i);
		if(dev_device[i])
			device_destroy(dev_class, devno);
	}
	class_destroy(dev_class);
fail_class_create:
	cdev_del(&cdev);
fail_cdev_add:
	unregister_chrdev_region(devno, MEMDEV_NR_DEVS);
out:
	return ret;
}

static void __exit memdev_exit(void)
{
	int i;

	for (i=0; i < MEMDEV_NR_DEVS; i++) {
		if( mem_devp[i].data )
			kfree(mem_devp[i].data);
	}
	kfree(mem_devp);
	for(i=0; i<MEMDEV_NR_DEVS; i++) {
		devno = MKDEV(mem_major, i);
		if(dev_device[i])
			device_destroy(dev_class, devno);
	}
	class_destroy(dev_class);
	cdev_del(&cdev);
	unregister_chrdev_region(devno, MEMDEV_NR_DEVS);
	printk("mem dev driver exit!\n");
}

module_init(memdev_init);
module_exit(memdev_exit);

MODULE_AUTHOR("zjq");
MODULE_LICENSE("GPL");

