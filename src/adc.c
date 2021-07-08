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
#else
    uint16_t currentADC[16];
#endif

ISR(ADC_vect) {
    #ifndef ADC_CHANNELS16
        uint8_t oldMUX = ADMUX;
        ADMUX = (((oldMUX & 0x1F) + 1) & 0x1F) | (oldMUX & 0xE0);

        oldMUX = oldMUX & 0x1F; /* Channel index */
        currentADC[oldMUX] = ADC;
    #else

    #endif
}

void adcInit() {
    uint8_t oldSREG = SREG;
    #ifndef FRAMAC_SKIP
        cli();
    #endif

    adcCurrentMux = 0;

    ADMUX = 0x40; /* AVCC reference voltage, MUX 0, right aligned */
    ADCSRB = 0x00; /* Free running trigger mode, highest mux bit 0 */
    ADCSRA = 0x2F; /* Prescaler / 128 -> about 1 kHz measurement effective for all PSUs, interrupt enable; ADCs currently DISABLED */

    SREG = oldSREG;

    /* Launch ADC ... */
    ADCSRA = ADCSRA | 0xC0; /* Enable and start ... */
}

#ifdef __cplusplus
    } /* extern "C" { */
#endif
