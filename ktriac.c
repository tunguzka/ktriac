/*********************************************
*** Linux kernel module to drive TRIAC with
*** Raspberry Pi
***
*** Written by The TunguZka Team Hungary
*** GNU GPLv3 license
*********************************************/


#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/ktime.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/hrtimer.h>
#include "ktriac.h"

#ifdef DEBUG_DEVICE
    #define UPDATED_TIME 1
    #define UPDATED_DUTY 2
    #define UPDATED_NOT_HANDLED_TIME 3

    static wait_queue_head_t waitqueue;
    static int updated;
#endif


#define SEC_IN_US                       1000000

#define OFF     0
#define ON      1

static int percent_to_angle_table[] = { -1, -1, -1, -1, 156, 154, 151, 149, 147, 145, 143, 141, 139, 137, 136, 134, 132, 131, 129, 128, 126, 125, 124, 122, 121, 120, 118, 117, 116, 114, 113, 112, 111, 109, 108, 107, 106, 105, 103, 102, 101, 100, 99, 98, 96, 95, 94, 93, 92, 91, 90, 88, 87, 86, 85, 84, 83, 81, 80, 79, 78, 77, 76, 74, 73, 72, 71, 70, 68, 67, 66, 65, 63, 62, 61, 60, 58, 57, 55, 54, 53, 51, 50, 48, 47, 45, 43, 42, 40, 38, 36, 34, 32, 30, 28, 25, 23, 19, 16, 11, 0 };

static int angle_to_percent_table[] = { 100, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 98, 98, 98, 98, 98, 97, 97, 97, 96, 96, 96, 96, 95, 95, 94, 94, 94, 93, 93, 92, 92, 91, 91, 90, 90, 89, 89, 88, 88, 87, 87, 86, 85, 85, 84, 84, 83, 82, 82, 81, 80, 80, 79, 78, 77, 77, 76, 75, 75, 74, 73, 72, 71, 71, 70, 69, 68, 67, 67, 66, 65, 64, 63, 62, 62, 61, 60, 59, 58, 57, 56, 56, 55, 54, 53, 52, 51, 50, 50, 49, 48, 47, 46, 45, 44, 43, 43, 42, 41, 40, 39, 38, 37, 37, 36, 35, 34, 33, 32, 32, 31, 30, 29, 28, 28, 27, 26, 25, 25, 24, 23, 22, 22, 21, 20, 19, 19, 18, 17, 17, 16, 15, 15, 14, 14, 13, 12, 12, 11, 11, 10, 10, 9, 9, 8, 8, 7, 7, 6, 6, 5, 5, 5, 4, 4, 3, 3, 3, 3, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0};

static struct hrtimer hr_timer;
static int triacAngle;
static unsigned int triacStatus;
static unsigned int ac_freq;
static unsigned int freqTimeLowerBound, freqTimeUpperBound;
static unsigned int counter;
static unsigned int mark, space;
static unsigned int duty;

//AC freq tolerance in %: great if your zerocrossing circuit noisy is.
static int tolerance;

static ktime_t lastRising, lastFalling, triacTriggerDelay, triacFireTime, nextFire;
static s64 delta_us, delta;
static s64 zeroCrossLatency, halfPhase;

/* Define GPIOs & Irq */
static struct gpio pins[] = {
                { GPIO_ACFREQ, GPIOF_IN, "AC Signal" },
                { GPIO_TRIAC, GPIOF_OUT_INIT_LOW, "TRIAC trigger" }
};

static int ac_irqs[] = { -1 };


inline bool is_triac_on(void)
{
    return ( triacStatus);
}

inline void triac(unsigned int value)
{
    gpio_set_value( GPIO_TRIAC, value);
    triacStatus = value;
}

inline unsigned int calc_freq(unsigned int us)
{
    unsigned int v, r;
    v = SEC_IN_US / (us * 2);
    r = SEC_IN_US % (us * 2);
    
    return ( r >= us) ? ++v : v;
}


/*
 * The interrupt service routine called on zerocrossing pin event
 */
