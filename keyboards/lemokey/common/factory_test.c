/* Copyright 2024 @ Keychron (https://www.keychron.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "quantum.h"
#include "raw_hid.h"
#include "via.h"
#include "lemokey_task.h"
#include "config.h"
#include "version.h"

#ifndef RAW_EPSIZE
#    define RAW_EPSIZE 32
#endif

#ifndef BL_CYCLE_KEY
#    define BL_CYCLE_KEY KC_RIGHT
#endif

#ifndef BL_TRIG_KEY
#    define BL_TRIG_KEY KC_HOME
#endif

enum {
    BACKLIGHT_TEST_OFF = 0,
    BACKLIGHT_TEST_WHITE,
    BACKLIGHT_TEST_RED,
    BACKLIGHT_TEST_GREEN,
    BACKLIGHT_TEST_BLUE,
    BACKLIGHT_TEST_MAX,
};

enum {
    KEY_PRESS_FN             = 0x01 << 0,
    KEY_PRESS_J              = 0x01 << 1,
    KEY_PRESS_Z              = 0x01 << 2,
    KEY_PRESS_BL_KEY1        = 0x01 << 3,
    KEY_PRESS_BL_KEY2        = 0x01 << 4,
    KEY_PRESS_FACTORY_RESET  = KEY_PRESS_FN | KEY_PRESS_J | KEY_PRESS_Z,
    KEY_PRESS_BACKLIGTH_TEST = KEY_PRESS_FN | KEY_PRESS_BL_KEY1 | KEY_PRESS_BL_KEY2,
};

enum {
    FACTORY_TEST_CMD_BACKLIGHT = 0x01,
    FACTORY_TEST_CMD_OS_SWITCH,
    FACTORY_TEST_CMD_JUMP_TO_BL,
    FACTORY_TEST_CMD_INT_PIN,
    FACTORY_TEST_CMD_GET_TRANSPORT,
    FACTORY_TEST_CMD_CHARGING_ADC,
    FACTORY_TEST_CMD_RADIO_CARRIER,
    FACTORY_TEST_CMD_GET_BUILD_TIME,
    FACTORY_TEST_CMD_GET_DEVICE_ID,
};

static uint32_t factory_reset_timer = 0;
static uint8_t  factory_reset_state = 0;
static uint8_t  backlight_test_mode = BACKLIGHT_TEST_OFF;

static uint32_t factory_reset_ind_timer = 0;
static uint8_t  factory_reset_ind_state = 0;
static bool     report_os_sw_state      = false;
static bool     keys_released           = true;

void factory_timer_start(void) {
    factory_reset_timer = timer_read32();
}

static inline void factory_timer_check(void) {
    if (timer_elapsed32(factory_reset_timer) > 3000) {
        factory_reset_timer = 0;

        if (factory_reset_state == KEY_PRESS_FACTORY_RESET) {
            factory_reset_ind_timer = timer_read32();
            factory_reset_ind_state++;
            keys_released = false;

            clear_keyboard(); // Avoid key being pressed after NKRO state changed
            layer_state_t default_layer_tmp = default_layer_state;
            eeconfig_init();
            keymap_config.raw = eeconfig_read_keymap();
            default_layer_set(default_layer_tmp);
#ifdef LED_MATRIX_ENABLE
            if (!led_matrix_is_enabled()) led_matrix_enable();
            led_matrix_init();
#endif
#ifdef RGB_MATRIX_ENABLE
            if (!rgb_matrix_is_enabled()) rgb_matrix_enable();
            rgb_matrix_init();
#endif
        } else if (factory_reset_state == KEY_PRESS_BACKLIGTH_TEST) {
#ifdef LED_MATRIX_ENABLE
            if (!led_matrix_is_enabled()) led_matrix_enable();
#endif
#ifdef RGB_MATRIX_ENABLE
            if (!rgb_matrix_is_enabled()) rgb_matrix_enable();
#endif
            backlight_test_mode = BACKLIGHT_TEST_WHITE;
        }

        factory_reset_state = 0;
    }
}

static inline void factory_reset_ind_timer_check(void) {
    if (factory_reset_ind_timer && timer_elapsed32(factory_reset_ind_timer) > 250) {
        if (factory_reset_ind_state++ > 6) {
            factory_reset_ind_timer = factory_reset_ind_state = 0;
        } else {
            factory_reset_ind_timer = timer_read32();
        }
    }
}

bool process_record_factory_test(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
#if defined(FN_KEY_1) || defined(FN_KEY_2)
#    if defined(FN_KEY_1)
        case FN_KEY_1: /* fall through */
