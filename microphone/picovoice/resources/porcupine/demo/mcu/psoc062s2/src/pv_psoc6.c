/*
    Copyright 2021 Picovoice Inc.

    You may not use this file except in compliance with the license. A copy of the license is located in the "LICENSE"
    file accompanying this source.

    Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on
    an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the
    specific language governing permissions and limitations under the License.
*/

#include "pv_psoc6.h"

#include "cy_pdl.h"
#include "cy_retarget_io.h"
#include "cy_rgb_led.h"
#include "cybsp.h"
#include "cyhal.h"

#define CPU_CLOCK_150MHZ (150000000)
#define UUID_SIZE (8)

static uint8_t uuid[UUID_SIZE];

cyhal_clock_t   pll_cpu_clock;
cyhal_clock_t   cpu_clock;

static cy_rslt_t cpu_clock_init(void) {
    cy_rslt_t result;
    result = cyhal_clock_get(&pll_cpu_clock, &CYHAL_CLOCK_PLL[0]);

    if (result == CY_RSLT_SUCCESS) {
        result = cyhal_clock_init(&pll_cpu_clock);
    }

    if (result == CY_RSLT_SUCCESS) {
        result = cyhal_clock_set_frequency(&pll_cpu_clock, CPU_CLOCK_150MHZ, NULL);
    }

    if (result == CY_RSLT_SUCCESS) {
        result = cyhal_clock_set_enabled(&pll_cpu_clock, true, true);
    }

    if (result == CY_RSLT_SUCCESS) {
        result = cyhal_clock_get(&cpu_clock, &CYHAL_CLOCK_HF[0]);
    }

    if (result == CY_RSLT_SUCCESS) {
        result = cyhal_clock_init(&cpu_clock);
    }

    if (result == CY_RSLT_SUCCESS) {
        result = cyhal_clock_set_source(&cpu_clock, &pll_cpu_clock);
    }

    return result;
}

pv_status_t pv_board_init(void) {
    cy_rslt_t result;

    result = cybsp_init();
    if (result != CY_RSLT_SUCCESS) {
        return PV_STATUS_INVALID_STATE;
    }

    result = cpu_clock_init();
    if (result != CY_RSLT_SUCCESS) {
        return PV_STATUS_INVALID_STATE;
    }

    result = cy_rgb_led_init(CYBSP_LED_RGB_RED, CYBSP_LED_RGB_GREEN, CYBSP_LED_RGB_BLUE, CY_RGB_LED_ACTIVE_LOW);
    if (result != CY_RSLT_SUCCESS) {
        return PV_STATUS_INVALID_STATE;
    }

    __enable_irq();

    uint64_t board_uuid = Cy_SysLib_GetUniqueId();
    memcpy(uuid, (const uint8_t *)&board_uuid, UUID_SIZE);


    return PV_STATUS_SUCCESS;
}

pv_status_t pv_message_init(void) {
    cy_rslt_t result;

    result = cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, CY_RETARGET_IO_BAUDRATE);
    if (result != CY_RSLT_SUCCESS) {
        return PV_STATUS_INVALID_STATE;
    }

    return PV_STATUS_SUCCESS;
}

void pv_board_deinit() {
}

const uint8_t *pv_get_uuid(void) {
   return (const uint8_t *) uuid;
}

const uint32_t pv_get_uuid_size(void) {
   return UUID_SIZE;
}

void pv_error_handler(void) {
    __disable_irq();
    while(true);
}
