#ifndef __is_included__a2cd9f9e_e0c1_11eb_a444_b499badf00a1
#define __is_included__a2cd9f9e_e0c1_11eb_a444_b499badf00a1 1

#ifndef __cplusplus
    #ifndef true
        #define true 1
        #define false 0
        typedef unsigned char bool;
    #endif
#endif

enum psuPolarity {
    psuPolarity_Positive,
    psuPolarity_Negative
};

enum limitingMode {
    psuLimit_Current,
    psuLimit_Voltage
};

struct psuState {
    bool                    bOutputEnable;
    enum psuPolarity        polPolarity;
    uint16_t                setVTarget;
    uint16_t                setILimit;

    /* Sensing */
    enum limitingMode       limitMode;
    uint16_t                realV;
    uint16_t                realI;
    #if 0
        enum psuPolarity        realPolarity;
    #endif
};

#ifdef __cplusplus
    extern "C" {
#endif

extern struct psuState psuStates[4];

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
void psuInit();

/*@
    requires (psuIndex >= 0) && (psuIndex < 4);
    requires \forall int i; 0 < i < 3 ==>
        ((psuStates[i].bOutputEnable == true) || (psuStates[i].bOutputEnable == false))
        && ((psuStates[i].polPolarity == psuPolarity_Positive) || (psuStates[i].polPolarity == psuPolarity_Negative));

    assigns PORTA;
    assigns PORTC;
*/
void psuSetOutput(int psuIndex);

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
void psuSetOutputs();

/*@
    assigns psuStates[0 .. 3].limitMode;
    assigns psuStates[0 .. 3].realV;
    assigns psuStates[0 .. 3].realI;

    ensures \forall int i; 0 <= i <= 3 ==>
        ((psuStates[i].limitMode == psuLimit_Voltage) || (psuStates[i].limitMode == psuLimit_Current))
        && (psuStates[i].realV >= 0)
        && (psuStates[i].realV < 1024)
        && (psuStates[i].realI >= 0)
        && (psuStates[i].realI < 1024);
*/
void psuUpdateMeasuredState();

#ifdef __cplusplus
    } /* extern "C" { */
#endif

#endif /* #ifndef __is_included__a2cd9f9e_e0c1_11eb_a444_b499badf00a1 */
