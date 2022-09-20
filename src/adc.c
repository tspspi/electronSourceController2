#include <avr/io.h>
#include <avr/interrupt.h>
#include <math.h>
#include <util/twi.h>
#include <stdint.h>

#include "./controller.h"
#include "./adc.h"

#ifdef __cplusplus
    extern "C" {
#endif

/*
    The ISR simply switches to the next MUX (the next conversion should already
    be running) and stores the current measured value in our own shadow registers.
*/
static uint8_t adcCurrentMux;

#ifndef ADC_CHANNELS16
    uint16_t currentADC[8];
    #define ADC_CHANNEL_COUNT 8
#else
    uint16_t currentADC[16];
    #define ADC_CHANNEL_COUNT 16
#endif

/*@
    assigns currentADC[0 .. ADC_CHANNEL_COUNT-1];
    assigns ADMUX;
    assigns ADCSRB;
*/
ISR(ADC_vect) {
    #ifndef ADC_CHANNELS16
        uint8_t oldMUX = ADMUX;

        currentADC[(oldMUX + 7) & 0x07] = ADC;

        ADMUX = (((oldMUX & 0x1F) + 1) & 0x07) | (oldMUX & 0xE0);
    #else
        uint8_t oldMUX = ADMUX;
        uint8_t oldADCSRB = ADCSRB;

        uint8_t adcIndex = (((oldMUX & 0x07) | (oldADCSRB & 0x08)) + 15) & 0x0F;

        currentADC[adcIndex] = ADC;

        ADMUX = ((adcIndex + 2) & 0x07) | (oldMUX & 0xE0);
        ADCSRB = (oldADCSRB & 0xF7) | ((adcIndex + 2) & 0x08);
    #endif
}

/*@
    assigns PRR0;
    assigns ADMUX;
    assigns ADCSRB;
    assigns ADCSRA;
    assigns SREG;

    ensures (PRR0 & 0x01) == 0;
    ensures (ADMUX == 0x40);
    ensures (ADCSRB == 0x00);
    ensures ADCSRA == 0xFF;
*/
void adcInit() {
    unsigned long int i;

    uint8_t oldSREG = SREG;
    #ifndef FRAMAC_SKIP
        cli();
    #endif

    adcCurrentMux = 0;

    for(i = 0; i < sizeof(currentADC) / sizeof(uint16_t); i=i+1) {
        currentADC[i] = ~0;
    }

    PRR0 = PRR0 & ~(0x01); /* Disable power saving features for ADC */
    ADMUX = 0x40; /* AVCC reference voltage, MUX 0, right aligned */
    ADCSRB = 0x00; /* Free running trigger mode, highest mux bit 0 */
    ADCSRA = 0xBF; /* Prescaler / 128 -> about 1 kHz measurement effective for all PSUs, interrupt enable; ADCs currently DISABLED */

    SREG = oldSREG;

    /* Launch ADC ... */
    ADCSRA = ADCSRA | 0x40; /* Start first conversion ... */
}

#ifdef __cplusplus
    } /* extern "C" { */
#endif
