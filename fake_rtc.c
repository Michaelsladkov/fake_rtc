#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/rtc.h>
#include <linux/time.h>


#define DEVICE_NAME "FakeRtc"
#define ACCELERATING_COEFFICIENT 2
#define SLOWING_COEFFICIENT 2

int init_module(void);
void cleanup_module(void);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);
static void sync_time();

static int Major;
static int Device_Open = 0;
static dev_t devNo;

static unsigned long synchronized_jiffies;
static struct timeval synchronized_time;

static enum {
    REAL,
    RANDOM,
    ACCELERATED,
    SLOWED
} mode;


static struct file_operations fops = {
    .write = device_write,
};

/**
 * @brief This function is being called id device initialization and on mode switching
 * It's purpose is to store true real time which is used to calculate our fake values 
 */
static void sync_time() {
    do_gettimeofday(&synchronized_time);
    synchronized_jiffies = jiffies;
}

/**
 * @brief Get the accelerated time
 *  
 * @param milliseconds_difference - milliseconds from last synchronization
 * @return time_t - time from January 1st 1970 in accelerated mode 
 */
static time_t get_accelerated_time(unsigned long milliseconds_difference) {
    return synchronized_time.tv_sec + milliseconds_difference * ACCELERATING_COEFFICIENT / 1000;
}

/**
 * @brief Get the slowed time
 * 
 * @param milliseconds_difference - milliseconds from last synchronization
 * @return time_t - time from January 1st 1970 in slowed mode 
 */
static time_t get_slowed_time(unsigned long milliseconds_difference) {
    return synchronized_time.tv_sec + milliseconds_difference / SLOWING_COEFFICIENT / 1000;
}

/**
 * @brief Get the randomized time 
 * 
 * @param milliseconds_difference - milliseconds from last synchronization
 * @return time_t - time from January 1st 1970 in random mode 
 */
static time_t get_randomized_time(unsigned long milliseconds_difference) {
    int8_t random_byte;
    get_random_bytes(&random_byte, 1);
    int8_t coefficient = random_byte % 10; 
    return synchronized_time.tv_sec + milliseconds_difference * coefficient / 1000;
}

/**
 * @brief This function performs Fake RTC device initialization
 * 
 * @return int 0 if init succeed
 */
int init_module(void) {
    struct device * pDev;
    Major = register_chrdev (0, DEVICE_NAME, &fops);
    if (Major < 0) {
        printk(KERN_ALERT "Registering fake rtc device failed\n");
        return Major;
    }
    printk(KERN_INFO "Registering my perfect device succeed with major number %d\n", Major);
    sync_time();
    return 0;
}

/**
 * @brief Cleanup function for Fake RTC
 * 
 */
void cleanup_module(void) {
    unregister_chrdev(Major, DEVICE_NAME);
} 

/**
 * @brief plug function for device writing
 * This device do not support write operation
 * 
 * @param filp 
 * @param buff 
 * @param len 
 * @param off 
 * @return ssize_t 
 */
static ssize_t device_write(struct file *filp, const char *buff, size_t len, loff_t * off) {
    printk(KERN_ALERT "Sorry, this operation isn't supported.\n");
    return -EINVAL;
}
