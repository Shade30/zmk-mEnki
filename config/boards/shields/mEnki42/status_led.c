#include <math.h>
#include <stdlib.h>

#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <zmk/activity.h>
#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>

#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/hid_indicators.h>
#include <zmk/keymap.h>
#include <zmk/usb.h>
#include <zmk/workqueue.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// LED Configurations
#define LED_FADE_STEPS 100
#define LED_FADE_DELAY_MS 2
#define LED_BLINK_PROFILE_DELAY_MS 500
#define LED_BLINK_BATTERY_DELAY_MS 400
#define LED_BLINK_BT_CONNECT_DELAY_MS 400
#define LED_BLINK_USB_DELAY_MS 200

// LED Colors (Enum for better readability)
typedef enum {
    COLOR_PURPLE = 0x800080,
    COLOR_YELLOW = 0xFFFF00,
    COLOR_GREEN  = 0x00FF00,
    COLOR_RED    = 0xFF0000,
    COLOR_BLUE   = 0x0000FF
} LedColor;

#define DISABLE_LED_SLEEP_PC

// LED structure definition
struct Led {
    const struct device *dev;
    uint32_t id;
};

// Enum for LEDs
typedef enum {
    RED_LED,
    GREEN_LED,
    BLUE_LED,
    LED_COUNT
} LedType;

// Array of LEDs
struct Led rgb_led[LED_COUNT] = {
    [RED_LED] = { .dev = DEVICE_DT_GET(DT_CHOSEN(zmk_backlight)), .id = 0 },
    [GREEN_LED] = { .dev = DEVICE_DT_GET(DT_CHOSEN(zmk_backlight)), .id = 1 },
    [BLUE_LED] = { .dev = DEVICE_DT_GET(DT_CHOSEN(zmk_backlight)), .id = 2 },
};

// Global State Variables
bool is_conn_checking = false;
int usb_conn_state = ZMK_USB_CONN_NONE;
uint8_t current_r = 0, current_g = 0, current_b = 0;
static int profile_blink_count = 1;

// Define stack size and priority for the animation work queue
#define ANIMATION_WORK_Q_STACK_SIZE 1024
#define ANIMATION_WORK_Q_PRIORITY 5

// Define the stack area for the animation work queue
K_THREAD_STACK_DEFINE(animation_work_q_stack, ANIMATION_WORK_Q_STACK_SIZE);

// Define the work queue object
struct k_work_q animation_work_q;

// Helper function to set individual LED brightness
static inline void set_led_brightness(LedType led, uint8_t brightness) {
    led_set_brightness(rgb_led[led].dev, rgb_led[led].id, brightness);
}

// Function to turn off all LEDs
static void turn_off_all_leds() {
    if (current_r == 0 && current_g == 0 && current_b == 0) {
        for (int i = 0; i < LED_COUNT; i++) {
            led_off(rgb_led[i].dev, rgb_led[i].id);
        }
        return;
    }

    for (int i = LED_FADE_STEPS; i >= 0; i--) {
        set_led_brightness(RED_LED, current_r * i / LED_FADE_STEPS);
        set_led_brightness(GREEN_LED, current_g * i / LED_FADE_STEPS);
        set_led_brightness(BLUE_LED, current_b * i / LED_FADE_STEPS);
        k_msleep(LED_FADE_DELAY_MS);
    }
    current_r = current_g = current_b = 0;
}

// Function to set RGB color with fade effect
static void rgb_set_color(LedColor color) {
    uint8_t target_r = (color >> 16) & 0xFF;
    uint8_t target_g = (color >> 8) & 0xFF;
    uint8_t target_b = color & 0xFF;

    if (target_r == current_r && target_g == current_g && target_b == current_b) {
        return; // No change in color, skip setting
    }

    for (int i = 0; i <= LED_FADE_STEPS; i++) {
        current_r = target_r * i / LED_FADE_STEPS;
        current_g = target_g * i / LED_FADE_STEPS;
        current_b = target_b * i / LED_FADE_STEPS;

        set_led_brightness(RED_LED, current_r);
        set_led_brightness(GREEN_LED, current_g);
        set_led_brightness(BLUE_LED, current_b);
        k_msleep(LED_FADE_DELAY_MS);
    }
}

// Blink LED with specified color, delay, and count
void rgb_blink_with_color(LedColor color, uint32_t delay_ms, int count) {
    for (int i = 0; i < count; i++) {
        rgb_set_color(color);
        k_msleep(delay_ms);
        turn_off_all_leds();
        k_msleep(delay_ms);
        }
}

// Bluetooth Connection Check Work Handler
struct k_work_delayable check_ble_conn_work;

void check_ble_conn_handler(struct k_work *work) {
    if (!is_conn_checking) {
        return;
    } else {
        if (zmk_ble_active_profile_is_connected() || usb_conn_state != ZMK_USB_CONN_NONE) {
            is_conn_checking = false;
            return;
        } else {
            rgb_blink_with_color(COLOR_RED, LED_BLINK_BT_CONNECT_DELAY_MS, 3);
            k_work_reschedule(&check_ble_conn_work, K_SECONDS(4));
            return;
        }
    }
}
K_WORK_DELAYABLE_DEFINE(check_ble_conn_work, check_ble_conn_handler);

