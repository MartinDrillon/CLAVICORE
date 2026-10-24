#ifndef TEST_STUB_HARDWARE_GPIO_H
#define TEST_STUB_HARDWARE_GPIO_H

#include <stdbool.h>
#include <stdint.h>

#define GPIO_IN 0
#define GPIO_OUT 1

void gpio_init(unsigned int gpio);
void gpio_put(unsigned int gpio, bool value);
bool gpio_get(unsigned int gpio);
bool gpio_get_out_level(unsigned int gpio);
void gpio_set_dir(unsigned int gpio, bool out);
void gpio_pull_up(unsigned int gpio);

#endif
