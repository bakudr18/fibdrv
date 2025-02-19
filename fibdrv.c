#include <linux/bitops.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 92

enum {
    FIB_SEQUENCE = 0,
    FIB_FAST_DOUBLING,
    FIB_FAST_DOUBLING_FLS,
    FIB_FAST_DOUBLING_CLZ
};

typedef long long (*fib_table)(unsigned int);
static unsigned int fib_index = FIB_SEQUENCE;

static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);
struct device *fib_device;
static bool ktime_enable = false;
static ktime_t kt;
static long long (**fib_exec)(unsigned int);
static long long (*fib_time_proxy_p)(unsigned int);
static long long (*fib_impl)(unsigned int);

static long long fib_sequence(unsigned int k)
{
    /* FIXME: C99 variable-length array (VLA) is not allowed in Linux kernel. */
    long long f[k + 2];

    f[0] = 0;
    f[1] = 1;

    for (int i = 2; i <= k; i++) {
        f[i] = f[i - 1] + f[i - 2];
    }

    return f[k];
}

static long long fib_fast_doubling(unsigned int n)
{
    // The position of the highest bit of n.
    // So we need to loop `h` times to get the answer.
    // Example: n = (Dec)50 = (Bin)00110010, then h = 6.
    //                               ^ 6th bit from right side
    unsigned int h = 0;
    for (unsigned int i = n; i; ++h, i >>= 1)
        ;

    long long a = 0;  // F(0) = 0
    long long b = 1;  // F(1) = 1
    // There is only one `1` in the bits of `mask`. The `1`'s position is same
    // as the highest bit of n(mask = 2^(h-1) at first), and it will be shifted
    // right iteratively to do `AND` operation with `n` to check `n_j` is odd or
    // even, where n_j is defined below.
    for (unsigned int mask = 1 << (h - 1); mask; mask >>= 1) {  // Run h times!
        // Let j = h-i (looping from i = 1 to i = h), n_j = floor(n / 2^j) = n
        // >> j (n_j = n when j = 0), k = floor(n_j / 2), then a = F(k), b =
        // F(k+1) now.
        uint64_t c = a * (2 * b - a);  // F(2k) = F(k) * [ 2 * F(k+1) - F(k) ]
        uint64_t d = a * a + b * b;    // F(2k+1) = F(k)^2 + F(k+1)^2

        if (mask & n) {  // n_j is odd: k = (n_j-1)/2 => n_j = 2k + 1
            a = d;       //   F(n_j) = F(2k + 1)
            b = c + d;   //   F(n_j + 1) = F(2k + 2) = F(2k) + F(2k + 1)
        } else {         // n_j is even: k = n_j/2 => n_j = 2k
            a = c;       //   F(n_j) = F(2k)
            b = d;       //   F(n_j + 1) = F(2k + 1)
        }
    }

    return a;
}

static long long fib_fast_doubling_fls(unsigned int n)
{
    long long a = 0;
    long long b = 1;
    for (unsigned int mask = 1 << (fls(n) - 1); mask; mask >>= 1) {
        uint64_t c = a * (2 * b - a);
        uint64_t d = a * a + b * b;

        if (mask & n) {
            a = d;
            b = c + d;
        } else {
            a = c;
            b = d;
        }
    }

    return a;
}

static long long fib_fast_doubling_clz(unsigned int n)
{
    long long a = 0;
    long long b = 1;
    for (unsigned int mask = 1 << (31 - __builtin_clz(n)); mask; mask >>= 1) {
        uint64_t c = a * (2 * b - a);
        uint64_t d = a * a + b * b;

        if (mask & n) {
            a = d;
            b = c + d;
        } else {
            a = c;
            b = d;
        }
    }

    return a;
}

static fib_table fib_method[] = {fib_sequence, fib_fast_doubling,
                                 fib_fast_doubling_fls, fib_fast_doubling_clz};

static inline void fib_impl_set(unsigned int index)
{
    fib_index = index;

    if (fib_index >= ARRAY_SIZE(fib_method))
        fib_index = ARRAY_SIZE(fib_method) - 1;

    fib_impl = fib_method[fib_index];
    printk("fib method: %u\n", fib_index);
}

static long long fib_time_proxy(unsigned int k)
{
    kt = ktime_get();
    long long result = fib_impl(k);
    kt = ktime_sub(ktime_get(), kt);

    return result;
}

static int fib_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&fib_mutex)) {
        printk(KERN_ALERT "fibdrv is in use");
        return -EBUSY;
    }
    return 0;
}

static int fib_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&fib_mutex);
    return 0;
}

/* calculate the fibonacci number at given offset */
static ssize_t fib_read(struct file *file,
                        char *buf,
                        size_t size,
                        loff_t *offset)
{
    return (ssize_t)(*fib_exec)((unsigned) *offset);
}

