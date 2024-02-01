/*
    Copyright 2021 Picovoice Inc.

    You may not use this file except in compliance with the license. A copy of the license is located in the "LICENSE"
    file accompanying this source.

    Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on
    an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the
    specific language governing permissions and limitations under the License.
*/

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "cybsp.h"
#include "cyhal.h"

#include "picovoice.h"

#define PV_AUDIO_REC_AUDIO_FREQUENCY    (16000U)
#define PV_AUDIO_REC_CHANNEL_NUMBER     (1)
#define PV_AUDIO_REC_RECORD_BUFFER_SIZE (512)
#define DECIMATION_RATE                 (96u)
#define AUDIO_SYS_CLOCK_HZ              (24576000u)
#define PDM_DATA                        (P10_5)
#define PDM_CLK                         (P10_4)

void pdm_pcm_isr_handler(void *arg, cyhal_pdm_pcm_event_t event);

cyhal_pdm_pcm_t pdm_pcm;
cyhal_clock_t   audio_clock;
cyhal_clock_t   pll_clock;

const cyhal_pdm_pcm_cfg_t pdm_pcm_cfg =
{
    .sample_rate     = PV_AUDIO_REC_AUDIO_FREQUENCY,
    .decimation_rate = DECIMATION_RATE,
    .mode            = CYHAL_PDM_PCM_MODE_LEFT,
    .word_length     = 16,  /* bits */
    .left_gain       = 21,   /* dB */
    .right_gain      = 21,   /* dB */
};

static int16_t ping_pong_buffer[2][PV_AUDIO_REC_RECORD_BUFFER_SIZE];
static int32_t last_read_index = -1;
static int32_t read_index = -1;
static int32_t write_index = 1;

struct {
    uint32_t channel_number;
    uint32_t audio_frequency;
    uint32_t record_buffer_size;
    bool is_recording;
} pv_audio_rec;

cy_rslt_t pdm_pcm_clock_init(void) {
    cy_rslt_t result;
    result = cyhal_clock_get(&pll_clock, &CYHAL_CLOCK_PLL[1]);

    if (result == CY_RSLT_SUCCESS) {
        result = cyhal_clock_init(&pll_clock);
    }

    if (result == CY_RSLT_SUCCESS) {
        result = cyhal_clock_set_frequency(&pll_clock, AUDIO_SYS_CLOCK_HZ, NULL);
    }

    if (result == CY_RSLT_SUCCESS) {
        result = cyhal_clock_set_enabled(&pll_clock, true, true);
    }

    if (result == CY_RSLT_SUCCESS) {
        result = cyhal_clock_get(&audio_clock, &CYHAL_CLOCK_HF[1]);
    }

    if (result == CY_RSLT_SUCCESS) {
        result = cyhal_clock_init(&audio_clock);
    }

    if (result == CY_RSLT_SUCCESS) {
        result = cyhal_clock_set_source(&audio_clock, &pll_clock);
    }

    if (result == CY_RSLT_SUCCESS) {
        result = cyhal_clock_set_enabled(&audio_clock, true, true);
    }

    return result;
}

pv_status_t pv_audio_rec_init(void) {
    cy_rslt_t result;
    result = pdm_pcm_clock_init();
    if (result != CY_RSLT_SUCCESS) {
        return PV_STATUS_INVALID_STATE;
    }

    result = cyhal_pdm_pcm_init(&pdm_pcm, PDM_DATA, PDM_CLK, &audio_clock, &pdm_pcm_cfg);
    if (result != CY_RSLT_SUCCESS) {
        return PV_STATUS_INVALID_STATE;
    }

    cyhal_pdm_pcm_register_callback(&pdm_pcm, pdm_pcm_isr_handler, NULL);
    cyhal_pdm_pcm_enable_event(&pdm_pcm, CYHAL_PDM_PCM_ASYNC_COMPLETE, CYHAL_ISR_PRIORITY_DEFAULT, true);

    pv_audio_rec.channel_number = PV_AUDIO_REC_CHANNEL_NUMBER;
    pv_audio_rec.audio_frequency = PV_AUDIO_REC_AUDIO_FREQUENCY;
    pv_audio_rec.record_buffer_size = PV_AUDIO_REC_RECORD_BUFFER_SIZE;

    return PV_STATUS_SUCCESS;
}

pv_status_t pv_audio_rec_start(void) {
    cyhal_pdm_pcm_start(&pdm_pcm);

    cyhal_pdm_pcm_read_async(&pdm_pcm, &ping_pong_buffer[write_index][0], PV_AUDIO_REC_RECORD_BUFFER_SIZE);

    pv_audio_rec.is_recording = true;
    return PV_STATUS_SUCCESS;
}

pv_status_t pv_audio_rec_stop(void) {
    cyhal_pdm_pcm_stop(&pdm_pcm);
    pv_audio_rec.is_recording = false;
    return PV_STATUS_SUCCESS;
}

float pv_audio_rec_loudness_db(void) {
    if (read_index == -1) {
        return -FLT_MAX;
    }

    if (pv_audio_rec.is_recording) {
        int64_t sum = 0;
        const int16_t *pcm_buffer = (const int16_t *) ping_pong_buffer[read_index];
        for (uint32_t i = 0; i < PV_AUDIO_REC_RECORD_BUFFER_SIZE; i++) {
          sum += (int64_t) pcm_buffer[i] * (int64_t) pcm_buffer[i];
        }
        return (10 * log10f( ((float) sum + FLT_EPSILON) / ( PV_AUDIO_REC_RECORD_BUFFER_SIZE) / (SHRT_MAX * SHRT_MAX)));
    } else {
        return -FLT_MAX;
    }
}

const int16_t *pv_audio_rec_get_new_buffer(void) {
    if (read_index == -1) {
        return NULL;
    }

    if (last_read_index == read_index) {
        return NULL;
    }

    last_read_index = read_index;

    return ping_pong_buffer[read_index];
}

void pdm_pcm_isr_handler(void *arg, cyhal_pdm_pcm_event_t event) {
    (void) arg;
    (void) event;

    cyhal_pdm_pcm_read_async(&pdm_pcm, &ping_pong_buffer[write_index][0], PV_AUDIO_REC_RECORD_BUFFER_SIZE);
    read_index = write_index;
    write_index = (write_index == 1) ? 0 : 1;
}

void pv_audio_rec_deinit(void) {
    cyhal_clock_free(&audio_clock);
    cyhal_clock_free(&pll_clock);
}