#    endif
#    if defined(FN_KEY_2)
        case FN_KEY_2:
#    endif
#    if defined(FN_KEY_3)
        case FN_KEY_3:
#    endif
            if (record->event.pressed) {
                factory_reset_state |= KEY_PRESS_FN;
            } else {
                factory_reset_state &= ~KEY_PRESS_FN;
                factory_reset_timer = 0;
            }
            break;
#endif

        case KC_J:
            if (record->event.pressed) {
                factory_reset_state |= KEY_PRESS_J;
                if (factory_reset_state == 0x07) factory_timer_start();
                if (factory_reset_state & KEY_PRESS_FN) return false;
            } else {
                factory_reset_state &= ~KEY_PRESS_J;
                factory_reset_timer = 0;
            }
            break;

        case KC_Z:
#if defined(FN_Z_KEY)
        case FN_Z_KEY:
#endif
            if (record->event.pressed) {
                factory_reset_state |= KEY_PRESS_Z;
                if (factory_reset_state == 0x07) factory_timer_start();
                if ((factory_reset_state & KEY_PRESS_FN) && keycode == KC_Z) return false;
            } else {
                factory_reset_state &= ~KEY_PRESS_Z;
                factory_reset_timer = 0;
                /* Avoid changing backlight effect on key repleased if FN_Z_KEY is mode*/

                if (!keys_released && keycode >= QK_BACKLIGHT_ON && keycode <= RGB_MODE_TWINKLE) {
                    keys_released = true;
                    return false;
                }
            }
            break;

#if defined(BL_CYCLE_KEY) || defined(BL_CYCLE_KEY_2)
#    if defined(BL_CYCLE_KEY)
        case BL_CYCLE_KEY:
#    endif
#    if defined(FN_BL_CYCLE_KEY)
        case FN_BL_CYCLE_KEY:
#    endif
            if (record->event.pressed) {
                if (backlight_test_mode) {
                    if (++backlight_test_mode >= BACKLIGHT_TEST_MAX) {
                        backlight_test_mode = BACKLIGHT_TEST_WHITE;
                    }
                } else {
                    factory_reset_state |= KEY_PRESS_BL_KEY1;
                    if (factory_reset_state == 0x19) {
                        factory_timer_start();
                    }
                }
            } else {
                factory_reset_state &= ~KEY_PRESS_BL_KEY1;
                factory_reset_timer = 0;
            }
            break;
#endif
#if defined(BL_TRIG_KEY) || defined(BL_TRIG_KEY_2)
#    if defined(BL_TRIG_KEY)
        case BL_TRIG_KEY:
#    endif
#    if defined(FN_BL_TRIG_KEY)
        case FN_BL_TRIG_KEY:
#    endif
            if (record->event.pressed) {
                if (backlight_test_mode) {
                    backlight_test_mode = BACKLIGHT_TEST_OFF;
                } else {
                    factory_reset_state |= KEY_PRESS_BL_KEY2;
                    if (factory_reset_state == 0x19) {
                        factory_timer_start();
                    }
                }
            } else {
                factory_reset_state &= ~KEY_PRESS_BL_KEY2;
                factory_reset_timer = 0;
            }
            break;
#endif
    }

    return true;
}

#ifdef LED_MATRIX_ENABLE
bool factory_test_indicator(void) {
    if (factory_reset_ind_state) {
        led_matrix_set_value_all(factory_reset_ind_state % 2 ? 0 : 255);
        return false;
    }

    return true;
}
#endif

