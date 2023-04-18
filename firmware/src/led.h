/*
    \file   led.h

    \brief  led header file.

    (c) 2018 Microchip Technology Inc. and its subsidiaries.

    Subject to your compliance with these terms, you may use Microchip software and any
    derivatives exclusively with Microchip products. It is your responsibility to comply with third party
    license terms applicable to your use of third party software (including open source software) that
    may accompany Microchip software.

    THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
    EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY
    IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS
    FOR A PARTICULAR PURPOSE.

    IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
    INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
    WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP
    HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO
    THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL
    CLAIMS IN ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT
    OF FEES, IF ANY, THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS
    SOFTWARE.
*/


#ifndef LED_H_
#define LED_H_
#include <stdint.h>
#include <stdbool.h>
#include "definitions.h"

#define LED_ON  false
#define LED_OFF true


#define LED_FLAG_EMPTY 0

typedef enum
{

    LED_GREEN

} led_number_t;

typedef union
{
    struct
    {
        uint16_t led_state : 1;
        uint16_t reserved : 15;
    };
    uint16_t as_uint16;
} led_change_t;

typedef union
{
    struct
    {
        uint16_t status : 3;
    };
    uint16_t as_uint16;
} led_state_t;

typedef struct led
{
    led_change_t change_flag;
    led_state_t  state_flag;
} led_status_t;

typedef enum
{
    LED_STATE_OFF        = 0,
    LED_STATE_HOLD       = (1 << 0),
    LED_STATE_BLINK_FAST = (1 << 1),
    LED_STATE_BLINK_SLOW = (1 << 2),
    LED_STAT_MAX         = INT16_MAX
} led_set_state_t;

typedef enum
{
    LED_INDICATOR_OFF     = 0,
    LED_INDICATOR_PENDING = (1 << 0),
    LED_INDICATOR_SUCCESS = (1 << 1),
    LED_INDICATOR_ERROR   = (1 << 2)
} led_indicator_name_t;

void LED_test(void);
void LED_init(void);
void LED_SetCloud(led_indicator_name_t state);

#endif /* LED_H_ */