static irqreturn_t zerocross_trigger_isr(int irq, void *data)
{
        ktime_t now = ktime_get();
        delta = ktime_us_delta( now, lastRising);
        
        
        //don't handle events < 300us
        if ( delta < 300 || delta < freqTimeLowerBound)
        {
#ifdef DEBUG_DEVICE        
            updated = UPDATED_NOT_HANDLED_TIME;        
            wake_up(&waitqueue);
#endif        
            return IRQ_HANDLED;
        }

        if ( delta > freqTimeUpperBound)
        {
            if ( delta > SEC_IN_US)
                printk(KERN_INFO "ktriac: irq out of freq: %d Hz delta: %lld us calc_freq: %d Hz\n", ac_freq, delta, calc_freq( delta));
        }

        lastRising = now;
        
        //pwm mode
        if ( mark)
        {
            ++counter;
            if ( counter <= mark)
            {
                nextFire = ktime_add_us( now, zeroCrossLatency);
                
                if ( !hrtimer_active( &hr_timer))                    
                    hrtimer_start(&hr_timer, nextFire, HRTIMER_MODE_ABS);
            }
            
            if ( counter >= space + mark)
                counter = 0;
            
        }
        else
        if ( triacAngle > 0)
        {
            nextFire = ktime_add_us( ktime_add( now, triacTriggerDelay), zeroCrossLatency);
//            printk(KERN_INFO "ktriac: timer to: %lld s\n", nextFire);
            
            if ( !hrtimer_active( &hr_timer))                
                hrtimer_start(&hr_timer, nextFire, HRTIMER_MODE_ABS);
        }
        else
        if ( triacAngle < 0 && is_triac_on())
        {
            triac( OFF);
        }
        else
        if ( triacAngle == 0 && !is_triac_on())
            triac( ON);
        
        delta_us = delta;

#ifdef DEBUG_DEVICE        
        updated = UPDATED_TIME;        
        wake_up(&waitqueue);
#endif
                
        return IRQ_HANDLED;
}

static enum hrtimer_restart triac_fire( struct hrtimer *timer)
{
    ktime_t now =  ktime_get();
    
    
    if ( !is_triac_on())
    {
//        printk(KERN_INFO "\t\t triac ON: %lld\t\tlatency: %lldus\n", now, ktime_us_delta( now, timer->_softexpires));
        
        hrtimer_forward( timer, timer->_softexpires, triacFireTime);
        triac( ON);
        
        return HRTIMER_RESTART;
    }
    
    //now = ktime_get();
    triac( OFF);
//    printk(KERN_INFO "\t\t triac OFF: %lld\n", now);
    
    if ( ktime_after( nextFire, now))
    {
        hrtimer_forward( timer, now, ktime_sub( nextFire, now));
//        printk(KERN_INFO "\t\t resheduling timer: %lld\n", nextFire);
        return HRTIMER_RESTART;
    }

    return HRTIMER_NORESTART;
}
 
static void set_triac_attack_angle( int angle_deg)
{
    mark = space = 0;
    
    if ( angle_deg > 180 || angle_deg < 0 || ac_freq == 0)
    {
        triacAngle = -1;
        duty = 0;
        
        goto update;
        return;
    } 
    else
    {
        unsigned int half_phase_duration = SEC_IN_US / ( ac_freq * 2);
        unsigned int delay = half_phase_duration * angle_deg * 1000 / 180;
        triacTriggerDelay = ktime_set( 0, delay);
    }
    
    duty = angle_to_percent_table[ angle_deg];
    triacAngle = angle_deg;    

update:
#ifdef DEBUG_DEVICE
        updated = UPDATED_DUTY;
        wake_up(&waitqueue);
#endif
        return;
}

static void set_triac_pwm( int m, int s)
{
    triacAngle = -1;
    
    if ( m <= 0 && s <= 0)
        mark = space = 0;
    
    mark = m;
    space = s;
    counter = 0;  
    
    duty = ( mark) ? mark * 100 / ( mark + space) : 0;

#ifdef DEBUG_DEVICE
    updated = UPDATED_DUTY;
    wake_up(&waitqueue);
#endif
}

static void set_ac_frequent( int freq)
{
    unsigned int duration;

    if ( freq <= 0)
        return;
    
    ac_freq = freq;
    
    //half period duration
    duration = SEC_IN_US / 2 / ac_freq;
    
    freqTimeLowerBound = ( duration * ( 100 - tolerance)) / 100;
    freqTimeUpperBound = ( duration * ( 100 + tolerance)) / 100;
    
    halfPhase = duration;    
    printk(KERN_INFO "ktriac: setting ac_freq: %d Hz freqTimeLowerBound: %d us freqTimeUpperBound: %d s\n", ac_freq,freqTimeLowerBound, freqTimeUpperBound);
}

