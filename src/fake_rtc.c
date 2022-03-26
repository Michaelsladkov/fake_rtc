#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/random.h>
#include <linux/rtc.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define DEVICE_NAME "FakeRTC"
#define ACCELERATING_COEFFICIENT 2
#define SLOWING_COEFFICIENT 2

#define PREFERABLE_MAJOR 0

static enum {
    REAL,
    RANDOM,
    ACCELERATED,
    SLOWED
} mode = REAL;

int major = PREFERABLE_MAJOR;
int minor = 1;
module_param(minor, int, S_IRUGO);

static int device_open = 0;

static struct fake_rtc_info {
    struct rtc_device *rtc_dev;
	struct platform_device *pdev;
} fake_rtc;

static ktime_t synchronized_real_time;
static ktime_t synchronized_boot_time;

static void synchronize_boot_time(void);
static void synchronize_real_time(void);

static void synchronize_boot_time(void) {
    synchronized_boot_time = ktime_get();
}

static void synchronize_real_time(void) {
    synchronized_real_time = ktime_get_real();
}

/**
 * @brief Get the accelerated time
 *  
 * @param nanoseconds_difference - nanoseconds from last synchronization
 * @return ktime_t - time from January 1st 1970 in accelerated mode 
 */
static ktime_t get_accelerated_time(unsigned long nanoseconds_difference) {
    return (ktime_t) {
        synchronized_real_time + nanoseconds_difference * ACCELERATING_COEFFICIENT
    };
}

/**
 * @brief Get the slowed time
 * 
 * @param nanoseconds_difference - nanoseconds from last synchronization
 * @return time_t - time from January 1st 1970 in slowed mode 
 */
static ktime_t get_slowed_time(unsigned long nanoseconds_difference) {
    return (ktime_t) {
        synchronized_real_time + nanoseconds_difference / SLOWING_COEFFICIENT
    };
}

/**
 * @brief Get the randomized time 
 * 
 * @param nanoseconds_difference - nanoseconds from last synchronization
 * @return time_t - time from January 1st 1970 in random mode 
 */
static ktime_t get_randomized_time(unsigned long nanoseconds_difference) {
    int8_t random_byte;
    get_random_bytes(&random_byte, 1);
    int8_t coefficient = random_byte % 10; 
    return (ktime_t) {
            synchronized_real_time + nanoseconds_difference * coefficient
        };
}

static ktime_t get_real_time(unsigned long ignored) {
    return ktime_get_real();
}

static ktime_t (*fake_rtc_accessors[])(unsigned long) = {
    [REAL] = get_real_time,
    [RANDOM] = get_randomized_time,
    [ACCELERATED] = get_accelerated_time,
    [SLOWED] = get_slowed_time
};

static int fake_rtc_read_time(struct device * dev, struct rtc_time * tm) {
    unsigned long nanosec_from_sync = ktime_get() - synchronized_boot_time;
    ktime_t my_time = fake_rtc_accessors[mode](nanosec_from_sync);
    rtc_time64_to_tm(my_time, tm);
    return 0;
}

static int fake_rtc_set_time(struct device * dev, struct rtc_time * tm) {
    synchronized_real_time = rtc_tm_to_ktime(*tm);
    synchronize_boot_time();
    return 0;
}

static const struct rtc_class_ops fake_rtc_operations = {
    .read_time = fake_rtc_read_time,
    .set_time = fake_rtc_set_time
};

void fake_rtc_cleanup(void) {
    printk(KERN_ALERT "Boot time: %lls\n", synchronized_boot_time);
}

static int fake_rtc_open(struct inode * inode, struct file * file) {
    if (device_open) {
        return EBUSY;
    }
    device_open++;
    try_module_get(THIS_MODULE);
    return 0;
}

static int fake_rtc_release(struct inode * inode, struct file * file) {
    device_open--;
    module_put(THIS_MODULE);
    return 0;
}

static struct file_operations fops = {
    .open = fake_rtc_open,
    .release = fake_rtc_release
};

int fake_rtc_init(void) {
    printk(KERN_CRIT "We are initing, huys\n");
    dev_t device = 0;
    int result;

    major = register_chrdev(PREFERABLE_MAJOR, DEVICE_NAME, &fops);
    if (major < 0) {
        printk(KERN_WARNING "Fake rtc: can't get major %d\n", major);
        return major;
    }
    printk(KERN_ALERT "FakeRTC major: %d\n", major);
    fake_rtc.pdev = platform_device_alloc(DEVICE_NAME, 2);
    printk(KERN_CRIT "We have allocated pdev\n");
    fake_rtc.rtc_dev = devm_rtc_allocate_device(&(fake_rtc.pdev->dev));
    printk(KERN_CRIT "We have allocated rtc\n");
    fake_rtc.rtc_dev->ops = &fake_rtc_operations;
    fake_rtc.rtc_dev->dev = fake_rtc.pdev->dev;

    synchronize_boot_time();
    synchronize_real_time();
    
    return __rtc_register_device(THIS_MODULE, fake_rtc.rtc_dev);
}

module_init(fake_rtc_init);
module_exit(fake_rtc_cleanup);

MODULE_AUTHOR("Mikhail Sladkov <msladkov2002@gmail.com>");
MODULE_DESCRIPTION("RTC driver with different faking modes");
MODULE_LICENSE("GPL");
