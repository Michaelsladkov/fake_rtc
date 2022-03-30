#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/random.h>
#include <linux/rtc.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>

/**
 * Feel free to change this contants to change accelerating and slowing behavior
 * But keep it natural numbers
 */
#define ACCELERATING_COEFFICIENT 2
#define SLOWING_COEFFICIENT 5

#define DEVICE_NAME "FakeRTC"
#define NANOSECONDS_IN_SECOND 1000000000
#define PROC_MSG_LEN 1024

/**
 * @brief Enum of operating modes for this module
 * 
 * Real - for real time, corresponding to system time
 * Random - for randomized time from last sychronization
 * Accelerated - time goes faster than real. How much faster - defined by ACCELERATING_COEFFICIENT
 * Slowed - time goes slower than real. How much slower - defined by SLOWING_COEFFICIENT
 */
static enum {
    REAL,
    RANDOM,
    ACCELERATED,
    SLOWED
} mode = REAL;

/**
 * @brief Struct to represent this device
 * 
 * @rtc_dev - rtc device registered in kernel
 * @pdev - registeredd platform device used to register rtc device
 * @proc_entry - entry to /proc dir corresponding to this module
 * @synchronized_real_time - time is nanoseconds used as starting point in time measurement. Synchronization takes place in init
 * @synchronized_boot_time - time in nanoseconds used to calculate time difference between measurement and synchronization which takes place in init and time set
 * @device_proc_open - used as variable for /proc file state (opened/closed) to forbid parallel access
 */
static struct fake_rtc_info {
    struct rtc_device *rtc_dev;
    struct platform_device *pdev;
    struct proc_dir_entry *proc_entry;
    ktime_t synchronized_real_time;
    ktime_t synchronized_boot_time;
    int8_t device_proc_open;
    uint64_t read_counter;
    uint64_t set_counter;
} fake_rtc;

/**
 * @brief Buffer for mesage displayed when /proc file is read
 * 
 */
static char proc_msg[PROC_MSG_LEN] = {0};
static char* proc_msg_ptr = proc_msg;

static void synchronize_boot_time(void) {
    fake_rtc.synchronized_boot_time = ktime_get();
}

static void synchronize_real_time(void) {
    fake_rtc.synchronized_real_time = ktime_get_real();
}

/**
 * @brief Get the accelerated time
 *  
 * @param nanoseconds_difference - nanoseconds from last synchronization
 * @return ktime_t - time from January 1st 1970 in accelerated mode 
 */
static ktime_t get_accelerated_time(unsigned long nanoseconds_difference) {
    return (ktime_t) {
        fake_rtc.synchronized_real_time + nanoseconds_difference * ACCELERATING_COEFFICIENT
    };
}

/**
 * @brief Get the slowed time
 * 
 * @param nanoseconds_difference - nanoseconds from last synchronization
 * @return time_t - time from January 1st 1970 in slowed mode 
 */
static ktime_t get_slowed_time(unsigned long nanoseconds_difference) {
    /* We need this counter because of the way hwclopck util works.
     * It won't return any result until seconds on clock will change.
     * To make it work we will add a second dor odd call and we won't for even call.
     * So without this counter hwclock will throw a timeout error
    */
    static int call_counter;
    call_counter++;
    return (ktime_t) {
        fake_rtc.synchronized_real_time + nanoseconds_difference / SLOWING_COEFFICIENT + (call_counter % 2) * NANOSECONDS_IN_SECOND
    };
}

/**
 * @brief Get the randomized time 
 * 
 * @param nanoseconds_difference - nanoseconds from last synchronization
 * @return time_t - time from January 1st 1970 in random mode 
 */
static ktime_t get_randomized_time(unsigned long nanoseconds_difference) {
    static int call_counter;
    int8_t random_byte;
    int8_t coefficient;
    call_counter++;
    get_random_bytes(&random_byte, 1);
    coefficient = random_byte % 10;
    return (ktime_t) {
            fake_rtc.synchronized_real_time + nanoseconds_difference * coefficient + (call_counter % 2) * NANOSECONDS_IN_SECOND
    };
}

static ktime_t get_real_time(unsigned long nanoseconds_difference) {
    return fake_rtc.synchronized_real_time + nanoseconds_difference;
}

/**
 * @brief Array of function pointers used to access calculating function corresponding to mode
 * 
 */
static ktime_t (*fake_rtc_accessors[])(unsigned long) = {
    [REAL] = get_real_time,
    [RANDOM] = get_randomized_time,
    [ACCELERATED] = get_accelerated_time,
    [SLOWED] = get_slowed_time
};

/**
 * @brief read time function, part of rtc interface
 * 
 * This function calculates nanoseconds spent from last synchronization and use it to get time value based on mode
 * Because calculating fuction returns nanoseconds from January 1st 1970, this function converts it to rtc_time
 * 
 * @param dev 
 * @param tm 
 * @return int - status
 */
