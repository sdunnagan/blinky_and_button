/*-----------------------------------------------------------------------------
 * blinky
 *
 * Application for blinking an LED on RPi 4 using libgpiod.
 * 
 * Steve Dunnagan
 * January 22, 2025
 *-----------------------------------------------------------------------------
*/
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <signal.h>
#include <gpiod.h>
#include <errno.h>
#include <string.h>

#define DEBUG_PRINT(fmt, ...) \
    fprintf(stderr, "%s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define ERROR_PRINT(fmt, ...) \
    fprintf(stderr, "%s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define GPIO_OUTPUT_PIN 18

static bool stop_flag = false;

static int gpio_write(int pin, int value)
{
    struct gpiod_chip *chip;
    struct gpiod_line *line;
    int ret;

    chip = gpiod_chip_open("/dev/gpiochip0");
    if (!chip) {
        syslog(LOG_ERR, "Failed to open GPIO chip");
        ERROR_PRINT("gpiod_chip_open() failed");
        fprintf(stderr, "gpiod_chip_open() failed, code: %d, message: %s\n",
            errno, strerror(errno));
        return -1;
    }

    // Get the GPIO line (pin) based on the pin number
    line = gpiod_chip_get_line(chip, pin);
    if (!line) {
        syslog(LOG_ERR, "Failed to get GPIO line");
        ERROR_PRINT("gpiod_chip_get_line() failed");
        fprintf(stderr, "gpiod_chip_get_line() failed, code: %d, message: %s\n",
            errno, strerror(errno));
        gpiod_chip_close(chip);
        return -1;
    }

    // Request the line as output
    ret = gpiod_line_request_output(line, "blinky", value);
    if (ret < 0) {
        syslog(LOG_ERR, "Failed to request GPIO line for output");
        ERROR_PRINT("gpiod_line_request_output() failed");
        fprintf(stderr, "gpiod_line_request_output() failed, code: %d, message: %s\n",
            errno, strerror(errno));
        gpiod_chip_close(chip);
        return -1;
    }

    // Set the GPIO line value
    ret = gpiod_line_set_value(line, value);
    if (ret < 0) {
        syslog(LOG_ERR, "Failed to write to GPIO line");
        ERROR_PRINT("gpiod_line_set_value() failed");
        fprintf(stderr, "gpiod_line_set_value() failed, code: %d, message: %s\n",
            errno, strerror(errno));
        gpiod_chip_close(chip);
        return -1;
    }

    gpiod_chip_close(chip);
    return 0;
}

// Blinky thread function
static void *blinky_thread(void *arg)
{
    while (!stop_flag) {
        syslog(LOG_DEBUG, "Setting gpio %d high", GPIO_OUTPUT_PIN);
        gpio_write(GPIO_OUTPUT_PIN, 1);  // GPIO 18
        sleep(1);

        syslog(LOG_DEBUG, "Setting gpio %d low", GPIO_OUTPUT_PIN);
        gpio_write(GPIO_OUTPUT_PIN, 0);
        sleep(1);
    }

    return NULL;
}

void signal_handler(int signal) {
    stop_flag = true;
    syslog(LOG_INFO, "Received signal %d - exiting", signal);
}

void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s\n\n", prog_name);
    fprintf(stderr, "  -D  Do not daemonize\n");
    fprintf(stderr, "  -h  Display usage information (this message)\n\n");
}

int main(int argc, char *argv[]) {
    bool daemonize = true;
    int opt;
    int retval = EXIT_SUCCESS;

    while ((opt = getopt (argc, argv, "Dh")) >= 0) {
        switch (opt) {
        case 'D':
            daemonize = false;
            break;
        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        default:
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    // Setup signal handler
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGSEGV, signal_handler);

    // Set logging level for messages submitted to syslog
    setlogmask(LOG_UPTO(LOG_DEBUG));
    openlog(NULL, LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);

    syslog(LOG_INFO, "Started");

    // Run in the background if needed
    if (daemonize) {
        if (daemon(0, 0) < 0) {
            syslog(LOG_ERR, "Daemonizing failed");
            goto err;
        }
    }

    // Spawn a thread to blink the LEDs
    pthread_t thread1;
    if (pthread_create(&thread1, NULL, blinky_thread, NULL) != 0) {
        syslog(LOG_ERR, "Failed to create blinky thread");
        goto err;
    }

    while (!stop_flag) {
        sleep(1);
    }

    syslog(LOG_INFO, "Main thread exiting");

    pthread_join(thread1, NULL);

done:
    closelog();
    return retval;

err:
    retval = EXIT_FAILURE;
    goto done;
}
