/*-----------------------------------------------------------------------------
 * gpio_button.c
 *
 * Platform driver for Raspberry Pi 4 using a Device Tree overlay to map GPIOs
 * and detect button presses and set an LED.
 *
 * January 29, 2025
 * Steve Dunnagan
 *-----------------------------------------------------------------------------
*/
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/cdev.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/atomic.h>
#include <linux/timer.h>

#define DRIVER_NAME "gpio_button"

static struct timer_list debounce_timer;
static atomic_t debounce_active = ATOMIC_INIT(0);

static struct gpio_desc *button_gpio;
static struct gpio_desc *led_gpio;
static int irq_number;
static dev_t dev_num;
static struct cdev c_dev;
static struct class *cl;
static DECLARE_WAIT_QUEUE_HEAD(button_wait);
static atomic_t button_event_flag = ATOMIC_INIT(0);
static volatile int led_status = 0;

static void debounce_timer_callback(struct timer_list *timer)
{
    int button_state = gpiod_get_value(button_gpio);
    
    if (button_state == 0) {  // Assuming active-low button
        atomic_set(&button_event_flag, 1);
        wake_up(&button_wait);
    }
    
    atomic_set(&debounce_active, 0);  // Re-enable interrupts
}

static irqreturn_t button_isr(int irq, void *dev_id)
{
    // Ignore interrupts during debounce period
    if (atomic_read(&debounce_active))
        return IRQ_HANDLED;
    
    // Start debounce timer
    atomic_set(&debounce_active, 1);
    mod_timer(&debounce_timer, jiffies + msecs_to_jiffies(50));  // 50ms debounce
    
    return IRQ_HANDLED;
}

static ssize_t gpio_button_read(struct file *file, char __user *buffer, size_t len, loff_t *offset)
{
    char event_char;
    int ret;

    // Wait until the event flag is set (blocking)
    ret = wait_event_interruptible(button_wait, atomic_read(&button_event_flag));
    if (ret) {
        return -ERESTARTSYS; // Interrupted by signal
    }

    // Event occurred, translate it to ASCII
    event_char = '1';

    // Clear the event flag
    atomic_set(&button_event_flag, 0);

    // Copy the ASCII character to userspace
    if (copy_to_user(buffer, &event_char, sizeof(event_char)))
        return -EFAULT;

    return sizeof(event_char);
}

static unsigned int gpio_button_poll(struct file *file, poll_table *wait)
{
    poll_wait(file, &button_wait, wait);
    return atomic_read(&button_event_flag) ? POLLIN : 0;
}

static int gpio_button_open(struct inode *inode, struct file *file)
{
    return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = gpio_button_open,
    .read = gpio_button_read,
    .poll = gpio_button_poll,
};

// Sysfs attribute show function
static ssize_t led_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", led_status);
}

static ssize_t led_status_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned long val;
    char local_buf[16]; // Buffer to hold the input string
    int ret;

    // Ensure the buffer is null-terminated and within size limits
    if (count >= sizeof(local_buf)) {
        pr_err("gpio_button: Input is too long (%zu bytes)\n", count);
        return -EINVAL;
    }

    // Copy the input to a local buffer and null-terminate it
    memcpy(local_buf, buf, count);
    local_buf[count] = '\0';

    // Remove any trailing newline character
    if (local_buf[count - 1] == '\n') {
        local_buf[count - 1] = '\0';
    }

    // Debugging: Print the processed input string
    pr_info("gpio_button: Processed input: '%s'\n", local_buf);

    // Convert the string to an integer
    ret = kstrtoul(local_buf, 10, &val);
    if (ret) {
        pr_err("gpio_button: Failed to convert input to integer, ret = %d\n", ret);
        return ret;
    }

    // Validate the input (must be 0 or 1)
    if (val != 0 && val != 1) {
        pr_err("gpio_button: Input value must be 0 or 1, got %lu\n", val);
        return -EINVAL;
    }

    // Set the LED status
    led_status = val;
    gpiod_set_value(led_gpio, led_status);

    pr_info("gpio_button: LED status set to %lu\n", val);

    return count;
}

static DEVICE_ATTR(led_status, 0664, led_status_show, led_status_store);

static struct device *sysfs_dev; // Device for sysfs attribute