/* write operation is skipped */
static ssize_t fib_write(struct file *file,
                         const char *buf,
                         size_t size,
                         loff_t *offset)
{
    return ktime_enable ? ktime_to_ns(kt) : 1;
}

static loff_t fib_device_lseek(struct file *file, loff_t offset, int orig)
{
    loff_t new_pos = 0;
    switch (orig) {
    case 0: /* SEEK_SET: */
        new_pos = offset;
        break;
    case 1: /* SEEK_CUR: */
        new_pos = file->f_pos + offset;
        break;
    case 2: /* SEEK_END: */
        new_pos = MAX_LENGTH - offset;
        break;
    }

    if (new_pos > MAX_LENGTH)
        new_pos = MAX_LENGTH;  // max case
    if (new_pos < 0)
        new_pos = 0;        // min case
    file->f_pos = new_pos;  // This is what we'll use now
    return new_pos;
}

static ssize_t ktime_measure_show(struct device *dev,
                                  struct device_attribute *attr,
                                  char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "%d\n", ktime_enable);
}

static ssize_t ktime_measure_store(struct device *dev,
                                   struct device_attribute *attr,
                                   const char *buf,
                                   size_t count)
{
    ktime_enable = (bool) (buf[0] - '0');
    fib_exec = ktime_enable ? &fib_time_proxy_p : &fib_impl;
    return count;
}

static ssize_t fib_method_show(struct device *dev,
                               struct device_attribute *attr,
                               char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "%d\n", fib_index);
}

static ssize_t fib_method_store(struct device *dev,
                                struct device_attribute *attr,
                                const char *buf,
                                size_t count)
{
    fib_impl_set((unsigned int) (buf[0] - '0'));
    return count;
}

static DEVICE_ATTR(ktime_measure,
                   S_IWUSR | S_IRUGO,
                   ktime_measure_show,
                   ktime_measure_store);

static DEVICE_ATTR(fib_method,
                   S_IWUSR | S_IRUGO,
                   fib_method_show,
                   fib_method_store);


static struct attribute *fib_attrs[] = {&dev_attr_ktime_measure.attr,
                                        &dev_attr_fib_method.attr, NULL};

static struct attribute_group fib_group = {
    .attrs = fib_attrs,
};

const struct file_operations fib_fops = {
    .owner = THIS_MODULE,
    .read = fib_read,
    .write = fib_write,
    .open = fib_open,
    .release = fib_release,
    .llseek = fib_device_lseek,
};

static int __init init_fib_dev(void)
{
    int rc = 0;

    mutex_init(&fib_mutex);

    // Let's register the device
    // This will dynamically allocate the major number
    rc = alloc_chrdev_region(&fib_dev, 0, 1, DEV_FIBONACCI_NAME);

    if (rc < 0) {
        printk(KERN_ALERT
               "Failed to register the fibonacci char device. rc = %i",
               rc);
        return rc;
    }

    fib_cdev = cdev_alloc();
    if (fib_cdev == NULL) {
        printk(KERN_ALERT "Failed to alloc cdev");
        rc = -1;
        goto failed_cdev;
    }
    fib_cdev->ops = &fib_fops;
    rc = cdev_add(fib_cdev, fib_dev, 1);

    if (rc < 0) {
        printk(KERN_ALERT "Failed to add cdev");
        rc = -2;
        goto failed_cdev;
    }

    fib_class = class_create(THIS_MODULE, DEV_FIBONACCI_NAME);

    if (!fib_class) {
        printk(KERN_ALERT "Failed to create device class");
        rc = -3;
        goto failed_class_create;
    }

    fib_device =
        device_create(fib_class, NULL, fib_dev, NULL, DEV_FIBONACCI_NAME);
    if (!fib_device) {
        printk(KERN_ALERT "Failed to create device");
        rc = -4;
        goto failed_device_create;
    }

    if (sysfs_create_group(&fib_device->kobj, &fib_group)) {
        rc = -5;
        goto failed_create_group;
    }

    fib_impl_set(FIB_SEQUENCE);
    fib_time_proxy_p = &fib_time_proxy;
    fib_exec = &fib_impl;

    return rc;
failed_create_group:
    device_destroy(fib_class, fib_dev);
failed_device_create:
    class_destroy(fib_class);
failed_class_create:
    cdev_del(fib_cdev);
failed_cdev:
    unregister_chrdev_region(fib_dev, 1);
    return rc;
}

static void __exit exit_fib_dev(void)
{
    mutex_destroy(&fib_mutex);
    sysfs_remove_group(&fib_device->kobj, &fib_group);
    device_destroy(fib_class, fib_dev);
    class_destroy(fib_class);
    cdev_del(fib_cdev);
    unregister_chrdev_region(fib_dev, 1);
}

module_init(init_fib_dev);
module_exit(exit_fib_dev);
