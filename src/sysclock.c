#include <avr/io.h>
#include <avr/interrupt.h>
#include <math.h>
#include <util/twi.h>

/*
    ================
    = System clock =
    ================
*/

#define SYSCLK_TIMER_OVERFLOW_MICROS                    (64L * 256L * (F_CPU / 1000000L))
#define SYSCLK_MILLI_INCREMENT                          (SYSCLK_TIMER_OVERFLOW_MICROS / 1000)
#define SYSCLK_MILLIFRACT_INCREMENT                     ((SYSCLK_TIMER_OVERFLOW_MICROS % 1000) >> 3)
#define SYSCLK_MILLIFRACT_MAXIMUM                       (1000 >> 3)

static volatile unsigned long int systemMillis                 = 0;
static volatile unsigned long int systemMilliFractional        = 0;
static volatile unsigned long int systemMonotonicOverflowCnt   = 0;

/*@
        assigns systemMillis, systemMilliFractional, systemMonotonicOverflowCnt;
*/

ISR(TIMER0_OVF_vect) {
        unsigned long int m, f;

        m = systemMillis;
        f = systemMilliFractional;

        m = m + SYSCLK_MILLI_INCREMENT;
        f = f + SYSCLK_MILLIFRACT_INCREMENT;

        if(f >= SYSCLK_MILLIFRACT_MAXIMUM) {
                f = f - SYSCLK_MILLIFRACT_MAXIMUM;
                m = m + 1;
        }

        systemMonotonicOverflowCnt = systemMonotonicOverflowCnt + 1;

        systemMillis = m;
        systemMilliFractional = f;
}

unsigned long int micros() {
        uint8_t srOld = SREG;
        unsigned long int overflowCounter;
        unsigned long int timerCounter;

        #ifndef FRAMAC_SKIP
                cli();
        #endif
        overflowCounter = systemMonotonicOverflowCnt;
        timerCounter = TCNT0;

        /*
                Check for pending overflow that has NOT been handeled up to now
        */
        if(((TIFR0 & 0x01) != 0) && (timerCounter < 255)) {
                overflowCounter = overflowCounter + 1;
        }

        SREG = srOld;

        return ((overflowCounter << 8) + timerCounter) * (64L / (F_CPU / 1000000L));
}

void delay(unsigned long millisecs) {
        //uint16_t lastMicro;
        unsigned int lastMicro;
        /*
                Busy waiting the desired amount of milliseconds ... by
                polling mircos
        */
        lastMicro = (unsigned int)micros();
        /*@
                loop assigns lastMicro;
                loop assigns millisecs;
        */
        while(millisecs > 0) {
                // uint16_t curMicro = (uint16_t)micros();
                unsigned int curMicro = micros();
                if(curMicro - lastMicro >= 1000)  {
                        /* Every ~ thousand microseconds tick ... */
                        lastMicro = lastMicro + 1000;
                        millisecs = millisecs - 1;
                }
        }
        return;
}

void sysclockInit() {
    TCCR0B = 0x00;          /* Disable timer 0 */
    TCNT0  = 0x00;          /* Reset counter */

    TCCR0A = 0x00;
    TIFR0  = 0x01;          /* Clear overflow interrupt flag if triggered before */
    TIMSK0 = 0x01;          /* Enable overflow interrupt */
    TCCR0B = 0x03;          /* /64 prescaler */
}