// USB Animation Work Handler
void usb_animation_work_handler(struct k_work *work) {
#ifdef DISABLE_LED_SLEEP_PC
    if (usb_conn_state == USB_DC_SUSPEND) {
        turn_off_all_leds();
        return;
    }
#endif
    rgb_blink_with_color(COLOR_PURPLE, LED_BLINK_USB_DELAY_MS, 3);
}
K_WORK_DELAYABLE_DEFINE(usb_animation_work, usb_animation_work_handler);

// Battery Animation Work Handler
struct k_work_delayable bat_animation_work;

void bat_animation_work_handler(struct k_work *work) {
    uint8_t level = zmk_battery_state_of_charge();
    if (level <= 15) {
        rgb_blink_with_color(COLOR_YELLOW, LED_BLINK_BATTERY_DELAY_MS, 3);
    } else if (level <= 30) {
        rgb_blink_with_color(COLOR_GREEN, LED_BLINK_BATTERY_DELAY_MS, 1);
    } else if (level <= 70) {
        rgb_blink_with_color(COLOR_GREEN, LED_BLINK_BATTERY_DELAY_MS, 2);
    } else {
        rgb_blink_with_color(COLOR_GREEN, LED_BLINK_BATTERY_DELAY_MS, 3);
    }
}
K_WORK_DELAYABLE_DEFINE(bat_animation_work, bat_animation_work_handler);

// LED Initialization
static int led_init(const struct device *dev) {
    turn_off_all_leds();

    // Initialize the work queue
    k_work_queue_init(&animation_work_q);

    // Start the work queue
    k_work_queue_start(&animation_work_q, animation_work_q_stack,
                       K_THREAD_STACK_SIZEOF(animation_work_q_stack), ANIMATION_WORK_Q_PRIORITY,
                       NULL);

    // Schedule battery animation work using the work queue
    k_work_schedule_for_queue(&animation_work_q, &bat_animation_work, K_SECONDS(1));
    return 0;
}

SYS_INIT(led_init, APPLICATION, 32);

struct k_work_delayable ble_profile_work;
// BLE Profile Listener Work Handler
void ble_profile_work_handler(struct k_work *work) {
    rgb_blink_with_color(COLOR_BLUE, LED_BLINK_PROFILE_DELAY_MS, profile_blink_count);
    if (!is_conn_checking) {
        is_conn_checking = true;
        k_work_reschedule(&check_ble_conn_work, K_SECONDS(4));
    }
}

// BLE Profile Listener using k_work_schedule_for_queue
int ble_profile_listener(const zmk_event_t *eh) {
    const struct zmk_ble_active_profile_changed *profile_ev = as_zmk_ble_active_profile_changed(eh);
    if (profile_ev && profile_ev->index <= 2) {
        profile_blink_count = profile_ev->index + 1;  // Set blink count based on profile index
        k_work_schedule_for_queue(&animation_work_q, &ble_profile_work, K_NO_WAIT);
    }
    return ZMK_EV_EVENT_BUBBLE;
}
K_WORK_DELAYABLE_DEFINE(ble_profile_work, ble_profile_work_handler);

ZMK_LISTENER(ble_profile_status, ble_profile_listener)
ZMK_SUBSCRIPTION(ble_profile_status, zmk_ble_active_profile_changed);

// USB Connection Listener Work Handler
struct k_work_delayable usb_conn_work;
void usb_conn_work_handler(struct k_work *work) {
    if (usb_conn_state == ZMK_USB_CONN_POWERED) {
        k_work_schedule_for_queue(&animation_work_q, &usb_animation_work, K_NO_WAIT);
    } else {
        is_conn_checking = true;
        k_work_reschedule(&check_ble_conn_work, K_SECONDS(4));
    }
}

// USB Connection Listener using k_work_schedule_for_queue
int usb_conn_listener(const zmk_event_t *eh) {
    const struct zmk_usb_conn_state_changed *usb_ev = as_zmk_usb_conn_state_changed(eh);
    if (usb_ev) {
        usb_conn_state = usb_ev->conn_state;
        k_work_schedule_for_queue(&animation_work_q, &usb_conn_work, K_NO_WAIT);
    }
    return ZMK_EV_EVENT_BUBBLE;
}
K_WORK_DELAYABLE_DEFINE(usb_conn_work, usb_conn_work_handler);

ZMK_LISTENER(usb_conn_state_listener, usb_conn_listener)
ZMK_SUBSCRIPTION(usb_conn_state_listener, zmk_usb_conn_state_changed);

// Functions to Show and Hide Battery Level Animation
void show_battery() {
    k_work_schedule_for_queue(&animation_work_q, &bat_animation_work, K_NO_WAIT);
}

void hide_battery() {
    // turn_off_all_leds();  // Uncomment if needed to turn off LEDs when hiding battery animation
}
