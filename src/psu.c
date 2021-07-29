#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>

#include "./sysclock.h"
#include "./psu.h"
#include "./adc.h"

#ifdef __cplusplus
    extern "C" {
#endif

struct psuState psuStates[4];

void psuUpdateMeasuredState() {
    psuStates[0].limitMode = ((PINA & 0x04) == 0) ? psuLimit_Voltage : psuLimit_Current;
    psuStates[0].realV = currentADC[0];
    psuStates[0].realI = currentADC[1];

    psuStates[1].limitMode = ((PINA & 0x40) == 0) ? psuLimit_Voltage : psuLimit_Current;
    psuStates[1].realV = currentADC[2];
    psuStates[1].realI = currentADC[3];

    psuStates[2].limitMode = ((PINC & 0x20) == 0) ? psuLimit_Voltage : psuLimit_Current;
    psuStates[2].realV = currentADC[4];
    psuStates[2].realI = currentADC[5];

    psuStates[3].limitMode = ((PINC & 0x02) == 0) ? psuLimit_Voltage : psuLimit_Current;
    psuStates[3].realV = currentADC[6];
    psuStates[3].realI = currentADC[7];
}

void psuSetOutputs() {
    uint8_t currentPortA = PORTA;
    uint8_t currentPortC = PORTC;

    uint8_t i;

    for(i = 0; i < 4; i=i+1) {
        switch(i) {
            case 0:
            {
                if(psuStates[0].bOutputEnable == true) {
                    currentPortA = currentPortA | 0x01;
                } else {
                    currentPortA = currentPortA & (~(0x01));
                }

                if(psuStates[0].polPolarity == psuPolarity_Positive) {
                    currentPortA = currentPortA & (~(0x02));
                } else {
                    currentPortA = currentPortA | 0x02;
                }
                break;
            }
            case 1:
            {
                if(psuStates[1].bOutputEnable == true) {
                    currentPortA = currentPortA | 0x10;
                } else {
                    currentPortA = currentPortA & (~(0x10));
                }

                if(psuStates[1].polPolarity == psuPolarity_Positive) {
                    currentPortA = currentPortA & (~(0x20));
                } else {
                    currentPortA = currentPortA | 0x20;
                }
                break;
            }
            case 2:
            {
                if(psuStates[2].bOutputEnable == true) {
                    currentPortC = currentPortC | 0x80;
                } else {
                    currentPortC = currentPortC & (~(0x80));
                }

                if(psuStates[2].polPolarity == psuPolarity_Positive) {
                    currentPortC = currentPortC & (~(0x40));
                } else {
                    currentPortC = currentPortC | 0x40;
                }
                break;
            }
            case 3:
            {
                if(psuStates[3].bOutputEnable == true) {
                    currentPortC = currentPortC | 0x08;
                } else {
                    currentPortC = currentPortC & (~(0x08));
                }

                if(psuStates[3].polPolarity == psuPolarity_Positive) {
                    currentPortC = currentPortC & (~(0x04));
                } else {
                    currentPortC = currentPortC | 0x04;
                }
                break;
            }
            default:
                return;
        }
    }

    PORTA = currentPortA;
    PORTC = currentPortC;
}

void psuSetOutput(int psuIndex) {
    uint8_t currentPortA = PORTA;
    uint8_t currentPortC = PORTC;

    switch(psuIndex) {
        case 0:
        {
            if(psuStates[0].bOutputEnable == true) {
                currentPortA = currentPortA | 0x01;
            } else {
                currentPortA = currentPortA & (~(0x01));
            }

            if(psuStates[0].polPolarity == psuPolarity_Positive) {
                currentPortA = currentPortA & (~(0x02));
            } else {
                currentPortA = currentPortA | 0x02;
            }
            break;
        }
        case 1:
        {
            if(psuStates[1].bOutputEnable == true) {
                currentPortA = currentPortA | 0x10;
            } else {
                currentPortA = currentPortA & (~(0x10));
            }

            if(psuStates[1].polPolarity == psuPolarity_Positive) {
                currentPortA = currentPortA & (~(0x20));
            } else {
                currentPortA = currentPortA | 0x20;
            }
            break;
        }
        case 2:
        {
            if(psuStates[2].bOutputEnable == true) {
                currentPortC = currentPortC | 0x80;
            } else {
                currentPortC = currentPortC & (~(0x80));
            }

            if(psuStates[2].polPolarity == psuPolarity_Positive) {
                currentPortC = currentPortC & (~(0x40));
            } else {
                currentPortC = currentPortC | 0x40;
            }
            break;
        }
        case 3:
        {
            if(psuStates[3].bOutputEnable == true) {
                currentPortC = currentPortC | 0x08;
            } else {
                currentPortC = currentPortC & (~(0x08));
            }

            if(psuStates[3].polPolarity == psuPolarity_Positive) {
                currentPortC = currentPortC & (~(0x04));
            } else {
                currentPortC = currentPortC | 0x04;
            }
            break;
        }
        default:
            return;
    }

    PORTA = currentPortA;
    PORTC = currentPortC;
}

void psuInit() {
    unsigned long int i;
    uint8_t oldSREG = SREG;

    #ifndef FRAMAC_SKIP
        cli();
    #endif

    DDRA = 0x33;    PORTA = 0x22;
    DDRC = 0xCC;    PORTC = 0x40;
    DDRL = 0xFF;    PORTL = 0x00;
    DDRD = 0x80;    PORTD = 0x00;

    /* Set PSU states structures */
    for(i = 0; i < 3; i=i+1) {
        psuStates[i].bOutputEnable      = false;
        psuStates[i].polPolarity        = psuPolarity_Negative;
        psuStates[i].setVTarget         = 0;
        psuStates[i].setILimit          = 0;
    }

    psuStates[3].bOutputEnable      = false;
    psuStates[3].polPolarity        = psuPolarity_Positive;
    psuStates[3].setVTarget         = 0;
    psuStates[3].setILimit          = 0;

    SREG = oldSREG;

    /* We just wait for around 2 ms till all analog measurements are done ... */
    delay(2);

    psuUpdateMeasuredState();
}

#ifdef __cplusplus
    } /* extern "C" { */
#endif