static int fake_rtc_read_time(struct device * dev, struct rtc_time * tm) {
    unsigned long nanosec_from_sync = ktime_get() - fake_rtc.synchronized_boot_time;
    ktime_t my_time = fake_rtc_accessors[mode](nanosec_from_sync);
    rtc_time64_to_tm(my_time / NANOSECONDS_IN_SECOND, tm);
    fake_rtc.read_counter++;
    return 0;
}

/**
 * @brief set time function, part of rtc interface
 * 
 * @param dev 
 * @param tm 
 * @return int - status
 */
static int fake_rtc_set_time(struct device * dev, struct rtc_time * tm) {
    fake_rtc.synchronized_real_time = rtc_tm_to_ktime(*tm);
    synchronize_boot_time();
    fake_rtc.set_counter++;
    return 0;
}

static const struct rtc_class_ops fake_rtc_operations = {
    .read_time = fake_rtc_read_time,
    .set_time = fake_rtc_set_time
};

/**
 * @brief open function for /proc interface
 * 
 * This function checks if someone has already opened device and if not it prepares message for user and occupies device
 * 
 * @param inode 
 * @param file 
 * @return int status
 */
static int fake_rtc_proc_open(struct inode * inode, struct file * file) {
    if (fake_rtc.device_proc_open) {
        return -EBUSY;
    }
    fake_rtc.device_proc_open++;
    sprintf(proc_msg, "Time has been set %llu times and read %llu times\n"\
    "Operating modes of this device:\n"\
    "\t0 - Real time\n"\
    "\t1 - Random time\n"\
    "\t2 - Accelerated time\n"\
    "\t3 - Slowed time\n"\
    "Current operating mode: %d\n"\
    "Write mode number to this file to change operating mode\n",\
        fake_rtc.set_counter, fake_rtc.read_counter, mode);
    proc_msg_ptr = proc_msg;
    try_module_get(THIS_MODULE);
    return 0;
}

static int fake_rtc_proc_release(struct inode * inode, struct file * file) {
    fake_rtc.device_proc_open--;
    module_put(THIS_MODULE);
    return 0;
}

static ssize_t fake_rtc_proc_read(struct file * filp, char * buffer, size_t length, loff_t * offset) {
    ssize_t bytes_read = 0;
    if (offset != NULL) {
        proc_msg_ptr += *offset;
    }
    if (proc_msg_ptr - proc_msg > PROC_MSG_LEN) {
        return 0;
    }
    while (*proc_msg_ptr && length--) {
        put_user(*(proc_msg_ptr++), buffer++);
        bytes_read++;
    }
    return bytes_read;
}

/**
 * @brief write function for /proc interface
 * 
 * It consumes 1 char from user input. It should be a digit from 0 to 3 to change mode
 * Otherways this function does nothing
 * 
 * @param filp 
 * @param buff 
 * @param len 
 * @param off 
 * @return ssize_t 
 */
static ssize_t fake_rtc_proc_write(struct file *filp, const char *buff, size_t len, loff_t * off) {
    static char mode_char;
    if (len == 0 || *off > 0) {
        dev_warn(&(fake_rtc.pdev->dev), "This module expects just one digit without offset in proc inputs");
        return len;
    }
    get_user(mode_char, buff);
    if (mode_char < '0' || mode_char > '3') {
        dev_warn(&(fake_rtc.pdev->dev), "This module expects first character of proc input to be digit from 0 to 3");
        return len;
    }
    mode = mode_char - '0';
    return len;
}


static struct file_operations fake_rtc_proc_ops = {
    .open = fake_rtc_proc_open,
    .release = fake_rtc_proc_release,
    .read = fake_rtc_proc_read,
    .write = fake_rtc_proc_write
};

/**
 * @brief cleanup routine
 * 
 * On module detach we need to free all allocated resources and /proc entry 
 */
void fake_rtc_cleanup(void) {
    platform_device_del(fake_rtc.pdev);
    proc_remove(fake_rtc.proc_entry);
}

/**
 * @brief initialisation routine
 * 
 * Platform device and rtc device are being registered here. 
 * Also this function creates /proc entry and synchronizes time
 * 
 * @return int - status
 */
int fake_rtc_init(void) {
    struct device* associated_device;
    fake_rtc.pdev = platform_device_register_simple(DEVICE_NAME, -1, NULL, 0);
    associated_device = &(fake_rtc.pdev->dev);
    fake_rtc.rtc_dev = devm_rtc_device_register(associated_device, DEVICE_NAME, &fake_rtc_operations, THIS_MODULE);

    fake_rtc.proc_entry = proc_create("FakeRTC", 0666, NULL, &fake_rtc_proc_ops);
    if (fake_rtc.proc_entry == NULL) {
        dev_err(associated_device, "Proc entry creation failed");
    }
    fake_rtc.device_proc_open = 0;

    fake_rtc.read_counter = 0;
    fake_rtc.set_counter = 0;

    synchronize_boot_time();
    synchronize_real_time();

    return 0;
}

module_init(fake_rtc_init);
module_exit(fake_rtc_cleanup);

MODULE_AUTHOR("Mikhail Sladkov <msladkov2002@gmail.com>");
MODULE_DESCRIPTION("RTC driver with different faking modes");
MODULE_LICENSE("GPL");
