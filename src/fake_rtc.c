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
#define NANOSECONDS_IN_SECOND 1000000000

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

static ktime_t get_real_time(unsigned long nanoseconds_difference) {
    return synchronized_real_time + nanoseconds_difference;
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
    rtc_time64_to_tm(my_time / NANOSECONDS_IN_SECOND, tm);
    return 0;
}

static int fake_rtc_set_time(struct device * dev, struct rtc_time * tm) {
    printk(KERN_ALERT "Fake RTC set time accessed\n");
    printk(KERN_ALERT "Was %lld\n", synchronized_real_time);
    synchronized_real_time = rtc_tm_to_ktime(*tm);
    printk(KERN_ALERT "Now %lld\n", synchronized_real_time);
    synchronize_boot_time();
    return 0;
}

static const struct rtc_class_ops fake_rtc_operations = {
    .open = fake_rtc_open,
    .release = fake_rtc_release,
    .read_time = fake_rtc_read_time,
    .set_time = fake_rtc_set_time
};

static int fake_rtc_open(struct device* dev) {
    if (device_open) {
        return EBUSY;
    }
    device_open++;
    try_module_get(THIS_MODULE);
    return 0;
}

static int fake_rtc_release(struct device* dev) {
    device_open--;
    module_put(THIS_MODULE);
    return 0;
}

void fake_rtc_cleanup(void) {
    platform_device_del(fake_rtc.pdev);
}

int fake_rtc_init(void) {
    fake_rtc.pdev = platform_device_register_simple(DEVICE_NAME, -1, NULL, 0);
    fake_rtc.rtc_dev = devm_rtc_device_register(&(fake_rtc.pdev->dev), "RTCmegaDevice", &fake_rtc_operations, THIS_MODULE);

    synchronize_boot_time();
    synchronize_real_time();

    return 0;
}

module_init(fake_rtc_init);
module_exit(fake_rtc_cleanup);

MODULE_AUTHOR("Mikhail Sladkov <msladkov2002@gmail.com>");
MODULE_DESCRIPTION("RTC driver with different faking modes");
MODULE_LICENSE("GPL");