static void set_zerocross_latency( int value)
{
    if ( value < 0)
        zeroCrossLatency = halfPhase + value;
    else
        zeroCrossLatency = value;
}

// SYSFS

inline const char* mains_status_str( void)
{
    unsigned int mains = ( ktime_us_delta( ktime_get(), lastRising) < freqTimeUpperBound * 2);
    
    return ( mains) ? "on" : "off";
}

static ssize_t triac_show(struct kobject *kobj, struct kobj_attribute *attr,
                      char *buf)
{
    int count = 0;
    
    count += sprintf( buf + count, "Mains: %s\nAC freq: %d\nTolerance: %d\n", mains_status_str(), ac_freq, tolerance);
    
    if ( mark)
        count += sprintf( buf + count, "PWM: %d:%d\n", mark, space);
    else
        count += sprintf( buf + count, "Angle: %d deg\n", triacAngle);
    
    count += sprintf( buf + count, "Duty: %d%%\nZeroCrossLatency: %d us\nFireTime: %d us\n", duty, (int)zeroCrossLatency, (unsigned int)ktime_to_us( triacFireTime));
    
    
    return count;
}

static ssize_t triac_store(struct kobject *kobj, struct kobj_attribute *attr,
                      const char *buf, size_t count)
{
        int value, value2, n;
        char buffer[128];

        //HANDLE PWM MODE
        if ( sscanf(buf, "%d:%d", &value, &value2) >= 2)
        {
            set_triac_pwm( value, value2);
            return count;
        }
        
        
        n = sscanf(buf, "%d%127s", &value, &buffer[0]);
        
        if ( !n)
            printk(KERN_INFO "ktriac: sscanf!!!suck!!!\n");
        else
        if ( n == 1)
        {
            set_triac_attack_angle( value);
        }
        else
        {
            if ( strcmp( &buffer[0], "d") == 0)
            {
                set_triac_attack_angle( value); 
            } else
            //set triac potencial in percentage
            if ( strcmp( &buffer[0], "%") == 0)
            {
                if ( value >= 0 && value <= 100)
                    set_triac_attack_angle( percent_to_angle_table[ value]); 
            } else
            //set triacf "on" time
            if ( strcmp( &buffer[0], "us") == 0)
            {
                triacFireTime = ktime_set( 0, value * 1000);
            } else
            //set latency time
            if ( strcmp( &buffer[0], "kus") == 0)
            {
                set_zerocross_latency( value);
            } else
            //set frequent
            if ( strcmp( &buffer[0], "Hz") == 0 || strcmp( &buffer[0], "hz") == 0)
            {
                set_ac_frequent( value);
            } else
            //set tolerance
            if ( strcmp( &buffer[0], "t") == 0 || strcmp( &buffer[0], "hz") == 0)
            {
                if ( value >=0 && value <= 100)
                {
                    tolerance = value;
                    set_ac_frequent( ac_freq);
                }
            }
        }
        
        return count;
}

static struct kobj_attribute ktriac_kobject_attribute =__ATTR(ktriac, 0664, triac_show,
                                                   triac_store);

static struct kobject *ktriac_kobject;

static void triac_sysfs_init(void){
//    printk(KERN_INFO "ktriac: starting sysfs...\n");
    
    ktriac_kobject = kobject_create_and_add("ktriac", NULL);
    
    if (sysfs_create_file(ktriac_kobject, &ktriac_kobject_attribute.attr)) {
        pr_debug("ktirac: failed to create triac sysfs!\n");
  }
}

static void triac_sysfs_exit(void){
    kobject_put(ktriac_kobject);
}


#ifdef DEBUG_DEVICE        

static int dev_open(struct inode *inode, struct file *file)
{
        return nonseekable_open(inode, file);
}

static int dev_release(struct inode *inode, struct file *file)
{
        return 0;
}

static ssize_t dev_write(struct file *file, const char __user *buf,
                size_t count, loff_t *pos)
{
        return -EINVAL;
}

