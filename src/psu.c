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

/*@
    assigns psuStates[0 .. 3].limitMode;
    assigns psuStates[0 .. 3].realV;
    assigns psuStates[0 .. 3].realI;

    ensures \forall int i; 0 <= i <= 3 ==>
        ((psuStates[i].limitMode == psuLimit_Voltage) || (psuStates[i].limitMode == psuLimit_Current))
        && (psuStates[i].realV == currentADC[i * 2])
        && (psuStates[i].realI == currentADC[i * 2 + 1]);
*/
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

/*@
    requires \forall int i; 0 < i < 3 ==>
        ((psuStates[i].bOutputEnable == true) || (psuStates[i].bOutputEnable == false))
        && ((psuStates[i].polPolarity == psuPolarity_Positive) || (psuStates[i].polPolarity == psuPolarity_Negative));

    assigns PORTA;
    assigns PORTC;

    ensures ((PORTA & 0x01) == 0x01) && (psuStates[0].bOutputEnable == true) || (((PORTA & 0x01) == 0) && (psuStates[0].bOutputEnable != true));
    ensures ((PORTA & 0x10) == 0x10) && (psuStates[1].bOutputEnable == true) || (((PORTA & 0x10) == 0) && (psuStates[1].bOutputEnable != true));
    ensures ((PORTC & 0x80) == 0x80) && (psuStates[2].bOutputEnable == true) || (((PORTC & 0x80) == 0) && (psuStates[2].bOutputEnable != true));
    ensures ((PORTC & 0x08) == 0x08) && (psuStates[3].bOutputEnable == true) || (((PORTC & 0x08) == 0) && (psuStates[3].bOutputEnable != true));

    ensures ((PORTA & 0x02) == 0x02) && (psuStates[0].polPolarity != psuPolarity_Positive) || (((PORTA & 0x02) == 0) && (psuStates[0].polPolarity == psuPolarity_Positive));
    ensures ((PORTA & 0x20) == 0x20) && (psuStates[1].polPolarity != psuPolarity_Positive) || (((PORTA & 0x20) == 0) && (psuStates[1].polPolarity == psuPolarity_Positive));
    ensures ((PORTC & 0x40) == 0x40) && (psuStates[2].polPolarity != psuPolarity_Positive) || (((PORTC & 0x40) == 0) && (psuStates[2].polPolarity == psuPolarity_Positive));
    ensures ((PORTC & 0x04) == 0x04) && (psuStates[2].polPolarity != psuPolarity_Positive) || (((PORTC & 0x04) == 0) && (psuStates[2].polPolarity == psuPolarity_Positive));
*/
void psuSetOutputs() {
    uint8_t currentPortA = PORTA;
    uint8_t currentPortC = PORTC;

    uint8_t i;

    /*@
        loop invariant 0 < i < 5;
        loop assigns currentPortA;
        loop assigns currentPortC;
        loop variant 4-i;
    */
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

/*@
    requires (psuIndex >= 0) && (psuIndex < 4);
    requires \forall int i; 0 < i < 3 ==>
        ((psuStates[i].bOutputEnable == true) || (psuStates[i].bOutputEnable == false))
        && ((psuStates[i].polPolarity == psuPolarity_Positive) || (psuStates[i].polPolarity == psuPolarity_Negative));

    assigns PORTA;
    assigns PORTC;
*/
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

/*@
    assigns SREG;
    assigns DDRA;
    assigns DDRC;
    assigns DDRL;
    assigns DDRD;
    assigns PORTA;
    assigns PORTC;
    assigns PORTL;
    assigns PORTD;

    assigns psuStates[0 .. 3].bOutputEnable;
    assigns psuStates[0 .. 3].polPolarity;
    assigns psuStates[0 .. 3].setVTarget;
    assigns psuStates[0 .. 3].setILimit;

    ensures \forall int i; 0 < i < 3 ==>
        (psuStates[i].bOutputEnable == false)
        && (psuStates[i].polPolarity == psuPolarity_Negative)
        && (psuStates[i].setVTarget == 0)
        && (psuStates[i].setILimit == 0);
    ensures (psuStates[3].bOutputEnable == false)
        && (psuStates[3].setVTarget == 0)
        && (psuStates[3].setILimit == 0)
        && (psuStates[3].polPolarity == psuPolarity_Positive);
*/
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
