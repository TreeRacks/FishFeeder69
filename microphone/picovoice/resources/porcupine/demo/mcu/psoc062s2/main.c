/*
    Copyright 2021 Picovoice Inc.

    You may not use this file except in compliance with the license. A copy of the license is located in the "LICENSE"
    file accompanying this source.

    Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on
    an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the
    specific language governing permissions and limitations under the License.
*/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "cy_rgb_led.h"
#include "cybsp.h"

#include "pv_audio_rec.h"
#include "pv_keywords.h"
#include "pv_porcupine.h"
#include "pv_psoc6.h"

#define MEMORY_BUFFER_SIZE (70 * 1024)

static const char* ACCESS_KEY = ... // AccessKey string obtained from Picovoice Console (https://picovoice.ai/console/)

static int8_t memory_buffer[MEMORY_BUFFER_SIZE] __attribute__((aligned(16)));

#ifdef __PV_LANGUAGE_ENGLISH__

static const int32_t NUM_KEYWORDS = 4;
static const int32_t KEYWORD_MODEL_SIZES[] = {
        sizeof(DEFAULT_KEYWORD_ARRAY),
        sizeof(PICOVOICE_KEYWORD_ARRAY),
        sizeof(BUMBLEBEE_KEYWORD_ARRAY),
        sizeof(ALEXA_KEYWORD_ARRAY)
};
static const void *KEYWORD_MODELS[] = {
        DEFAULT_KEYWORD_ARRAY,
        PICOVOICE_KEYWORD_ARRAY,
        BUMBLEBEE_KEYWORD_ARRAY,
        ALEXA_KEYWORD_ARRAY
};
static const float SENSITIVITIES[] = {
        0.75f,
        0.75f,
        0.75f,
        0.75f
};

static const char *KEYWORDS_NAME[] = {
        "Porcupine",
        "Picovoice",
        "Bumblebee",
        "Alexa"
};

#else

static const int32_t NUM_KEYWORDS = 1;
static const int32_t KEYWORD_MODEL_SIZES[] = { sizeof(DEFAULT_KEYWORD_ARRAY) };
static const void *KEYWORD_MODELS[] = { DEFAULT_KEYWORD_ARRAY };
static const float SENSITIVITIES[] = { 0.75f };
static const char *KEYWORDS_NAME[] = { "" };

#endif

static void error_handler(void) {
    cy_rgb_led_on(CY_RGB_LED_COLOR_RED, CY_RGB_LED_MAX_BRIGHTNESS);
    while(true);
}

int main(void) {

    pv_status_t status = pv_board_init();
    if (status != PV_STATUS_SUCCESS) {
        error_handler();
    }

    status = pv_message_init();
    if (status != PV_STATUS_SUCCESS) {
        error_handler();
    }

    const uint8_t *board_uuid = pv_get_uuid();
    printf("UUID: ");
    for (uint32_t i = 0; i < pv_get_uuid_size(); i++) {
        printf(" %.2x", board_uuid[i]);
    }
    printf("\r\n");

    status = pv_audio_rec_init();
    if (status != PV_STATUS_SUCCESS) {
        printf("Audio init failed with '%s'\r\n", pv_status_to_string(status));
        error_handler();
    }

    status = pv_audio_rec_start();
    if (status != PV_STATUS_SUCCESS) {
        printf("Recording audio failed with '%s'\r\n", pv_status_to_string(status));
        error_handler();
    }

    pv_porcupine_t *handle = NULL;

    status = pv_porcupine_init(
            ACCESS_KEY,
            MEMORY_BUFFER_SIZE,
            memory_buffer,
            NUM_KEYWORDS,
            KEYWORD_MODEL_SIZES,
            KEYWORD_MODELS,
            SENSITIVITIES,
            &handle);

    if (status != PV_STATUS_SUCCESS) {
        printf("Porcupine init failed with '%s'\r\n", pv_status_to_string(status));
        error_handler();
    }

    while (true) {
        const int16_t *buffer = pv_audio_rec_get_new_buffer();
        if (buffer) {
            int32_t keyword_index = -1;
            const pv_status_t status = pv_porcupine_process(handle, buffer, &keyword_index);
            if (status != PV_STATUS_SUCCESS) {
                printf("Porcupine process failed with '%s'\r\n", pv_status_to_string(status));
                error_handler();
            }
            if (keyword_index >= 0) {
                printf("[wake word] %s\r\n", KEYWORDS_NAME[keyword_index]);

                switch (keyword_index) {
                    case 0:
                        cy_rgb_led_on(CY_RGB_LED_COLOR_GREEN, CY_RGB_LED_MAX_BRIGHTNESS);
                        break;
                    case 1:
                        cy_rgb_led_on(CY_RGB_LED_COLOR_CYAN, CY_RGB_LED_MAX_BRIGHTNESS);
                        break;
                    case 2:
                        cy_rgb_led_on(CY_RGB_LED_COLOR_RED, CY_RGB_LED_MAX_BRIGHTNESS);
                        break;
                    case 3:
                        cy_rgb_led_on(CY_RGB_LED_COLOR_BLUE, CY_RGB_LED_MAX_BRIGHTNESS);
                        break;
                    default:
                        break;
                }
                Cy_SysLib_Delay(500);
                cy_rgb_led_off();
            }
        }

    }
    pv_board_deinit();
    pv_audio_rec_deinit();
    pv_porcupine_delete(handle);
}
