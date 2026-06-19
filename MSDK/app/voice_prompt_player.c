/*!
    \file    voice_prompt_player.c
    \brief   Local voice prompt playback for the voice demo.
*/

#include <stdint.h>
#include <stdio.h>
#include "app_cfg.h"

#ifdef CONFIG_VOICE_PROMPT_DANGER

#include "wrapper_os.h"
#include "voice_i2s_out.h"
#include "voice_prompt_danger.h"
#include "voice_prompt_player.h"

#define VOICE_PROMPT_TASK_STACK       768
#define VOICE_PROMPT_TASK_PRIO        OS_TASK_PRIORITY(2)
#define VOICE_PROMPT_CHUNK_BYTES      512
#define VOICE_PROMPT_HIGH_WATER       (12 * 1024)
#define VOICE_PROMPT_IDLE_DELAY_MS    20
#define VOICE_PROMPT_STARTUP_DELAY_MS 120

static os_task_t voice_prompt_task_handle = NULL;
static volatile uint32_t voice_prompt_pending = 0;

static void voice_prompt_wait_for_space(void)
{
    while (voice_i2s_out_buffered() > VOICE_PROMPT_HIGH_WATER) {
        sys_ms_sleep(2);
    }
}

void voice_prompt_player_play_once(void)
{
    uint32_t offset = 0;
    uint32_t pcm_bytes = voice_prompt_danger_sample_count * 2;

    printf("voice prompt: playing danger area warning, %lu samples @ %lu Hz\r\n",
           (unsigned long)voice_prompt_danger_sample_count,
           (unsigned long)voice_prompt_danger_sample_rate);

    while (offset < pcm_bytes) {
        uint32_t chunk = pcm_bytes - offset;

        if (chunk > VOICE_PROMPT_CHUNK_BYTES) {
            chunk = VOICE_PROMPT_CHUNK_BYTES;
        }

        voice_prompt_wait_for_space();
        if (voice_prompt_danger_sample_rate == 48000) {
            voice_i2s_out_push(&voice_prompt_danger_pcm[offset], chunk);
        } else {
            voice_i2s_out_push_16k_mono_as_48k(&voice_prompt_danger_pcm[offset], chunk);
        }
        offset += chunk;
    }
}

static void voice_prompt_task(void *param)
{
    (void)param;

    if (voice_prompt_danger_sample_rate != 16000 &&
        voice_prompt_danger_sample_rate != 48000) {
        printf("voice prompt: unsupported prompt sample rate %lu\r\n",
               (unsigned long)voice_prompt_danger_sample_rate);
        voice_prompt_task_handle = NULL;
        sys_task_delete(NULL);
        return;
    }

    if (voice_i2s_out_start() != 0) {
        printf("voice prompt: i2s start failed\r\n");
        voice_prompt_task_handle = NULL;
        sys_task_delete(NULL);
        return;
    }

    sys_ms_sleep(VOICE_PROMPT_STARTUP_DELAY_MS);
    printf("voice prompt: local danger warning ready\r\n");

    while (1) {
        if (voice_prompt_pending != 0) {
            voice_prompt_pending = 0;
            voice_prompt_player_play_once();
        } else {
            sys_ms_sleep(VOICE_PROMPT_IDLE_DELAY_MS);
        }
    }
}

int voice_prompt_player_start(void)
{
    if (voice_prompt_task_handle != NULL) {
        return 0;
    }

    voice_prompt_task_handle = sys_task_create_dynamic((const uint8_t *)"voice_prompt",
            VOICE_PROMPT_TASK_STACK, VOICE_PROMPT_TASK_PRIO, voice_prompt_task, NULL);
    if (voice_prompt_task_handle == NULL) {
        printf("voice prompt: create task failed\r\n");
        return -1;
    }

    voice_prompt_pending = 1;

    return 0;
}

void voice_prompt_player_trigger(void)
{
    voice_prompt_pending = 1;
}

#endif /* CONFIG_VOICE_PROMPT_DANGER */
