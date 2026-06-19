/*!
    \file    voice_i2s_out.c
    \brief   Buffered PCM16 mono to I2S stereo playback.
*/

#include <stdint.h>
#include <stdio.h>
#include "app_cfg.h"

#ifdef CONFIG_VOICE_DEMO

#include "wrapper_os.h"
#include "spi_i2s/spi_i2s.h"
#include "voice_i2s_out.h"

#define VOICE_SAMPLE_RATE              48000
#define VOICE_RING_BYTES               (16 * 1024)
#define VOICE_DMA_WORDS                512
#define VOICE_DMA_QUEUE_DEPTH          4
#define VOICE_AUDIO_TASK_STACK         768
#define VOICE_AUDIO_TASK_PRIO          OS_TASK_PRIORITY(1)
#define VOICE_UPSAMPLE_CHUNK_BYTES     384

static os_mutex_t voice_ring_mutex = NULL;
static os_queue_t voice_dma_queue = NULL;
static os_task_t voice_audio_task_handle = NULL;

static uint8_t voice_ring[VOICE_RING_BYTES];
static uint32_t voice_ring_rpos;
static uint32_t voice_ring_wpos;
static uint32_t voice_ring_used;

static uint16_t voice_dma_buf0[VOICE_DMA_WORDS];
static uint16_t voice_dma_buf1[VOICE_DMA_WORDS];
static uint8_t voice_upsample_buf[VOICE_UPSAMPLE_CHUNK_BYTES * 3];

static uint32_t voice_drop_bytes;
static uint32_t voice_underrun_samples;

static uint32_t voice_ring_free_locked(void)
{
    return VOICE_RING_BYTES - voice_ring_used;
}

static void voice_ring_drop_oldest_locked(uint32_t len)
{
    if (len > voice_ring_used) {
        len = voice_ring_used;
    }

    voice_ring_rpos = (voice_ring_rpos + len) % VOICE_RING_BYTES;
    voice_ring_used -= len;
    voice_drop_bytes += len;
}

static uint32_t voice_ring_write_locked(const uint8_t *data, uint32_t len)
{
    uint32_t first;
    uint32_t free_len = voice_ring_free_locked();

    if (len > VOICE_RING_BYTES) {
        data += len - VOICE_RING_BYTES;
        voice_drop_bytes += len - VOICE_RING_BYTES;
        len = VOICE_RING_BYTES;
        free_len = voice_ring_free_locked();
    }

    if (len > free_len) {
        voice_ring_drop_oldest_locked(len - free_len);
    }

    first = VOICE_RING_BYTES - voice_ring_wpos;
    if (first > len) {
        first = len;
    }
    sys_memcpy(&voice_ring[voice_ring_wpos], data, first);
    if (len > first) {
        sys_memcpy(voice_ring, data + first, len - first);
    }

    voice_ring_wpos = (voice_ring_wpos + len) % VOICE_RING_BYTES;
    voice_ring_used += len;

    return len;
}