static ssize_t dev_read(struct file *file, char __user *buf,
                size_t count, loff_t *pos)
{
        char tmp[256];
        int len = 0;


        wait_event_interruptible(waitqueue, updated != 0);

        switch( updated) {
            case UPDATED_TIME:
                len = sprintf(tmp, "%lld\n", delta_us);
                break;

            case UPDATED_DUTY:
                len = sprintf(tmp, "#duty changed: %d%%\n", duty);
                break;
            
            case UPDATED_NOT_HANDLED_TIME:
                len = sprintf(tmp, "\t\t\t%lld\n", delta);
                break;
                
            default:
                len = sprintf(tmp, "#default  updated:%d\n", updated);
                break;
        }

        updated = 0;

        if (copy_to_user(buf, tmp, len)) {
            return -EFAULT;
        }
        return len;
}

unsigned int dev_poll(struct file *filp, struct poll_table_struct *wait)
{
    //wait_event_interruptible(waitqueue, updated == true);
    poll_wait(filp, &waitqueue, wait);
    
    if (updated)
        return POLLIN;
    
    return 0;
}

static struct file_operations dev_fops = {
    .owner = THIS_MODULE,
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .poll = dev_poll,
    .release = dev_release,
};

static struct miscdevice dev_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "ktriac",
    .fops = &dev_fops,
    .mode = 0666
};
#endif


/*
 * Module init function
 */
static int __init ktriac_init(void)
{
        int ret = 0;
//        printk(KERN_INFO "%s\n", __func__);

        //init ac freq variables
        tolerance = AC_DEFAULT_TOLERANCE;
        set_ac_frequent( AC_DEFAULT_FREQ);
        set_zerocross_latency( ZEROCROSS_DEFAULT_LATENCY);
        triacAngle = -1;
        delta_us = 0;
        counter = mark = space = 0;
        duty = 0;
        
#ifdef DEBUG_DEVICE        
        updated = 0;
#endif
        
        // INITIALIZE IRQ TIME AND Queue Management
        lastRising = ktime_set( 0, 0);
        lastFalling = ktime_set( 0, 0);
        triacFireTime = ktime_set( 0, TRIAC_DEFAULT_FIRE_TIME);

        // register GPIO PIN in use
        ret = gpio_request_array(pins, ARRAY_SIZE(pins));

        if (ret) {
                printk(KERN_ERR "ktriac - Unable to request GPIOs for zerocrossing signals & TRIAC output: %d\n", ret);
                goto fail2;
        }

        // Register IRQ for this GPIO
        ret = gpio_to_irq(pins[0].gpio);
        if(ret < 0) {
                printk(KERN_ERR "ktriac - Unable to request IRQ: %d\n", ret);
                goto fail2;
        }

        ac_irqs[0] = ret;
        printk(KERN_INFO "ktriac - Successfully requested zerocrossing IRQ # %d\n", ac_irqs[0]);
        ret = request_irq(ac_irqs[0], zerocross_trigger_isr, IRQF_TRIGGER_RISING /*| IRQF_TRIGGER_FALLING */, "ktriac_ac_zerocross#trigger", NULL);
        if(ret) {
                printk(KERN_ERR "ktriac - Unable to request IRQ: %d\n", ret);
                goto fail3;
        }

        //init hrtimer
        hrtimer_init(&hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
        hr_timer.function = &triac_fire;
        
        //set triac output to low
        triac( OFF);
        
        triac_sysfs_init();
        
#ifdef DEBUG_DEVICE        
        // Register a character device for communication with user space
        init_waitqueue_head(&waitqueue);
        misc_register(&dev_misc_device);
#endif        
        
        return 0;

        // cleanup what has been setup so far
fail3:
        free_irq(ac_irqs[0], NULL);

fail2: 
        gpio_free_array(pins, ARRAY_SIZE(pins));
        return ret;
}

/**
 * Module exit function
 */
static void __exit ktriac_exit(void)
{
//        printk(KERN_INFO "%s\n", __func__);

        triac( OFF);
        
#ifdef DEBUG_DEVICE        
        misc_deregister(&dev_misc_device);
#endif
        
        triac_sysfs_exit();
        
        // free irqs
        free_irq(ac_irqs[0], NULL);

        // unregister
        gpio_free_array(pins, ARRAY_SIZE(pins));
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("The TunguZka Team Hungary");
MODULE_DESCRIPTION("Linux Kernel Module for control TRIACs and so AC circuits");

module_init(ktriac_init);
module_exit(ktriac_exit);
