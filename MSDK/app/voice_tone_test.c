/*!
    \file    voice_tone_test.c
    \brief   Feed a repeated local test tone into the I2S playback ring.
*/

#include <stdint.h>
#include <stdio.h>
#include "app_cfg.h"

#ifdef CONFIG_VOICE_TONE_TEST

#include "wrapper_os.h"
#include "voice_i2s_out.h"
#include "voice_tone_test.h"

#define VOICE_TONE_TASK_STACK       768
#define VOICE_TONE_TASK_PRIO        OS_TASK_PRIORITY(2)
#define VOICE_TONE_SAMPLE_RATE      48000
#define VOICE_TONE_CHUNK_SAMPLES    256
#define VOICE_TONE_AMPLITUDE        22000
#define VOICE_TONE_LOG_INTERVAL     250
#define VOICE_TONE_HIGH_WATER       (12 * 1024)

static os_task_t voice_tone_task_handle = NULL;

static const int16_t tone_1khz[48] = {
    0, 2872, 5695, 8422, 11000, 13391, 15556, 17457,
    19053, 20326, 21259, 21837, VOICE_TONE_AMPLITUDE, 21837, 21259, 20326,
    19053, 17457, 15556, 13391, 11000, 8422, 5695, 2872,
    0, -2872, -5695, -8422, -11000, -13391, -15556, -17457,
    -19053, -20326, -21259, -21837, -VOICE_TONE_AMPLITUDE, -21837, -21259, -20326,
    -19053, -17457, -15556, -13391, -11000, -8422, -5695, -2872,
};

static void voice_tone_fill(uint8_t *buf, uint32_t samples, uint32_t *phase,
                            uint8_t enabled)
{
    uint32_t i;

    for (i = 0; i < samples; i++) {
        int16_t sample = enabled ? tone_1khz[*phase] : 0;
        buf[i * 2] = (uint8_t)((uint16_t)sample & 0xFF);
        buf[i * 2 + 1] = (uint8_t)(((uint16_t)sample >> 8) & 0xFF);
        *phase = (*phase + 1) % 48;
    }
}

static void voice_tone_task(void *param)
{
    uint8_t pcm[VOICE_TONE_CHUNK_SAMPLES * 2];
    uint32_t phase = 0;
    uint32_t ticks = 0;

    (void)param;

    if (voice_i2s_out_start() != 0) {
        printf("voice tone: i2s start failed\r\n");
        voice_tone_task_handle = NULL;
        sys_task_delete(NULL);
    }

    printf("voice tone: local 1kHz test tone started\r\n");

    while (1) {
        uint8_t on = 1;

        while (voice_i2s_out_buffered() > VOICE_TONE_HIGH_WATER) {
            sys_ms_sleep(2);
        }

        voice_tone_fill(pcm, VOICE_TONE_CHUNK_SAMPLES, &phase, on);
        voice_i2s_out_push(pcm, sizeof(pcm));

        if ((ticks % VOICE_TONE_LOG_INTERVAL) == 0) {
            printf("voice tone: buffered=%lu dropped=%lu underrun=%lu\r\n",
                   (unsigned long)voice_i2s_out_buffered(),
                   (unsigned long)voice_i2s_out_dropped(),
                   (unsigned long)voice_i2s_out_underruns());
        }

        ticks++;
        sys_ms_sleep((VOICE_TONE_CHUNK_SAMPLES * 1000) / VOICE_TONE_SAMPLE_RATE);
    }
}

int voice_tone_test_start(void)
{
    if (voice_tone_task_handle != NULL) {
        return 0;
    }

    voice_tone_task_handle = sys_task_create_dynamic((const uint8_t *)"voice_tone",
            VOICE_TONE_TASK_STACK, VOICE_TONE_TASK_PRIO, voice_tone_task, NULL);
    if (voice_tone_task_handle == NULL) {
        printf("voice tone: create task failed\r\n");
        return -1;
    }

    return 0;
}

#endif /* CONFIG_VOICE_TONE_TEST */
