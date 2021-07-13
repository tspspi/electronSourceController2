#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>

#define PWM_VPERDIV 3.1914893617
#define PWM_VPERUA 0.979959039479

#include "./sysclock.h"
#include "./pwmout.h"

#ifdef __cplusplus
    extern "C" {
#endif

/*
    Clock source will be timer2

    Running with one of the following settings:
            Prescaler               Frequency               OCR1A
            /64                     250 kHz                 2 <--
            /256                    62.5 kHz                1
            /1024                   15.625 kHz              1
*/
#define PWM_TIMERTICK_PRESCALER             0x06
#define PWM_TIMERTICK_OVERFLOWVAL           0x01
#define PWM_WAKEUP_SLEEP_TICKS              10

uint16_t pwmoutOnCycles[8];
static uint16_t pwmoutCurrentCycles[8];

ISR(TIMER2_COMPA_vect) {
    uint8_t i;

    for(i = 0; i < sizeof(pwmoutCurrentCycles)/sizeof(uint16_t); i=i+1) {
        pwmoutCurrentCycles[i] = (pwmoutCurrentCycles[i] + 1) % 1024;
        if(pwmoutCurrentCycles[i] >= pwmoutOnCycles[i]) {
            if(i == 0) { PORTL = PORTL & ~(0x80); }
            else if(i == 1) { PORTL = PORTL & ~(0x40); }

            else if(i == 2) { PORTL = PORTL & ~(0x20); }
            else if(i == 3) { PORTL = PORTL & ~(0x10); }

            else if(i == 4) { PORTL = PORTL & ~(0x08); }
            else if(i == 5) { PORTL = PORTL & ~(0x04); }

            else if(i == 6) { PORTL = PORTL & ~(0x02); }
            else if(i == 7) { PORTL = PORTL & ~(0x01); }
        } else {
            if(i == 0) { PORTL = PORTL | (0x80); }
            else if(i == 1) { PORTL = PORTL | (0x40); }

            else if(i == 2) { PORTL = PORTL | (0x20); }
            else if(i == 3) { PORTL = PORTL | (0x10); }

            else if(i == 4) { PORTL = PORTL | (0x08); }
            else if(i == 5) { PORTL = PORTL | (0x04); }

            else if(i == 6) { PORTL = PORTL | (0x02); }
            else if(i == 7) { PORTL = PORTL | (0x01); }
        }
    }
}

void pwmoutInit() {
    uint8_t i;
    uint8_t sregOld = SREG;

    #ifndef FRAMAC_SKIP
        cli();
    #endif

    for(i = 0; i < sizeof(pwmoutCurrentCycles)/sizeof(uint16_t); i=i+1) {
        pwmoutCurrentCycles[i] = 0;
        pwmoutOnCycles[i] = 0;
    }

    TCNT2   = 0;
    TCCR2A  = 0x02;      /* CTC mode (up to OCR2A, disable OCR output pins) */
    OCR2A   = 1;          /* Count up to one - one int per tick */
    TIMSK2  = 0x02;      /* Set OCIE2A flag to enable interrupts on output compare */
    TCCR2B  = PWM_TIMERTICK_PRESCALER;

    SREG = sregOld;
}

void setPSUVolts(
    uint16_t v,
    uint8_t psu
) {
    uint16_t dutyCycleOn = (uint16_t)(((double)v) / PWM_VPERDIV);

    switch(psu) {
        case 1:         pwmoutOnCycles[0] = dutyCycleOn; break;
        case 2:         pwmoutOnCycles[2] = dutyCycleOn; break;
        case 3:         pwmoutOnCycles[4] = dutyCycleOn; break;
        case 4:         pwmoutOnCycles[6] = dutyCycleOn; break;
        default:        return;
    }
}

void setPSUMicroamps(
    uint16_t ua,
    uint8_t psu
) {
    uint16_t dutyCycleOn = (uint16_t)(((double)ua) / PWM_VPERUA);

    switch(psu) {
        case 1:         pwmoutOnCycles[1] = dutyCycleOn; break;
        case 2:         pwmoutOnCycles[3] = dutyCycleOn; break;
        case 3:         pwmoutOnCycles[5] = dutyCycleOn; break;
        case 4:         pwmoutOnCycles[7] = dutyCycleOn; break;
        default:        return;
    }
}

#ifdef __cplusplus
    } /* extern "C" { */
#endif
