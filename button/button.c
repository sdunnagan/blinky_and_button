/*-----------------------------------------------------------------------------
 * button.c
 *
 * Application that polls the /dev/gpio_button device descriptor and waits
 * for button events. When an event is detected, it stops polling for 5 seconds
 * and toggles the button LED state via the sysfs interface provided by the
 * driver.
 * 
 * Steve Dunnagan
 * January 29, 2025
 *-----------------------------------------------------------------------------
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#define GPIO_BUTTON_DEVICE "/dev/gpio_button"
#define GPIO_LED_SYSFS_PATH "/sys/class/gpio_button/gpio_button_sysfs/led_status"

int main() {
    int button_fd, led_fd;
    char event_flag;
    char led_value[2];
    int current_led_state = 0;

    // Open in blocking mode
    button_fd = open(GPIO_BUTTON_DEVICE, O_RDONLY);
    if (button_fd < 0) {
        perror("Failed to open GPIO button device");
        return -1;
    }

    // Get initial LED state
    led_fd = open(GPIO_LED_SYSFS_PATH, O_RDONLY);
    if (led_fd >= 0) {
        char init_buf[2];
        if (read(led_fd, init_buf, sizeof(init_buf)) > 0) {
            current_led_state = atoi(init_buf);
        }
        close(led_fd);
    }

    printf("LED Control App - Initial State: %d\n", current_led_state);

    while (1) {
        // Block until the driver signals an event via wait queue
        if (read(button_fd, &event_flag, sizeof(event_flag)) < 0) {
            perror("Read error");
            close(button_fd);
            return -1;
        }

        // Toggle LED state
        current_led_state = !current_led_state;

        // Write new state to sysfs
        led_fd = open(GPIO_LED_SYSFS_PATH, O_WRONLY);
        if (led_fd < 0) {
            fprintf(stderr, "Failed to open LED sysfs: %s\n", strerror(errno));
            close(button_fd);
            return -1;
        }

        snprintf(led_value, sizeof(led_value), "%d", current_led_state);
        write(led_fd, led_value, 1);
        close(led_fd);

        printf("LED Toggled → %d\n", current_led_state);
    }

    close(button_fd);
    return 0;
}
