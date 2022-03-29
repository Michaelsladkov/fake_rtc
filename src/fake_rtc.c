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

static struct fake_rtc_info {
    struct rtc_device *rtc_dev;
    struct platform_device *pdev;
    struct proc_dir_entry *proc_entry;
} fake_rtc;

static ktime_t synchronized_real_time;
static ktime_t synchronized_boot_time;
static int device_proc_open = 0;

static char proc_msg[PROC_MSG_LEN] = {0};
static char* proc_msg_ptr = proc_msg;

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
    /* We need this counter because of the way hwclopck util works.
     * It won't return any result until seconds on clock will change.
     * To make it work we will add a second dor odd call and we won't for even call.
     * So without this counter hwclock will throw a timeout error
    */
    static int call_counter; 
    call_counter++;
    return (ktime_t) {
        synchronized_real_time + nanoseconds_difference / SLOWING_COEFFICIENT + (call_counter % 2) * NANOSECONDS_IN_SECOND
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
    int8_t coefficient; 
    get_random_bytes(&random_byte, 1);
    coefficient = random_byte % 10;
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
    synchronized_real_time = rtc_tm_to_ktime(*tm);
    synchronize_boot_time();
    return 0;
}

static const struct rtc_class_ops fake_rtc_operations = {
    .read_time = fake_rtc_read_time,
    .set_time = fake_rtc_set_time
};

void fake_rtc_cleanup(void) {
    platform_device_del(fake_rtc.pdev);
    proc_remove(fake_rtc.proc_entry);
}

static int fake_rtc_proc_open(struct inode * inode, struct file * file) {
    if (device_proc_open) {
        return -EBUSY;
    }
    device_proc_open++;
    sprintf(proc_msg, "Operating modes of this device:\n"\
    "\t0 - Real time\n"\
    "\t1 - Random time\n"\
    "\t2 - Accelerated time\n"\
    "\t3 - Slowed time\n"\
    "Current operating mode: %d\n"\
    "Write mode number to this file to change operating mode\n", mode);
    proc_msg_ptr = proc_msg;
    try_module_get(THIS_MODULE);
    return 0;
}

static int fake_rtc_proc_release(struct inode * inode, struct file * file) {
    device_proc_open--;
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

static ssize_t fake_rtc_proc_write(struct file *filp, const char *buff, size_t len, loff_t * off) {
    static char mode_char;
    if (len == 0 || *off > 0) {
        return len;
    }
    get_user(mode_char, buff);
    if (mode_char < '0' || mode_char > '3') {
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

int fake_rtc_init(void) {
    fake_rtc.pdev = platform_device_register_simple(DEVICE_NAME, -1, NULL, 0);
    fake_rtc.rtc_dev = devm_rtc_device_register(&(fake_rtc.pdev->dev), DEVICE_NAME, &fake_rtc_operations, THIS_MODULE);

    fake_rtc.proc_entry = proc_create("FakeRTC", 0666, NULL, &fake_rtc_proc_ops);

    synchronize_boot_time();
    synchronize_real_time();

    return 0;
}

module_init(fake_rtc_init);
module_exit(fake_rtc_cleanup);

MODULE_AUTHOR("Mikhail Sladkov <msladkov2002@gmail.com>");
MODULE_DESCRIPTION("RTC driver with different faking modes");
MODULE_LICENSE("GPL");
