#include <linux/kernel.h>
#include <linux/rtc.h>
#include <linux/module.h>

struct fake_rtc_info {
    struct rtc_device *rtc_dev;
	struct device *dev;
};