#ifdef RGB_MATRIX_ENABLE
bool factory_test_indicator(void) {
    if (factory_reset_ind_state) {
        backlight_test_mode = BACKLIGHT_TEST_OFF;
        rgb_matrix_set_color_all(factory_reset_ind_state % 2 ? 0 : 255, 0, 0);
        return false;
    } else if (backlight_test_mode) {
        switch (backlight_test_mode) {
            case BACKLIGHT_TEST_WHITE:
                rgb_matrix_set_color_all(255, 255, 255);
                break;

            case BACKLIGHT_TEST_RED:
                rgb_matrix_set_color_all(255, 0, 0);
                break;

            case BACKLIGHT_TEST_GREEN:
                rgb_matrix_set_color_all(0, 255, 0);
                break;

            case BACKLIGHT_TEST_BLUE:
                rgb_matrix_set_color_all(0, 0, 255);
                break;
        }
        return false;
    }

    return true;
}
#endif

bool factory_reset_indicating(void) {
    return factory_reset_ind_timer;
}

bool factory_test_task(void) {
    if (factory_reset_timer) factory_timer_check();
    if (factory_reset_ind_timer) factory_reset_ind_timer_check();

    return true;
}

void factory_test_send(uint8_t *payload, uint8_t length) {
#ifdef RAW_ENABLE
    uint16_t checksum         = 0;
    uint8_t  data[RAW_EPSIZE] = {0};

    uint8_t i = 0;
    data[i++] = 0xAB;

    memcpy(&data[i], payload, length);
    i += length;

    for (uint8_t i = 1; i < RAW_EPSIZE - 3; i++)
        checksum += data[i];
    data[RAW_EPSIZE - 2] = checksum & 0xFF;
    data[RAW_EPSIZE - 1] = (checksum >> 8) & 0xFF;

    raw_hid_send(data, RAW_EPSIZE);
#endif
}

void factory_test_rx(uint8_t *data, uint8_t length) {
    if (data[0] == 0xAB) {
        uint16_t checksum = 0;

        for (uint8_t i = 1; i < RAW_EPSIZE - 3; i++) {
            checksum += data[i];
        }
        /* Verify checksum */
        if ((checksum & 0xFF) != data[RAW_EPSIZE - 2] || checksum >> 8 != data[RAW_EPSIZE - 1]) return;

        uint8_t payload[32];
        uint8_t len = 0;

        switch (data[1]) {
            case FACTORY_TEST_CMD_BACKLIGHT:
                backlight_test_mode = data[2];
                factory_reset_timer = 0;
                break;

            case FACTORY_TEST_CMD_OS_SWITCH:
                report_os_sw_state = data[2];
                break;

            case FACTORY_TEST_CMD_JUMP_TO_BL:
                break;

            case FACTORY_TEST_CMD_GET_BUILD_TIME: {
                payload[len++] = FACTORY_TEST_CMD_GET_BUILD_TIME;
                payload[len++] = 'v';
                if ((DEVICE_VER & 0xF000) != 0) itoa((DEVICE_VER >> 12), (char *)&payload[len++], 16);
                itoa((DEVICE_VER >> 8) & 0xF, (char *)&payload[len++], 16);
                payload[len++] = '.';
                itoa((DEVICE_VER >> 4) & 0xF, (char *)&payload[len++], 16);
                payload[len++] = '.';
                itoa((DEVICE_VER >> 4) & 0xF, (char *)&payload[len++], 16);
                payload[len++] = ' ';
                memcpy(&payload[len], QMK_BUILDDATE, sizeof(QMK_BUILDDATE));
                len += sizeof(QMK_BUILDDATE);
                factory_test_send(payload, len);
            } break;

            case FACTORY_TEST_CMD_GET_DEVICE_ID:
                payload[len++] = FACTORY_TEST_CMD_GET_DEVICE_ID;
                payload[len++] = 12;
                memcpy(&payload[len], (uint32_t *)UID_BASE, 4);
                memcpy(&payload[len + 4], (uint32_t *)UID_BASE + 4, 4);
                memcpy(&payload[len + 8], (uint32_t *)UID_BASE + 8, 4);

                len += 12;
                factory_test_send(payload, len);
                break;
        }
    }
}
