#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/random.h>
#include <linux/rtc.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

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

static struct fake_rtc_info {
    struct rtc_device *rtc_dev;
	struct device *dev;
} this_device;

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
 * @param milliseconds_difference - milliseconds from last synchronization
 * @return ktime_t - time from January 1st 1970 in accelerated mode 
 */
static ktime_t get_accelerated_time(unsigned long milliseconds_difference) {
    return (ktime_t) {
        synchronized_real_time + milliseconds_difference * ACCELERATING_COEFFICIENT / 1000
    };
}

/**
 * @brief Get the slowed time
 * 
 * @param milliseconds_difference - milliseconds from last synchronization
 * @return time_t - time from January 1st 1970 in slowed mode 
 */
static ktime_t get_slowed_time(unsigned long milliseconds_difference) {
    return (ktime_t) {
        synchronized_real_time + milliseconds_difference / SLOWING_COEFFICIENT / 1000
    };
}

/**
 * @brief Get the randomized time 
 * 
 * @param milliseconds_difference - milliseconds from last synchronization
 * @return time_t - time from January 1st 1970 in random mode 
 */
static ktime_t get_randomized_time(unsigned long milliseconds_difference) {
    int8_t random_byte;
    get_random_bytes(&random_byte, 1);
    int8_t coefficient = random_byte % 10; 
    return (ktime_t) {
            synchronized_real_time + milliseconds_difference * coefficient / 1000
        };
}

static int fake_rtc_read_time(struct device * dev, struct rtc_time * tm) {
    return 0;
}

static int fake_rtc_set_time(struct device * dev, struct rtc_time * tm) {
    return 0;
}

static const struct rtc_class_ops fake_rtc_operations = {
    .read_time = fake_rtc_read_time,
    .set_time = fake_rtc_set_time
};

void fake_rtc_cleanup(void) {

}

int fake_rtc_init(void) {
    dev_t device = 0;
    int result;
    if (PREFERABLE_MAJOR) {
		device = MKDEV(PREFERABLE_MAJOR, minor);
	} else {
		major = MAJOR(device);
	}
	if (result < 0) {
		printk(KERN_WARNING "Fake rtc: can't get major %d\n", major);
		return result;
	}
    synchronize_boot_time();
    synchronize_real_time();
    printk(KERN_ALERT "pizdec\n");
    return 0;
}

module_init(fake_rtc_init);
module_exit(fake_rtc_cleanup);

MODULE_AUTHOR("Mikhail Sladkov <msladkov2002@gmail.com>");
MODULE_DESCRIPTION("RTC driver with different faking modes");
MODULE_LICENSE("GPL");