static int gpio_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    int ret = 0;

    pr_info("gpio_button: %s():%d: Probe started\n",
            __func__, __LINE__);

    // Get GPIO descriptors from device tree
    button_gpio = gpiod_get(dev, "button", GPIOD_IN);
    if (IS_ERR(button_gpio)) {
        dev_err(dev, "Failed to get BUTTON GPIO: %ld\n", PTR_ERR(button_gpio));
        pr_err("gpio_button: %s():%d: Button GPIO error, code: %d\n",
                __func__, __LINE__, PTR_ERR(button_gpio));
        return PTR_ERR(button_gpio);
    }
    pr_info("gpio_button: %s():%d: Button GPIO acquired: %d\n",
            __func__, __LINE__, desc_to_gpio(button_gpio));

    led_gpio = gpiod_get(dev, "led", GPIOD_OUT_LOW);
    if (IS_ERR(led_gpio)) {
        dev_err(dev, "Failed to get LED GPIO: %ld\n", PTR_ERR(led_gpio));
        pr_err("gpio_button: %s():%d: LED GPIO error, code: $ld\n",
               __func__, __LINE__, PTR_ERR(led_gpio));
        ret = PTR_ERR(led_gpio);
        goto err_led;
    }
    pr_info("gpio_button: %s():%d: LED GPIO acquired: %d\n",
            __func__, __LINE__, desc_to_gpio(led_gpio));

    // Setup interrupt
    irq_number = gpiod_to_irq(button_gpio);
    if (irq_number < 0) {
        dev_err(dev, "Failed to get IRQ: %d\n", irq_number);
        pr_err("gpio_button: %s():%d: IRQ error, code: %d\n",
               __func__, __LINE__, irq_number);
        ret = irq_number;
        goto err_irq;
    }
    pr_info("gpio_button: %s():%d: IRQ number: %d\n",
            __func__, __LINE__, irq_number);

    ret = request_irq(irq_number, button_isr, IRQF_TRIGGER_FALLING, DRIVER_NAME, NULL);
    if (ret) {
        dev_err(dev, "Failed to request IRQ %d\n", irq_number);
        pr_err("GPIO Driver: IRQ Request Error! Code: %d\n", ret);
        goto err_irq;
    }
    pr_info("gpio_button: %s():%d: IRQ registered successfully\n",
            __func__, __LINE__);

    // Create character device
    if (alloc_chrdev_region(&dev_num, 0, 1, DRIVER_NAME)) {
        ret = -ENODEV;
        pr_err("gpio_button: %s():%d: Failed to allocate chrdev region\n",
               __func__, __LINE__);
        goto err_alloc;
    }
    pr_info("gpio_button: %s():%d: chrdev region allocated\n",
            __func__, __LINE__);

    cdev_init(&c_dev, &fops);
    if (cdev_add(&c_dev, dev_num, 1)) {
        ret = -ENODEV;
        pr_err("GPIO Driver: Failed to Add Cdev\n");
        goto err_add;
    }
    pr_info("gpio_button: %s():%d: cdev added\n",
            __func__, __LINE__);

    cl = class_create(DRIVER_NAME);
    if (IS_ERR(cl)) {
        ret = PTR_ERR(cl);
        pr_err("gpio_button: %s():%d: Create class error, code: %d\n",
               __func__, __LINE__, ret);
        goto err_class;
    }
    pr_info("gpio_button: %s():%d: Class created\n",
            __func__, __LINE__);

    // Create device for /dev/gpio_button
    device_create(cl, NULL, dev_num, NULL, "%s", DRIVER_NAME);

    // Create device for sysfs attribute
    sysfs_dev = device_create(cl, NULL, 0, NULL, "gpio_button_sysfs");
    if (IS_ERR(sysfs_dev)) {
        ret = PTR_ERR(sysfs_dev);
        pr_err("gpio_button: %s():%d: Failed to create sysfs device\n",
               __func__, __LINE__);
        goto err_sysfs_dev;
    }

    // Create sysfs attribute
    ret = device_create_file(sysfs_dev, &dev_attr_led_status);
    if (ret) {
        pr_err("gpio_button: %s():%d: Failed to create sysfs attribute\n",
               __func__, __LINE__);
        goto err_sysfs_attr;
    }

    // Setup button debounce timer.
    timer_setup(&debounce_timer, debounce_timer_callback, 0);

    pr_info("gpio_button: %s():%d: Probe completed successfully\n",
            __func__, __LINE__);

    return 0;

err_sysfs_attr:
    device_destroy(cl, 0);

err_sysfs_dev:
    device_destroy(cl, dev_num);

err_class:
    cdev_del(&c_dev);

err_add:
    unregister_chrdev_region(dev_num, 1);

err_alloc:
    free_irq(irq_number, NULL);

err_irq:
    gpiod_put(led_gpio);

err_led:
    gpiod_put(button_gpio);
    pr_info("gpio_button: %s():%d: Probe failed, code: %d\n",
            __func__, __LINE__, ret);
    return ret;
}

static int gpio_remove(struct platform_device *pdev)
{
    // Remove sysfs attribute
    device_remove_file(sysfs_dev, &dev_attr_led_status);

    // Destroy sysfs device
    device_destroy(cl, 0);

    // Destroy /dev/gpio_button device
    device_destroy(cl, dev_num);

    class_destroy(cl);
    cdev_del(&c_dev);
    unregister_chrdev_region(dev_num, 1);
    free_irq(irq_number, NULL);
    gpiod_put(button_gpio);
    gpiod_put(led_gpio);

    return 0;
}

static const struct of_device_id gpio_button_of_match[] = {
    { .compatible = "custom,gpio-button" },
    { },
};
MODULE_DEVICE_TABLE(of, gpio_button_of_match);

static struct platform_driver gpio_platform_driver = {
    .probe = gpio_probe,
    .remove = gpio_remove,
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = gpio_button_of_match,
    },
};

module_platform_driver(gpio_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Steve Dunnagan");
MODULE_DESCRIPTION("GPIO button and LED driver");
MODULE_VERSION("4.0");
