/*!
    \file    voice_danger_key_sim.c
    \brief   Simulate an incoming H759 danger signal with the VW553K-START button.
*/

#include <stdint.h>
#include <stdio.h>
#include "app_cfg.h"

#ifdef CONFIG_VOICE_DANGER_KEY_SIM

#include "gd32vw55x.h"
#include "wrapper_os.h"
#include "voice_danger_key_sim.h"
#include "voice_prompt_player.h"

#define DANGER_KEY_TASK_STACK          512
#define DANGER_KEY_TASK_PRIO           OS_TASK_PRIORITY(2)
#define DANGER_KEY_POLL_MS             10
#define DANGER_KEY_DEBOUNCE_MS         50

/*
 * Use BOOT0/PC8 as the local active-low simulator input. Do not reconfigure
 * SW2/UartDownload on PA15 here, because PA15 is shared with the JTAG path.
 */
#define DANGER_KEY_GPIO                GPIOC
#define DANGER_KEY_PIN                 GPIO_PIN_8

static os_task_t danger_key_task_handle = NULL;

static void danger_key_gpio_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOC);
    gpio_mode_set(DANGER_KEY_GPIO, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, DANGER_KEY_PIN);
}

static uint8_t danger_key_is_pressed(void)
{
    return gpio_input_bit_get(DANGER_KEY_GPIO, DANGER_KEY_PIN) == RESET;
}

static void danger_key_wait_released(void)
{
    uint32_t released_ms = 0;

    while (released_ms < DANGER_KEY_DEBOUNCE_MS) {
        if (danger_key_is_pressed()) {
            released_ms = 0;
        } else {
            released_ms += DANGER_KEY_POLL_MS;
        }

        sys_ms_sleep(DANGER_KEY_POLL_MS);
    }
}

static void danger_key_task(void *param)
{
    uint32_t pressed_ms = 0;

    (void)param;

    danger_key_gpio_init();
    printf("voice danger sim: BOOT0/PC8 active-low input ready, waiting release\r\n");
    danger_key_wait_released();
    printf("voice danger sim: armed\r\n");

    while (1) {
        if (danger_key_is_pressed()) {
            pressed_ms += DANGER_KEY_POLL_MS;
            if (pressed_ms >= DANGER_KEY_DEBOUNCE_MS) {
                printf("voice danger sim: danger signal triggered by local key\r\n");
                voice_prompt_player_play_once();
                danger_key_wait_released();
                pressed_ms = 0;
                printf("voice danger sim: re-armed\r\n");
            } else {
                sys_ms_sleep(DANGER_KEY_POLL_MS);
            }
        } else {
            pressed_ms = 0;
            sys_ms_sleep(DANGER_KEY_POLL_MS);
        }
    }
}

int voice_danger_key_sim_start(void)
{
    if (danger_key_task_handle != NULL) {
        return 0;
    }

    danger_key_task_handle = sys_task_create_dynamic((const uint8_t *)"danger_key",
            DANGER_KEY_TASK_STACK, DANGER_KEY_TASK_PRIO, danger_key_task, NULL);
    if (danger_key_task_handle == NULL) {
        printf("voice danger sim: create task failed\r\n");
        return -1;
    }

    return 0;
}

#endif /* CONFIG_VOICE_DANGER_KEY_SIM */
