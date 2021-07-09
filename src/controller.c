#include <avr/io.h>
#include <avr/interrupt.h>
#include <math.h>
#include <util/twi.h>
#include <stdint.h>

#include "./serial.h"
#include "./controller.h"
#include "./sysclock.h"
#include "./adc.h"

int main() {
    #ifndef FRAMAC_SKIP
		cli();
	#endif

    /*
        Initialize state (power on reset)
    */

    /*
        PORTA & PORTC:
            PSU digital in and out
        PORTL:
            Analog out for PSUs
        PORTD:
            PD7: Filament power on/off
        PORTB:
            PB7: Onboard LED (OC0A)
    */
    DDRA = 0x33;
    DDRC = 0xCC;
    DDRL = 0xFF;
    DDRD = 0x80;
    DDRB = DDRB | 0x80;

    /*
        Disable all powersupplies and filament (sane safe values)
    */

    /*
        Load voltage values from EEPROM
    */

    /* Clear debug LED */
    PORTB = PORTB & 0x7F;

    /* Setup system clock */
    sysclockInit();

    /*
        Setup serial
            USART0 is used to communicate via serial or USB interface
    */
    serialInit0();

    #ifndef FRAMAC_SKIP
		sei();
	#endif

    /*
        INitialize ADCs:
            * Free running mode
            * MUX set to ADC0 for first conversion
            * prescaler 128 (125 kHz ADC frequency -> 10 kHz sampling frqeuency
                with 8 channels for 4 PSUs -> little bit more than 1 kHz sampling
                of all voltages and currents - more than sufficient)
    */
    adcInit();


    for(;;) {
        /*
            This is the main application loop. It works in a hybrid synchronous
            and asynchronous way. ISRs are usually only used as a data pump
            into queues, any actions are taken by this main loop - of course
            this won't satisfy any timing constraints but there is no need
            for such constraints during slow control of the experiment
        */
        handleSerial0Messages(); /* main external serial interface */
    }
}
