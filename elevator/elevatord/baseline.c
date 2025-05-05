#include <gpiod.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define GPIO_CHIP_NAME "/dev/gpiochip0"

#define BASELINE_1_GPIO_PIN 80  // GPIOA_7
#define BASELINE_2_GPIO_PIN 81  // GPIOA_8

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct baseline_gpio {
    struct gpiod_chip* chip;
    struct gpiod_line_bulk bulk;
    
    int running;
    pthread_t tid;
} _baseline_gpio = {NULL, {}, 0, -1};

struct gpio_pin {
    const char* name;
    unsigned int offset;
} _baseline_pins[] = {
    {.name = "baseline1", BASELINE_1_GPIO_PIN},
    {.name = "baseline2", BASELINE_2_GPIO_PIN}};

static void* _baseline_monitor_routin(void* args) {
    int ret = -1;
    printf("%s(%d): ......\n", __FUNCTION__, __LINE__);
 struct timespec timeout = {1, 0};
    struct baseline_gpio* gpio = (struct baseline_gpio*)args;
    if (!gpio) {
        return NULL;
    }

    gpio->running = 1;
    ret = gpiod_line_request_bulk_both_edges_events(&gpio->bulk, "multi gpios trigger");

    while (gpio->running) {
        struct gpiod_line_bulk event_bulk;
        ret = gpiod_line_event_wait_bulk(&gpio->bulk, &timeout, &event_bulk);
        if (-1 == ret) {
            usleep(1000 * 1000);
            continue;
        }
        if (0 == ret) {
            continue;
        }

        for (size_t i = 0; i < gpiod_line_bulk_num_lines(&event_bulk); i++) {
            struct gpiod_line_event event;
            struct gpiod_line* line;
            line = gpiod_line_bulk_get_line(&event_bulk, i);

            printf("line %s(%d)\n", gpiod_line_name(line), gpiod_line_offset(line));
            ret = gpiod_line_event_read(line, &event);
            if (event.event_type == GPIOD_LINE_EVENT_RISING_EDGE) {
                printf("Rising edge detected on GPIO %d\n", gpiod_line_offset(line));
            } else if (event.event_type == GPIOD_LINE_EVENT_FALLING_EDGE) {
                printf("Falling edge detected on GPIO %d\n", gpiod_line_offset(line));
            }
        }
    }

    return NULL;
}
int baseline_init() {
    pthread_attr_t attr;
    if (!_baseline_gpio.chip) {
        _baseline_gpio.chip = gpiod_chip_open(GPIO_CHIP_NAME);
        if (!_baseline_gpio.chip) {
            return -1;
        }
        gpiod_line_bulk_init(&_baseline_gpio.bulk);
        for (size_t i = 0; i < ARRAY_SIZE(_baseline_pins); i++) {
            struct gpiod_line* line = gpiod_chip_get_line(_baseline_gpio.chip, _baseline_pins[i].offset);
            if (!line) {
                goto failed;
            }
            gpiod_line_bulk_add(&_baseline_gpio.bulk, line);
        }

        gpiod_line_request_bulk_both_edges_events(&_baseline_gpio.bulk, "baseline multi gpios trigger");
    }

    pthread_attr_init(&attr);

    if (0 != pthread_create(&_baseline_gpio.tid, &attr, _baseline_monitor_routin, &_baseline_gpio)) {
        pthread_attr_destroy(&attr);
        goto failed;
    }
    pthread_attr_destroy(&attr);
    return 0;
failed:

    // for (i = 0; i < gpiod_line_bulk_num_lines(&_baseline_gpio.bulk); i++) {
    //     struct gpiod_line* line = gpiod_line_bulk_get_line(&_baseline_gpio.bulk, i);
    //     if (line) {
    //         gpiod_line_release(line);
    //     }
    // }

    gpiod_line_release_bulk(&_baseline_gpio.bulk);

    gpiod_chip_close(_baseline_gpio.chip);
    memset((void*)&_baseline_gpio, 0, sizeof(_baseline_gpio));

    return -1;
}
int baseline_deinit() {
    _baseline_gpio.running = 0;
    pthread_cancel(_baseline_gpio.tid);
    pthread_join(_baseline_gpio.tid, NULL);

    // for (size_t i = 0; i < gpiod_line_bulk_num_lines(&_baseline_gpio.bulk); i++) {
    //     struct gpiod_line* line = gpiod_line_bulk_get_line(&_baseline_gpio.bulk, i);
    //     if (line) {
    //         gpiod_line_release(line);
    //     }
    // }

    gpiod_line_release_bulk(&_baseline_gpio.bulk);

    gpiod_chip_close(_baseline_gpio.chip);
    memset((void*)&_baseline_gpio, 0, sizeof(_baseline_gpio));

    return 0;
}
#if ENABLE_TEST
int main(int argc, char** argv) {
    baseline_init();

    getchar();
    
    baseline_deinit();
    return 0;
}
#endif