static int16_t voice_ring_read_sample_locked(void)
{
    uint8_t lo;
    uint8_t hi;

    if (voice_ring_used < 2) {
        voice_underrun_samples++;
        return 0;
    }

    lo = voice_ring[voice_ring_rpos];
    voice_ring_rpos = (voice_ring_rpos + 1) % VOICE_RING_BYTES;
    hi = voice_ring[voice_ring_rpos];
    voice_ring_rpos = (voice_ring_rpos + 1) % VOICE_RING_BYTES;
    voice_ring_used -= 2;

    return (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
}

static void voice_fill_dma_buffer(uint16_t *buf)
{
    uint32_t idx;

    sys_mutex_get(&voice_ring_mutex);
    for (idx = 0; idx < VOICE_DMA_WORDS; idx += 2) {
        int16_t sample = voice_ring_read_sample_locked();
        uint16_t word = (uint16_t)sample;
        buf[idx] = word;
        buf[idx + 1] = word;
    }
    sys_mutex_put(&voice_ring_mutex);
}

static void voice_audio_task(void *param)
{
    pcm_buf_info_t info;

    (void)param;

    spi_i2s_init_config();
    if (spi_i2s_init_sample_rate(VOICE_SAMPLE_RATE) != 0) {
        printf("voice: i2s sample rate init failed\r\n");
        voice_audio_task_handle = NULL;
        sys_task_delete(NULL);
    }

    voice_fill_dma_buffer(voice_dma_buf0);
    voice_fill_dma_buffer(voice_dma_buf1);
    spi_i2s_start_send(voice_dma_queue, (uint32_t)voice_dma_buf0,
                       (uint32_t)voice_dma_buf1, VOICE_DMA_WORDS);

    printf("voice: i2s playback started, pcm16 mono %u Hz\r\n", VOICE_SAMPLE_RATE);

    while (1) {
        if (sys_queue_fetch(&voice_dma_queue, &info, 0, 1) == OS_OK) {
            if (info.pcm_addr == 0) {
                voice_fill_dma_buffer(voice_dma_buf0);
            } else {
                voice_fill_dma_buffer(voice_dma_buf1);
            }
        }
    }
}

int voice_i2s_out_start(void)
{
    if (voice_audio_task_handle != NULL) {
        return 0;
    }

    if (voice_ring_mutex == NULL && sys_mutex_init(&voice_ring_mutex) != OS_OK) {
        printf("voice: create ring mutex failed\r\n");
        return -1;
    }

    if (voice_dma_queue == NULL &&
        sys_queue_init(&voice_dma_queue, VOICE_DMA_QUEUE_DEPTH, sizeof(pcm_buf_info_t)) != OS_OK) {
        printf("voice: create dma queue failed\r\n");
        return -1;
    }

    voice_audio_task_handle = sys_task_create_dynamic((const uint8_t *)"voice_audio",
            VOICE_AUDIO_TASK_STACK, VOICE_AUDIO_TASK_PRIO, voice_audio_task, NULL);
    if (voice_audio_task_handle == NULL) {
        printf("voice: create audio task failed\r\n");
        return -1;
    }

    return 0;
}

void voice_i2s_out_reset(void)
{
    if (voice_ring_mutex == NULL) {
        return;
    }

    sys_mutex_get(&voice_ring_mutex);
    voice_ring_rpos = 0;
    voice_ring_wpos = 0;
    voice_ring_used = 0;
    sys_mutex_put(&voice_ring_mutex);
}

uint32_t voice_i2s_out_push(const uint8_t *data, uint32_t len)
{
    uint32_t written;

    if (voice_ring_mutex == NULL || data == NULL || len == 0) {
        return 0;
    }

    if (len & 1) {
        len--;
    }

    sys_mutex_get(&voice_ring_mutex);
    written = voice_ring_write_locked(data, len);
    sys_mutex_put(&voice_ring_mutex);

    return written;
}

uint32_t voice_i2s_out_push_16k_mono_as_48k(const uint8_t *data, uint32_t len)
{
    uint32_t total_written = 0;
    uint32_t offset = 0;

    if (data == NULL || len == 0) {
        return 0;
    }

    if (len & 1) {
        len--;
    }

    while (offset < len) {
        uint32_t chunk = len - offset;
        uint32_t out = 0;
        uint32_t i;

        if (chunk > VOICE_UPSAMPLE_CHUNK_BYTES) {
            chunk = VOICE_UPSAMPLE_CHUNK_BYTES;
        }
        if (chunk & 1) {
            chunk--;
        }

        for (i = 0; i < chunk; i += 2) {
            int16_t s0 = (int16_t)((uint16_t)data[offset + i] |
                         ((uint16_t)data[offset + i + 1] << 8));
            int16_t s1;
            int32_t interp;

            if (i + 3 < chunk) {
                s1 = (int16_t)((uint16_t)data[offset + i + 2] |
                     ((uint16_t)data[offset + i + 3] << 8));
            } else if (offset + chunk < len) {
                s1 = (int16_t)((uint16_t)data[offset + chunk] |
                     ((uint16_t)data[offset + chunk + 1] << 8));
            } else {
                s1 = s0;
            }

            interp = s0;
            voice_upsample_buf[out++] = (uint8_t)interp;
            voice_upsample_buf[out++] = (uint8_t)((uint16_t)interp >> 8);

            interp = ((int32_t)2 * s0 + s1) / 3;
            voice_upsample_buf[out++] = (uint8_t)interp;
            voice_upsample_buf[out++] = (uint8_t)((uint16_t)interp >> 8);

            interp = (s0 + (int32_t)2 * s1) / 3;
            voice_upsample_buf[out++] = (uint8_t)interp;
            voice_upsample_buf[out++] = (uint8_t)((uint16_t)interp >> 8);
        }

        total_written += voice_i2s_out_push(voice_upsample_buf, out);
        offset += chunk;
    }

    return total_written;
}

uint32_t voice_i2s_out_buffered(void)
{
    uint32_t buffered;

    if (voice_ring_mutex == NULL) {
        return 0;
    }

    sys_mutex_get(&voice_ring_mutex);
    buffered = voice_ring_used;
    sys_mutex_put(&voice_ring_mutex);

    return buffered;
}

uint32_t voice_i2s_out_dropped(void)
{
    return voice_drop_bytes;
}

uint32_t voice_i2s_out_underruns(void)
{
    return voice_underrun_samples;
}

#endif /* CONFIG_VOICE_DEMO */
