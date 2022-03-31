#! /bin/bash
insmod $1
PROC_ENTRY="/proc/FakeRTC"
DEVICE_NAME=$(dmesg | grep FakeRTC | tail --lines=1 | egrep -o 'rtc[[:digit:]]+')
echo "RTC device registered as $DEVICE_NAME"
DEVICE_FILE="/dev/$DEVICE_NAME"
echo "Device file: $DEVICE_FILE"
echo ""
sleep 2

echo "Let's read time (module is in real mode by default)"
hwclock -f $DEVICE_FILE
echo "Compare it with system time: "
date
echo ""
sleep 2

echo "Let's look for /proc output of our module (/proc entry is $PROC_ENTRY)"
cat $PROC_ENTRY
sleep 5
echo ""
echo "Now we will switch to random mode and check /proc output again"
sleep 2
echo "1" > $PROC_ENTRY
cat $PROC_ENTRY
echo ""
sleep 5

echo "To be sure it is realy random, let's read time 5 times"
for i in {1..5}
do
    hwclock -f $DEVICE_FILE
    sleep 2
done
echo ""
sleep 3

echo "And now let's go back to real mode and compare again with system time"
echo "0" > $PROC_ENTRY
echo "Check mode via /proc"
cat $PROC_ENTRY
echo ""
sleep 3
echo "Our RTC time:"
hwclock -f $DEVICE_FILE
echo "System time:"
date
echo ""
sleep 3

echo "Let's set time now to 10:42"
hwclock -f $DEVICE_FILE --set --date="10:42"
sleep 1
echo "Now go to accelerated mode and check"
echo "2" > $PROC_ENTRY
hwclock -f $DEVICE_FILE
echo "And wait for 5 sec"
sleep 5
hwclock -f $DEVICE_FILE
echo "Our RTC says it was 10 sec!"
echo ""
sleep 3

echo "Now let's try slowed mode"
echo "3" > $PROC_ENTRY
cat $PROC_ENTRY
echo ""
sleep 1
hwclock -f $DEVICE_FILE
echo "And wait for 5 seconds"
sleep 5
hwclock -f $DEVICE_FILE
echo "Our RTC says it was only 1!"
echo ""
sleep 3

echo "That's all from demo. I wil leave module installed so you can play with it. To remove moule run: "
echo "sudo bash test/cleanup.sh $DEVICE_FILE"
