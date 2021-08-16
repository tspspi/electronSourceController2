#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>

#define PWM_VPERDIV 3.1914893617
#define PWM_VPERUA 0.979959039479
#define PWM_FILA_VPERDIV 0.224609375

#include "./sysclock.h"
#include "./pwmout.h"

#ifdef __cplusplus
    extern "C" {
#endif

/*
    Clock source will be timer2

    Running with one of the following settings:
            Prescaler               Frequency               OCR1A           Prescaler val
            /1                      8 MHz                   -               0x01
            /8                      1 MHz                   -               0x02
            /32                     500 kHz                 1               0x03
            /64                     250 kHz                 16              0x04
            /128                    125 kHz                 1               0x05
            /256                    62.5 kHz                1               0x06
            /1024                   15.625 kHz              1               0x07

    Values that seem to work +- relieable:
        Prescaler 0x04 (/64 -> 250 KHz), Overflow counter 0x10 (16) -> PWM coutner freq. 15.625 kHz)
*/
#define PWM_TIMERTICK_PRESCALER             0x06
#define PWM_TIMERTICK_OVERFLOWVAL           0x02

uint16_t pwmoutOnCycles[9];
static uint16_t pwmoutCurrentCycles[9];

ISR(TIMER2_COMPA_vect) {
    uint8_t i;

    for(i = 0; i < sizeof(pwmoutCurrentCycles)/sizeof(uint16_t)-1; i=i+1) {
        pwmoutCurrentCycles[i] = (pwmoutCurrentCycles[i] + 1) & 0x3FF;
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

    pwmoutCurrentCycles[8] = (pwmoutCurrentCycles[8] + 1) & 0x7F;
    if(pwmoutCurrentCycles[8] >= pwmoutOnCycles[8]) {
        PORTD = PORTD & ~(0x80);
    } else {
        PORTD = PORTD | 0x80;
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
    OCR2A   = PWM_TIMERTICK_OVERFLOWVAL; /* Count up to one - one int per tick */
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

#if 0
void setFilamentVoltage(
    uint16_t volts
) {
    uint16_t dutyCycleOn = (uint16_t)(((double)volts) / PWM_FILA_VPERDIV);
    pwmoutOnCycles[8] = dutyCycleOn/8;
}
#endif

void setFilamentPWM(
    uint16_t pwmCycles
) {
    /* Setting in 128 steps from 0-12V -> ~ 0.09V resolution */
    pwmoutOnCycles[8] = pwmCycles & 0x7F;
}

#ifdef __cplusplus
    } /* extern "C" { */
#endif
