/*!
    \file    voice_i2s_out.h
    \brief   I2S playback helper for the Wi-Fi voice demo.
*/

#ifndef _VOICE_I2S_OUT_H_
#define _VOICE_I2S_OUT_H_

#include <stdint.h>

int voice_i2s_out_start(void);
void voice_i2s_out_reset(void);
uint32_t voice_i2s_out_push(const uint8_t *data, uint32_t len);
uint32_t voice_i2s_out_push_16k_mono_as_48k(const uint8_t *data, uint32_t len);
uint32_t voice_i2s_out_buffered(void);
uint32_t voice_i2s_out_dropped(void);
uint32_t voice_i2s_out_underruns(void);

#endif /* _VOICE_I2S_OUT_H_ */
