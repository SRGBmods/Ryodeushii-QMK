/*
Copyright 2023 @ Nuphy <https://nuphy.com/>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "user_kb.h"
#include "ansi.h"
#include "hal_usb.h"
#include "usb_main.h"
#include "mcu_pwr.h"

extern DEV_INFO_STRUCT dev_info;
extern uint16_t        rf_linking_time;
extern uint16_t        rf_link_timeout;
extern uint32_t        no_act_time;
extern bool            f_goto_sleep;
extern bool            f_wakeup_prepare;

void set_side_rgb(uint8_t r, uint8_t g, uint8_t b);
void set_logo_rgb(uint8_t r, uint8_t g, uint8_t b);
void deep_sleep_handle(void) {
    // Visual cue for deep sleep on side LED.
    pwr_side_led_on();
    wait_ms(50); // give some time to ensure LED powers on.
    set_side_rgb(0x99, 0x00, 0x00);
    set_logo_rgb(0x99, 0x00, 0x00);
    wait_ms(500);

    // Sync again before sleeping. Without this, the wake keystroke is more likely to be lost.
    dev_sts_sync();

    enter_deep_sleep(); // puts the board in WFI mode and pauses the MCU
    exit_deep_sleep();  // This gets called when there is an interrupt (wake) event.

    no_act_time = 0; // required to not cause an immediate sleep on first wake
}

/**
 * @brief  Sleep Handle.
 */
void sleep_handle(void) {
    static uint32_t delay_step_timer     = 0;
    static uint8_t  usb_suspend_debounce = 0;
#if (WORK_MODE == THREE_MODE)
    static uint32_t rf_disconnect_time = 0;
#endif

    /* 50ms interval */
    if (timer_elapsed32(delay_step_timer) < 50) return;
    delay_step_timer = timer_read32();

    if (!g_config.sleep_enable) return;
    uint32_t sleep_time_delay = get_sleep_timeout();
    // sleep process;
    if (f_goto_sleep) {
        // reset all counters
        f_goto_sleep         = 0;
        usb_suspend_debounce = 0;
#if (WORK_MODE == THREE_MODE)
        rf_linking_time    = 0;
        rf_disconnect_time = 0;
#endif

        // don't deep sleep if charging on wireless, charging interrupts and wakes the MCU
        if (dev_info.link_mode != LINK_USB && dev_info.rf_charge & 0x01) {
            break_all_key();
            enter_light_sleep();
            // Don't deep sleep if in USB mode. Board may have issues waking as reported by others. I assume it's being
            // powered if USB port is on, or otherwise it's disconnected at the hardware level if USB port is off..
        } else if (dev_info.link_mode == LINK_USB && (g_config.usb_sleep_toggle || USB_DRIVER.state == USB_SUSPENDED)) {
            break_all_key();
            enter_light_sleep();
        } else if (g_config.sleep_enable) {
            break_all_key(); // reset keys before sleeping for new QMK lifecycle to handle on wake.
            deep_sleep_handle();
            return; // don't need to do anything else
        }

        f_wakeup_prepare = 1; // only if light sleep.
    }

    // NOTE: wakeup logic moved to early keypress detection in ansi.c -> pre_process_record_kb

    // sleep check, won't reach here on deep sleep.
    if (f_goto_sleep || f_wakeup_prepare) return;

    if (dev_info.link_mode == LINK_USB) {
        if (USB_DRIVER.state == USB_SUSPENDED) {
            usb_suspend_debounce++;
            if (usb_suspend_debounce >= 20) {
                f_goto_sleep = 1;
            }
        } else {
            usb_suspend_debounce = 0;
            if (g_config.usb_sleep_toggle && no_act_time >= sleep_time_delay) {
                f_goto_sleep = 1;
            } else {
                f_goto_sleep = 0;
            }
        }
    }
#if (WORK_MODE == THREE_MODE)
    else if (no_act_time >= sleep_time_delay) {
        f_goto_sleep = 1;
    } else if (rf_linking_time >= LINK_TIMEOUT_ALT) {
        rf_linking_time = 0;
        f_goto_sleep    = 1;
    } else if (dev_info.rf_state == RF_DISCONNECT) {
        rf_disconnect_time++;
        if (rf_disconnect_time > 5 * 20) { // 5 seconds
            f_goto_sleep = 1;
        }
    } else if (dev_info.rf_state == RF_CONNECT) {
        rf_disconnect_time = 0;
    }
#endif
}
