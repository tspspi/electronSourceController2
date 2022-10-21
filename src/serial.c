#include <avr/io.h>
#include <avr/interrupt.h>
#include <math.h>
#include <util/twi.h>
#include <stdint.h>
#include <string.h>

#include "./serial.h"
#include "./controller.h"
#include "./sysclock.h"
#include "./adc.h"
#include "./psu.h"
#include "./pwmout.h"

static volatile unsigned long int dwFilament__SetCurrent;
static volatile bool bFilament__EnableCurrent;

/*
    ADC counts to current or voltage:

        HCP 14-6500:        max. 6.5 kV, 2 mA <-> 0...10V

        0...5V <--> 3.25 kV, 1 mA
        5V <-> 1024 ADC counts
        1 ADC count <-> 0.0048828125V
            0.0048828125 <-> 0.003173828125 kV = 3.1738 V
                         <-> 0.0009765625 mA = 0.9765625 uA
*/
/*@
    requires (adcCounts >= 0) && (adcCounts < 1024);
    assigns \nothing;
    ensures \result == (uint16_t)(3.221407 * adcCounts);
*/
static inline uint16_t serialADC2VoltsHCP(
    uint16_t adcCounts
) {
    return (uint16_t)((double)(adcCounts) * 3.1738 * 1.015);
}
/*@
    requires (adcCounts >= 0) && (adcCounts < 1024);
    assigns \nothing;
    ensures \result == (uint16_t)(9.765625 * adcCounts);
*/
static inline uint16_t serialADC2TenthMicroampsHCP(
    uint16_t adcCounts
) {
    return (uint16_t)((double)(adcCounts) * 9.765625);
}
static inline uint16_t serialADC2MilliampsFILA(
    uint16_t adcCounts
) {
    uint16_t deviation;
    if(adcCounts < 512) {
        deviation = 512 - adcCounts;
    } else {
        deviation = adcCounts - 512;
    }
    return (uint16_t)(((double)deviation) * 4.8828125);
}

/*
    Ringbuffer utilis
*/
/*@
    predicate acsl_serialbuffer_valid(struct ringBuffer* lpBuf) =
        \valid(lpBuf)
        && \valid(&(lpBuf->dwHead))
        && \valid(&(lpBuf->dwTail))
        && (lpBuf->dwHead >= 0) && (lpBuf->dwHead < SERIAL_RINGBUFFER_SIZE)
        && (lpBuf->dwTail >= 0) && (lpBuf->dwTail < SERIAL_RINGBUFFER_SIZE);
*/
/*@
    requires lpBuf != NULL;
    requires \valid(lpBuf);
    requires \valid(&(lpBuf->dwHead));
    requires \valid(&(lpBuf->dwTail));

    assigns lpBuf->dwHead;
    assigns lpBuf->dwTail;

    ensures lpBuf->dwHead == 0;
    ensures lpBuf->dwTail == 0;
    ensures acsl_serialbuffer_valid(lpBuf);
*/
static inline void ringBuffer_Init(volatile struct ringBuffer* lpBuf) {
    #ifndef FRAMAC_SKIP
        uint8_t oldSREG = SREG;
            cli();
    #endif
    lpBuf->dwHead = 0;
    lpBuf->dwTail = 0;
    #ifndef FRAMAC_SKIP
        SREG = oldSREG;
    #endif
}
/*@
    requires lpBuf != NULL;
    requires acsl_serialbuffer_valid(lpBuf);

    assigns \nothing;

    ensures (\result == true) && (\result == false);
    ensures acsl_serialbuffer_valid(lpBuf);

    behavior isDataAvailable:
        assumes lpBuf->dwHead != lpBuf->dwTail;
        ensures \result == true;
    behavior noDataAvailable:
        assumes lpBuf->dwHead == lpBuf->dwTail;
        ensures \result == false;

    disjoint behaviors isDataAvailable, noDataAvailable;
    complete behaviors isDataAvailable, noDataAvailable;
*/
static inline bool ringBuffer_Available(volatile struct ringBuffer* lpBuf) {
    bool res;
    #ifndef FRAMAC_SKIP
        uint8_t oldSREG = SREG;
        cli();
    #endif
    res = (lpBuf->dwHead != lpBuf->dwTail) ? true : false;
    #ifndef FRAMAC_SKIP
        SREG = oldSREG;
    #endif
    return res;
}
/*@
    requires lpBuf != NULL;
    requires acsl_serialbuffer_valid(lpBuf);

    assigns \nothing;

    ensures (\result == true) || (\result == false);
    ensures acsl_serialbuffer_valid(lpBuf);

    behavior noSpaceAvailable:
        assumes ((lpBuf->dwHead + 1) % SERIAL_RINGBUFFER_SIZE) == lpBuf->dwTail;
        ensures \result == false;
    behavior spaceAvailable:
        assumes ((lpBuf->dwHead + 1) % SERIAL_RINGBUFFER_SIZE) != lpBuf->dwTail;
        ensures \result == true;

    disjoint behaviors noSpaceAvailable, spaceAvailable;
    complete behaviors noSpaceAvailable, spaceAvailable;
*/
static inline bool ringBuffer_Writable(volatile struct ringBuffer* lpBuf) {
    bool res;
    #ifndef FRAMAC_SKIP
        uint8_t oldSREG = SREG;
        cli();
    #endif
    res = (((lpBuf->dwHead + 1) % SERIAL_RINGBUFFER_SIZE) != lpBuf->dwTail) ? true : false;
    #ifndef FRAMAC_SKIP
        SREG = oldSREG;
    #endif
    return res;
}
/*@
    requires lpBuf != NULL;
    requires acsl_serialbuffer_valid(lpBuf);

    assigns \nothing;

    ensures \result >= 0;
    ensures \result < SERIAL_RINGBUFFER_SIZE;
    ensures acsl_serialbuffer_valid(lpBuf);

    behavior noWrapping:
        assumes lpBuf->dwHead >= lpBuf->dwTail;
        ensures \result == (lpBuf->dwTail - lpBuf->dwHead);
    behavior wrapping:
        assumes lpBuf->dwHead < lpBuf->dwTail;
        ensures \result == (SERIAL_RINGBUFFER_SIZE - lpBuf->dwTail) + lpBuf->dwHead;

    disjoint behaviors noWrapping, wrapping;
    complete behaviors noWrapping, wrapping;
*/
static inline unsigned long int ringBuffer_AvailableN(volatile struct ringBuffer* lpBuf) {
    unsigned long int res;
    #ifndef FRAMAC_SKIP
        uint8_t oldSREG = SREG;
        cli();
    #endif

    if(lpBuf->dwHead >= lpBuf->dwTail) {
        res = lpBuf->dwHead - lpBuf->dwTail;
    } else {
        res = (SERIAL_RINGBUFFER_SIZE - lpBuf->dwTail) + lpBuf->dwHead;
    }

    #ifndef FRAMAC_SKIP
        SREG = oldSREG;
    #endif
    return res;
}
/*@
    requires lpBuf != NULL;
    requires acsl_serialbuffer_valid(lpBuf);

    assigns \nothing;

    ensures \result >= 0;
    ensures \result < SERIAL_RINGBUFFER_SIZE;
    ensures acsl_serialbuffer_valid(lpBuf);

    behavior noWrapping:
        assumes lpBuf->dwHead >= lpBuf->dwTail;
        ensures \result == SERIAL_RINGBUFFER_SIZE - (lpBuf->dwTail - lpBuf->dwHead) - 1;
    behavior wrapping:
        assumes lpBuf->dwHead < lpBuf->dwTail;
        ensures \result == SERIAL_RINGBUFFER_SIZE - ((SERIAL_RINGBUFFER_SIZE - lpBuf->dwTail) + lpBuf->dwHead) - 1;

    disjoint behaviors noWrapping, wrapping;
    complete behaviors noWrapping, wrapping;
*/
static inline unsigned long int ringBuffer_WriteableN(volatile struct ringBuffer* lpBuf) {
    return SERIAL_RINGBUFFER_SIZE - ringBuffer_AvailableN(lpBuf);
}
/*@
    requires lpBuf != NULL;
    requires acsl_serialbuffer_valid(lpBuf);

    ensures (\result >= 0) && (\result <= 0xFF);
    ensures acsl_serialbuffer_valid(lpBuf);

    behavior emptyBuffer:
        assumes lpBuf->dwHead == lpBuf->dwTail;

        assigns \nothing;

        ensures \result == 0x00;
        ensures lpBuf->dwTail == \old(lpBuf->dwTail);
    behavior availableData:
        assumes lpBuf->dwHead != lpBuf->dwTail;

        assigns lpBuf->dwTail;

        ensures \result == lpBuf->buffer[\old(lpBuf->dwTail)];
        ensures lpBuf->dwTail == ((\old(lpBuf->dwTail)+1) % SERIAL_RINGBUFFER_SIZE);

    disjoint behaviors emptyBuffer, availableData;
    complete behaviors emptyBuffer, availableData;
*/
static unsigned char ringBuffer_ReadChar(volatile struct ringBuffer* lpBuf) {
    char t;

    #ifndef FRAMAC_SKIP
        uint8_t oldSREG = SREG;
        cli();
    #endif

    if(lpBuf->dwHead == lpBuf->dwTail) {
        return 0x00;
    }

    t = lpBuf->buffer[lpBuf->dwTail];
    lpBuf->dwTail = (lpBuf->dwTail + 1) % SERIAL_RINGBUFFER_SIZE;

    #ifndef FRAMAC_SKIP
        SREG = oldSREG;
    #endif

    return t;
}
/*@
    requires lpBuf != NULL;
    requires acsl_serialbuffer_valid(lpBuf);

    assigns \nothing;

    ensures (\result >= 0) && (\result <= 0xFF);
    ensures acsl_serialbuffer_valid(lpBuf);
    ensures lpBuf->dwTail == \old(lpBuf->dwTail);

    behavior emptyBuffer:
        assumes lpBuf->dwHead == lpBuf->dwTail;
        assigns \nothing;
        ensures \result == 0x00;
    behavior availableData:
        assumes lpBuf->dwHead != lpBuf->dwTail;
        assigns \nothing;
        ensures \result == lpBuf->buffer[\old(lpBuf->dwTail)];

    disjoint behaviors emptyBuffer, availableData;
    complete behaviors emptyBuffer, availableData;
*/
static unsigned char ringBuffer_PeekChar(volatile struct ringBuffer* lpBuf) {
    unsigned char res = 0x00;

    #ifndef FRAMAC_SKIP
        uint8_t oldSREG = SREG;
        cli();
    #endif

    if(lpBuf->dwHead != lpBuf->dwTail) {
        res = lpBuf->buffer[lpBuf->dwTail];
    }

    #ifndef FRAMAC_SKIP
        SREG = oldSREG;
    #endif
    return res;
}

/*@
    requires lpBuf != NULL;
    requires acsl_serialbuffer_valid(lpBuf);
    requires (dwDistance < SERIAL_RINGBUFFER_SIZE);

    assigns \nothing;

    ensures (\result >= 0) && (\result <= 0xFF);
    ensures acsl_serialbuffer_valid(lpBuf);
    ensures lpBuf->dwTail == \old(lpBuf->dwTail);

    behavior emptyBuffer:
        assumes lpBuf->dwHead == lpBuf->dwTail;
        assigns \nothing;
        ensures \result == 0x00;
    behavior distanceInRange:
        assumes ((lpBuf->dwHead > lpBuf->dwTail) && (dwDistance < (lpBuf->dwTail - lpBuf->dwHead))) || ((lpBuf->dwHead < lpBuf->dwTail) && (dwDistance < (SERIAL_RINGBUFFER_SIZE - lpBuf->dwTail) + lpBuf->dwHead));
        assigns \nothing;
        ensures \result == lpBuf->buffer[(\old(lpBuf->dwTail) + dwDistance) % SERIAL_RINGBUFFER_SIZE];
    behavior availableData:
        assumes ((lpBuf->dwHead > lpBuf->dwTail) && (dwDistance >= (lpBuf->dwTail - lpBuf->dwHead))) || ((lpBuf->dwHead < lpBuf->dwTail) && (dwDistance >= (SERIAL_RINGBUFFER_SIZE - lpBuf->dwTail) + lpBuf->dwHead));
        assigns \nothing;
        ensures \result == 0x00;

    disjoint behaviors emptyBuffer, availableData;
    complete behaviors emptyBuffer, availableData;
*/
static unsigned char ringBuffer_PeekCharN(
    volatile struct ringBuffer* lpBuf,
    unsigned long int dwDistance
) {
    unsigned char res = 0x00;

    #ifndef FRAMAC_SKIP
        uint8_t oldSREG = SREG;
        cli();
    #endif

    if(lpBuf->dwHead != lpBuf->dwTail) {
        if(ringBuffer_AvailableN(lpBuf) > dwDistance) {
            res = lpBuf->buffer[(lpBuf->dwTail + dwDistance) % SERIAL_RINGBUFFER_SIZE];
        }
    }

    #ifndef FRAMAC_SKIP
        SREG = oldSREG;
    #endif

    return res; 
}
/*@
    requires lpBuf != NULL;
    requires acsl_serialbuffer_valid(lpBuf);
    requires ((lpBuf->dwHead > lpBuf->dwTail) && (dwCount <= (lpBuf->dwTail - lpBuf->dwHead))) || ((lpBuf->dwHead < lpBuf->dwTail) && (dwCount <= (SERIAL_RINGBUFFER_SIZE - lpBuf->dwTail) + lpBuf->dwHead));
    requires (dwCount >= 0);

    assigns lpBuf->dwTail;

    ensures lpBuf->dwTail == ((\old(lpBuf->dwTail) + dwCount) % SERIAL_RINGBUFFER_SIZE);
    ensures acsl_serialbuffer_valid(lpBuf);
*/
static inline void ringBuffer_discardN(
    volatile struct ringBuffer* lpBuf,
    unsigned long int dwCount
) {
    #ifndef FRAMAC_SKIP
        uint8_t oldSREG = SREG;
        cli();
    #endif
    lpBuf->dwTail = (lpBuf->dwTail + dwCount) % SERIAL_RINGBUFFER_SIZE;
    #ifndef FRAMAC_SKIP
        SREG = oldSREG;
    #endif
    return;
}
/*
    required lpBuf != NULL;
    requires \valid(lpOut);
    requires \valid(&(lpOut[0..dwLen]));
    requires acsl_serialbuffer_valid(lpBuf);
    requires dwLen < SERIAL_RINGBUFFER_SIZE;

    assigns lpOut[0 .. dwLen-1];

    ensures acsl_serialbuffer_valid(lpBuf);
    ensures (\result >= 0) && (\result <= dwLen);

    behavior notEnoughData:
        assumes ((dwLen > (lpBuf->dwTail - lpBuf->dwHead)) && (lpBuf->dwHead >= lpBuf->dwTail))
            || ((dwLen > ((SERIAL_RINGBUFFER_SIZE - lpBuf->dwTail) + lpBuf->dwHead)) && (lpBuf->dwHead < lpBuf->dwTail));
        assigns \nothing;
        ensures \result == 0
        
    behavior dataAvail:
        assumes ((dwLen <= (lpBuf->dwTail - lpBuf->dwHead)) && (lpBuf->dwHead >= lpBuf->dwTail))
            || ((dwLen <= ((SERIAL_RINGBUFFER_SIZE - lpBuf->dwTail) + lpBuf->dwHead)) && (lpBuf->dwHead < lpBuf->dwTail));

        assigns lpOut[0 .. dwLen-1];

        ensures \result == dwLen;
        ensures \forall int i; 0 <= i < dwLen ==>
            lpOut[i] == lpBuf->buffer((\old(lpBuf->lpTail) + i) % SERIAL_RINGBUFFER_SIZE);
        ensures \lpBuf->lpTail == (\old(lpBuf->lpTail) + dwLen) % SERIAL_RINGBUFFER_SIZE;

    disjoint behaviors notEnoughData, dataAvail;
    complete behaviors notEnoughData, dataAvail;
*/
static unsigned long int ringBuffer_ReadChars(
    volatile struct ringBuffer* lpBuf,
    unsigned char* lpOut,
    unsigned long int dwLen
) {
    char t;
    unsigned long int i = 0;

    #ifndef FRAMAC_SKIP
        uint8_t oldSREG = SREG;
        cli();
    #endif

    if(dwLen <= ringBuffer_AvailableN(lpBuf)) {
        /*@
            loop invariant 0 <= i <= dwLen;
            loop assigns lpOut[0 .. dwLen-1];
            loop variant dwLen - i;
        */
        for(i = 0; i < dwLen; i=i+1) {
            t = lpBuf->buffer[lpBuf->dwTail];
            lpBuf->dwTail = (lpBuf->dwTail + 1) % SERIAL_RINGBUFFER_SIZE;
            lpOut[i] = t;
        }
    }

    #ifndef FRAMAC_SKIP
        SREG = oldSREG;
    #endif

    return i;
}
/*@
    requires lpBuf != NULL;
    requires acsl_serialbuffer_valid(lpBuf);
    requires (bData >= 0) && (bData <= 0xFF);

    assigns lpBuf->buffer[\old(lpBuf->dwHead)];
    assigns lpBuf->dwHead;

    ensures acsl_serialbuffer_valid(lpBuf);

    behavior bufferAvail:
        assumes ((lpBuf->dwHead + 1) % SERIAL_RINGBUFFER_SIZE) != lpBuf->dwTail;
        assigns lpBuf->buffer[\old(lpBuf->dwHead)];
        assigns lpBuf->dwHead;
        ensures lpBuf->buffer[lpBuf->dwHead] == bData;
        ensures lpBuf->dwHead == (\old(lpBuf->dwHead) + 1) % SERIAL_RINGBUFFER_SIZE;
    behavior bufferFull:
        assumes ((lpBuf->dwHead + 1) % SERIAL_RINGBUFFER_SIZE) == lpBuf->dwTail;
        assigns \nothing;

    disjoint behaviors bufferAvail, bufferFull;
    complete behaviors bufferAvail, bufferFull;
*/
static void ringBuffer_WriteChar(
    volatile struct ringBuffer* lpBuf,
    unsigned char bData
) {
    #ifndef FRAMAC_SKIP
        uint8_t oldSREG = SREG;
        cli();
    #endif

    if(((lpBuf->dwHead + 1) % SERIAL_RINGBUFFER_SIZE) != lpBuf->dwTail) {
        lpBuf->buffer[lpBuf->dwHead] = bData;
        lpBuf->dwHead = (lpBuf->dwHead + 1) % SERIAL_RINGBUFFER_SIZE;
    }

    #ifndef FRAMAC_SKIP
        SREG = oldSREG;
    #endif
}
/*@
    requires lpBuf != NULL;
    requires acsl_serialbuffer_valid(lpBuf);
    requires \valid(bData);
    requires \valid(&(bData[0 .. dwLen]));

    assigns lpBuf->buffer[0 .. SERIAL_RINGBUFFER_SIZE];
    assigns lpBuf->dwHead;

    ensures acsl_serialbuffer_valid(lpBuf);
*/
static void ringBuffer_WriteChars(
    volatile struct ringBuffer* lpBuf,
    unsigned char* bData,
    unsigned long int dwLen
) {
    unsigned long int i;

    /*@
        loop invariant 0 <= i < dwLen;
        loop variant dwLen - i;
    */
    for(i = 0; i < dwLen; i=i+1) {
        ringBuffer_WriteChar(lpBuf, bData[i]);
    }
}
/*@
    requires lpBuf != NULL;
    requires \valid(lpBuf);
    requires acsl_serialbuffer_valid(lpBuf);
    requires (ui >= 0) && (ui < 4294967297);

    assigns lpBuf->buffer[0 .. SERIAL_RINGBUFFER_SIZE];
    assigns lpBuf->dwHead;

    ensures acsl_serialbuffer_valid(lpBuf);
*/
static void ringBuffer_WriteASCIIUnsignedInt(
    volatile struct ringBuffer* lpBuf,
    uint32_t ui
) {
    uint8_t i;
    char bTemp[10];
    uint8_t len;
    uint32_t current;

    /*
        We perform a simple conversion of the unsigned int
        and push all numbers into the ringbuffer
    */
    if(ui == 0) {
        ringBuffer_WriteChar(lpBuf, 0x30);
    } else {
        current = ui;
        len = 0;
        /*@
            loop invariant current >= 0;
            loop assigns bTemp[0 .. 9];
            loop assigns len;
            loop assigns current;
            loop variant current;
        */
        while(current != 0) {
            bTemp[len] = ((uint8_t)(current % 10)) + 0x30;
            len = len + 1;
            current = current / 10;
        }

        /*@
            loop invariant 0 <= i <= len;
            loop assigns lpBuf->buffer[0 .. SERIAL_RINGBUFFER_SIZE];
            loop variant len - i;
        */
        for(i = 0; i < len; i=i+1) {
            ringBuffer_WriteChar(lpBuf, bTemp[len - 1 - i]);
        }
    }
}

/*
    Serial handler (UART0)

    This is an full duplex serial communication bus that's accessible
    either via USB or used by an external ethernet controller.

    The main command protocol is spoken over this bus.
*/
volatile struct ringBuffer serialRB0_TX;
volatile struct ringBuffer serialRB0_RX;

static volatile int serialRXFlag; /* RX flag is set to indicate that new data has arrived */

/*@
    requires \valid(&UCSR0A) && \valid(&UCSR0B) && \valid(&SREG);

    assigns UCSR0A;
    assigns UCSR0B;

    ensures (UCSR0A & 0x40) == 0x40;
    ensures (UCSR0B & 0x28) == 0x28;
*/
void serialModeTX0() {
    /*
        This starts the transmitter ... other than for the RS485 half duplex
        lines it does _not_ interfer with the receiver part and would not
        interfer with an already running transmitter.
    */
    uint8_t sregOld = SREG;
    #ifndef FRAMAC_SKIP
        cli();
    #endif

    UCSR0A = UCSR0A | 0x40; /* Reset TXCn bit */
    UCSR0B = UCSR0B | 0x08 | 0x20;

    SREG = sregOld;
}
/*@
    requires \valid(&serialRB0_TX) && \valid(&serialRB0_RX);
    requires \valid(&UBRR0) && \valid(&UCSR0A) && \valid(&UCSR0B) && \valid(&UCSR0C);

    assigns UBRR0;
    assigns UCSR0A;
    assigns UCSR0B;
    assigns UCSR0C;

    ensures acsl_serialbuffer_valid(&serialRB0_TX);
    ensures acsl_serialbuffer_valid(&serialRB0_RX);
    ensures serialRXFlag == 0;
    ensures UBRR0 == 103;
    ensures (UCSR0A == 0x02)
        && (UCSR0B == 0x90)
        && (UCSR0C == 0x06);
*/
void serialInit0() {
    uint8_t sregOld = SREG;
    #ifndef FRAMAC_SKIP
        cli();
    #endif

    ringBuffer_Init(&serialRB0_TX);
    ringBuffer_Init(&serialRB0_RX);

    serialRXFlag = 0;

    UBRR0   = 103; // 16 : 115200, 103: 19200
    UCSR0A  = 0x02;
    UCSR0B  = 0x10 | 0x80; /* Enable receiver and RX interrupt */
    UCSR0C  = 0x06;

    SREG = sregOld;

    return;
}
/*@
    requires acsl_serialbuffer_valid(&serialRB0_RX);
    requires \valid(&UDR0);

    assigns serialRXFlag;
    assigns serialRB0_RX.buffer[0 .. SERIAL_RINGBUFFER_SIZE];
    assigns serialRB0_RX.dwHead;

    ensures acsl_serialbuffer_valid(&serialRB0_RX);
*/
ISR(USART0_RX_vect) {
    ringBuffer_WriteChar(&serialRB0_RX, UDR0);
    serialRXFlag = 1;
}
/*@
    requires acsl_serialbuffer_valid(&serialRB0_TX);

    assigns UDR0;
    assigns UCSR0B;

    ensures acsl_serialbuffer_valid(&serialRB0_TX);

    behavior dataAvail:
        assumes serialRB0_TX.dwHead != serialRB0_TX.dwTail;

        assigns UDR0;

        ensures UDR0 == serialRB0_TX.buffer[\old(serialRB0_TX.dwTail)];
        ensures serialRB0_TX.dwTail == \old(serialRB0_TX.dwTail) + 1;
    behavior noDataAvail:
        assumes serialRB0_TX.dwHead == serialRB0_TX.dwTail;

        assigns UCSR0B;

    disjoint behaviors dataAvail, noDataAvail;
    complete behaviors dataAvail, noDataAvail;
*/
ISR(USART0_UDRE_vect) {
    if(ringBuffer_Available(&serialRB0_TX) == true) {
        /* Shift next byte to the outside world ... */
        UDR0 = ringBuffer_ReadChar(&serialRB0_TX);
        if(ringBuffer_Available(&serialRB0_TX) != true) {
            /* Stop transmission after this character */
            UCSR0B = UCSR0B & (~(0x08 | 0x20));
        }
    } else {
        /*
            Since no more data is available for shifting simply stop
            the transmitter and associated interrupts
        */
        UDR0 = '$';
        UCSR0B = UCSR0B & (~(0x08 | 0x20));
    }
}


#ifdef SERIAL_UART1_ENABLE
    /*
        Serial handler (UART1, UART2)
    */
    volatile struct ringBuffer serialRB1_TX;
    volatile struct ringBuffer serialRB1_RX;

    static volatile int serialRX1Flag; /* RX flag is set to indicate that new data has arrived */

    /*@
        requires \valid(&UCSR1A) && \valid(&UCSR1B) && \valid(&SREG);

        assigns SREG;
        assigns UCSR1A;
        assigns UCSR1B;

        ensures (UCSR1A & 0x40) == 0x40;
        ensures (UCSR1B & 0x28) == 0x28;
    */
    void serialModeTX1() {
        uint8_t sregOld = SREG;
        #ifndef FRAMAC_SKIP
            cli();
        #endif

        UCSR1A = UCSR1A | 0x40; /* Reset TXCn bit */
        UCSR1B = UCSR1B | 0x08 | 0x20;
        #ifndef FRAMAC_SKIP
            SREG = sregOld;
        #endif
    }
    /*@
        requires \valid(&serialRB1_TX) && \valid(&serialRB1_RX);
        requires \valid(&UBRR1) && \valid(&UCSR1A) && \valid(&UCSR1B) && \valid(&UCSR1C);

        assigns UBRR1;
        assigns UCSR1A;
        assigns UCSR1B;
        assigns UCSR1C;

        ensures acsl_serialbuffer_valid(&serialRB1_TX);
        ensures acsl_serialbuffer_valid(&serialRB1_RX);

        ensures serialRX1Flag == 0;

        ensures UBRR1 == 103;
        ensures (UCSR1A == 0x02)
            && (UCSR1B == 0x90)
            && (UCSR1C == 0x06);
    */
    void serialInit1() {
        uint8_t sregOld = SREG;
        #ifndef FRAMAC_SKIP
            cli();
        #endif

        ringBuffer_Init(&serialRB1_TX);
        ringBuffer_Init(&serialRB1_RX);

        serialRX1Flag = 0;

        UBRR1   = 103; // 16 : 115200, 103: 19200
        UCSR1A  = 0x02;
        UCSR1B  = 0x10 | 0x80; /* Enable receiver and RX interrupt */
        UCSR1C  = 0x06;

        SREG = sregOld;

        return;
    }
    /*@
        requires acsl_serialbuffer_valid(&serialRB1_RX);
        requires \valid(&UDR1);

        assigns serialRB1_RX.buffer[0 .. SERIAL_RINGBUFFER_SIZE-1];
        assigns serialRB1_RX.dwHead;
        assigns serialRX1Flag;

        ensures acsl_serialbuffer_valid(&serialRB1_RX);
    */
    ISR(USART1_RX_vect) {
        ringBuffer_WriteChar(&serialRB1_RX, UDR1);
        serialRX1Flag = 1;
    }
    /*@
        requires acsl_serialbuffer_valid(&serialRB1_TX);

        assigns UDR1;
        assigns UCSR1B;

        ensures acsl_serialbuffer_valid(&serialRB1_TX);

        behavior dataAvail:
            assumes serialRB1_TX.dwHead != serialRB1_TX.dwTail;

            assigns UDR1;

            ensures UDR1 == serialRB1_TX.buffer[\old(serialRB1_TX.dwTail)];
            ensures serialRB1_TX.dwTail == \old(serialRB1_TX.dwTail) + 1;
        behavior noDataAvail:
            assumes serialRB1_TX.dwHead == serialRB1_TX.dwTail;

            assigns UCSR1B;

        disjoint behaviors dataAvail, noDataAvail;
        complete behaviors dataAvail, noDataAvail;
    */
    ISR(USART1_UDRE_vect) {
        if(ringBuffer_Available(&serialRB1_TX) == true) {
            /* Shift next byte to the outside world ... */
            UDR1 = ringBuffer_ReadChar(&serialRB1_TX);
            if(ringBuffer_Available(&serialRB1_TX) != true) {
                /* Stop transmission after this character */
                UCSR1B = UCSR1B & (~(0x08 | 0x20));
            }
        } else {
            /*
                Since no more data is available for shifting simply stop
                the transmitter and associated interrupts
            */
            UCSR1B = UCSR1B & (~(0x08 | 0x20));
        }
    }
#endif

volatile struct ringBuffer serialRB2_TX;
volatile struct ringBuffer serialRB2_RX;

static volatile int serialRX2Flag; /* RX flag is set to indicate that new data has arrived */

/*@
    requires \valid(&UCSR2A) && \valid(&UCSR2B) && \valid(&SREG);

    assigns SREG;
    assigns UCSR2A;
    assigns UCSR2B;

    ensures (UCSR2A & 0x40) == 0x40;
    ensures (UCSR2B & 0x28) == 0x28;
*/
void serialModeTX2() {
    uint8_t sregOld = SREG;
    #ifndef FRAMAC_SKIP
        cli();
    #endif

    UCSR2A = UCSR2A | 0x40; /* Reset TXCn bit */
    UCSR2B = UCSR2B | 0x08 | 0x20;

    SREG = sregOld;
}
/*@
    requires \valid(&serialRB2_TX);
    requires \valid(&UBRR2) && \valid(&UCSR2A) && \valid(&UCSR2B) && \valid(&UCSR2C);

    assigns UBRR2;
    assigns UCSR2A;
    assigns UCSR2B;
    assigns UCSR2C;

    ensures acsl_serialbuffer_valid(&serialRB2_TX);

    ensures serialRX2Flag == 0;

    ensures UBRR2 == 103;
    ensures (UCSR2A == 0x02)
        && (UCSR2B == 0x90)
        && (UCSR2C == 0x06);
 */
void serialInit2() {
    dwFilament__SetCurrent = 0;
    bFilament__EnableCurrent = false;

    uint8_t sregOld = SREG;
    #ifndef FRAMAC_SKIP
        cli();
    #endif

    ringBuffer_Init(&serialRB2_TX);
    ringBuffer_Init(&serialRB2_RX);

    serialRX2Flag = 0;

    UBRR2   = 832; // 16 : 115200, 103: 19200, 832 : 2400
    UCSR2A  = 0x02;
    UCSR2B  = 0x10 | 0x80; /* Enable receiver and RX interrupt */
    UCSR2C  = 0x06;

    SREG = sregOld;

    return;
}
/*@
    assigns serialRX2Flag;

    behavior ignoreOtherThanQ:
        assumes (UDR2 != 'q');
        assigns \nothing;
    behavior gotQ:
        assumes UDR2 == 'q';
        assigns serialRX2Flag;
        ensures serialRX2Flag == 1;

    disjoint behaviors gotQ, ignoreOtherThanQ;
    complete behaviors gotQ, ignoreOtherThanQ;
*/
ISR(USART2_RX_vect) {
    ringBuffer_WriteChar(&serialRB2_RX, UDR2);
    serialRX2Flag = 1;
}
    /*@
        requires acsl_serialbuffer_valid(&serialRB2_TX);

        assigns UDR2;
        assigns UCSR2B;

        ensures acsl_serialbuffer_valid(&serialRB2_TX);

        behavior dataAvail:
            assumes serialRB2_TX.dwHead != serialRB2_TX.dwTail;

            assigns UDR2;

            ensures UDR2 == serialRB2_TX.buffer[\old(serialRB2_TX.dwTail)];
            ensures serialRB2_TX.dwTail == \old(serialRB2_TX.dwTail) + 1;
        behavior noDataAvail:
            assumes serialRB2_TX.dwHead == serialRB2_TX.dwTail;

            assigns UCSR2B;

        disjoint behaviors dataAvail, noDataAvail;
        complete behaviors dataAvail, noDataAvail;
    */
ISR(USART2_UDRE_vect) {
    if(ringBuffer_Available(&serialRB2_TX) == true) {
        /* Shift next byte to the outside world ... */
        UDR2 = ringBuffer_ReadChar(&serialRB2_TX);

        /* In case we've written the last byte we stop transmissions after this byte */
        if(ringBuffer_Available(&serialRB2_TX) != true) {
            UCSR2B = UCSR2B & (~(0x08 | 0x20));
        }
    } else {
        /*
            Since no more data is available for shifting simply stop
            the transmitter and associated interrupts
        */
        UDR2 = '$';
        UCSR2B = UCSR2B & (~(0x08 | 0x20));
    }
}

/*
    =================================================
    = Command handler for serial protocol on USART0 =
    =================================================
*/

static unsigned char handleSerial0Messages_StringBuffer[SERIAL_RINGBUFFER_SIZE];

/*@
    predicate acsl_is_whitespace(char c) =
        (c == 0x0A) || (c == 0x0D) || (c == 0x09) || (c == 0x0C) || (c == 0x0B) || (c == 0x20);
*/
/*@
    requires (c >= 0) && (c < 256);

    assigns \nothing;

    behavior ws:
        assumes acsl_is_whitespace(c);
        ensures \result == true;
    behavior nows:
        assumes !acsl_is_whitespace(c);
        ensures \result == false;

    disjoint behaviors ws, nows;
    complete behaviors ws, nows;
*/
static inline bool strIsWhite(char c) {
    switch(c) {
        case 0x0A:  return true;
        case 0x0D:  return true;
        case 0x09:  return true;
        case 0x0C:  return true;
        case 0x0B:  return true;
        case 0x20:  return true;
    }
    return false;
}
/*@
    requires (c >= 0) && (c < 256);

    assigns \nothing;

    behavior capitalLetter:
        assumes (c >= 0x41) && (c <= 0x5A);
        ensures \result == (c + 0x20);
    behavior nocapitalLetter:
        assumes (c < 0x41) || (c > 0x5A);
        ensures \result == c;

    disjoint behaviors capitalLetter, nocapitalLetter;
    complete behaviors capitalLetter, nocapitalLetter;
*/
static inline char strCasefoldIfChar(char c) {
    if((c >= 0x41) && (c <= 0x5A)) { c = c + 0x20; }
    return c;
}
/*@
    requires \valid(lpA) && \valid(lpB);
    requires \valid(&(lpA[0 .. dwLenA-1]));
    requires \valid(&(lpB[0 .. dwLenB-1]));

    assigns \nothing;

    behavior diffLen:
        assumes dwLenA != dwLenB;
        assigns \nothing;
        ensures \result == false;
    behavior equalString:
        assumes dwLenA == dwLenB;
        assumes \forall int i; 0 <= i < dwLenA ==>
            lpA[i] == lpB[i];
        assigns \nothing;
        ensures \result == true;
    behavior differentString:
        assumes dwLenA == dwLenB;
        assumes \exists int i; 0 <= i <= dwLenA
            && lpA[i] != lpB[i];
        assigns \nothing;
        ensures \result == false;

    disjoint behaviors diffLen, equalString, differentString;
    complete behaviors diffLen, equalString, differentString;
*/
static bool strCompare(
    char* lpA,
    unsigned long int dwLenA,
    unsigned char* lpB,
    unsigned long int dwLenB
) {
    unsigned long int i;

    if(dwLenA != dwLenB) { return false; }

    /*@
        loop invariant 0 <= i < dwLenA;
        loop assigns \nothing;
        loop variant dwLenA - i;
    */
    for(i = 0; i < dwLenA; i=i+1) {
        if(lpA[i] != lpB[i]) {
            return false;
        }
    }

    return true;
}
/*@
    requires \valid(lpA) && \valid(lpB);
    requires \valid(&(lpA[0 .. dwLenA-1]));
    requires \valid(&(lpB[0 .. dwLenB-1]));

    assigns \nothing;

    behavior atoolong:
        assumes dwLenA > dwLenB;
        assigns \nothing;
        ensures \result == false;
    behavior equalString:
        assumes dwLenA <= dwLenB;
        assumes \forall int i; 0 <= i < dwLenA ==>
            lpA[i] == lpB[i];
        assigns \nothing;
        ensures \result == true;
    behavior differentString:
        assumes dwLenA <= dwLenB;
        assumes \exists int i; 0 <= i <= dwLenA
            && lpA[i] != lpB[i];
        assigns \nothing;
        ensures \result == false;

    disjoint behaviors atoolong, equalString, differentString;
    complete behaviors atoolong, equalString, differentString;
*/
static bool strComparePrefix(
    char* lpA,
    unsigned long int dwLenA,
    unsigned char* lpB,
    unsigned long int dwLenB
) {
    unsigned long int i;

    if(dwLenA > dwLenB) { return false; }

    /*@
        loop invariant 0 <= i < dwLenA;
        loop assigns \nothing;
        loop variant dwLenA - i;
    */
    for(i = 0; i < dwLenA; i=i+1) {
        if(lpA[i] != lpB[i]) {
            return false;
        }
    }

    return true;
}
/*@
    requires \valid(lpStr);
    requires \valid(&(lpStr[0 .. dwLen-1]));

    assigns \nothing;

    ensures (\result >= 0) && (\result < 4294967296);
*/
static uint32_t strASCIIToDecimal(
    uint8_t* lpStr,
    unsigned long int dwLen
) {
    unsigned long int i;
    uint8_t currentChar;
    uint32_t currentValue = 0;

    /*@
        loop invariant 0 <= i < dwLen;
        loop assigns currentValue;
        loop variant dwLen - i;
    */
    for(i = 0; i < dwLen; i=i+1) {
        currentChar = lpStr[i];
        if((currentChar >= 0x30) && (currentChar <= 0x39)) {
            currentChar = currentChar - 0x30;
            currentValue = currentValue * 10 + currentChar;
        }
    }
    return currentValue;
}

static unsigned char handleSerial0Messages_Response__ID[] = "$$$electronctrl_20221021_001\n";
static unsigned char handleSerial0Messages_Response__ERR[] = "$$$err\n";
static unsigned char handleSerial0Messages_Response__VN_Part[] = "$$$v";
static unsigned char handleSerial0Messages_Response__AN_Part[] = "$$$a";
static unsigned char handleSerial0Messages_Response__PSUSTATE_Part[] = "$$$psustate";

/*@
    requires \valid(&serialRB0_RX);
    requires \valid(&(serialRB0_RX.buffer[0 .. SERIAL_RINGBUFFER_SIZE]));
    requires acsl_serialbuffer_valid(&serialRB0_RX);
    requires dwLength >= 5;

    assigns handleSerial0Messages_StringBuffer[0 .. dwLength-1];

    ensures acsl_serialbuffer_valid(&serialRB0_RX);
*/
static void handleSerial0Messages_CompleteMessage(
    unsigned long int dwLength
) {
    unsigned long int dwLen;

    /*
        We have received a complete message - now we will remove the sync
        pattern, calculate actual length
    */
    ringBuffer_discardN(&serialRB0_RX, 3); /* Skip sync pattern */
    dwLen = dwLength - 3;
    //@ assert dwLen > 2;

    /* Remove end of line for next parser ... */
    dwLen = dwLen - 1; /* Remove LF */
    //@ assert dwLen > 0;
    dwLen = dwLen - ((ringBuffer_PeekCharN(&serialRB0_RX, dwLen-1) == 0x0D) ? 1 : 0); /* Remove CR if present */
    //@ assert dwLen >= 0;

    /* Now copy message into a local buffer to make parsing WAY easier ... */
    ringBuffer_ReadChars(&serialRB0_RX, handleSerial0Messages_StringBuffer, dwLen);

    /*
        Now process that message at <handleSerial0Messages_StringBuffer, dwLen>
    */

    /*
        Parse different commands
    */
    if(strCompare("id", 2, handleSerial0Messages_StringBuffer, dwLen) == true) {
        /* Send ID response ... */
        ringBuffer_WriteChars(&serialRB0_TX, handleSerial0Messages_Response__ID, sizeof(handleSerial0Messages_Response__ID)-1);
        serialModeTX0();
        filamentCurrent_GetId();
        filamentCurrent_GetVersion();
    } else if(strCompare("psugetv1", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        uint16_t v;
        {
            cli();
            v = currentADC[0];
            sei();
        }
        v = serialADC2VoltsHCP(v);
        ringBuffer_WriteChars(&serialRB0_TX, handleSerial0Messages_Response__VN_Part, sizeof(handleSerial0Messages_Response__VN_Part)-1);
        ringBuffer_WriteChar(&serialRB0_TX, '1');
        ringBuffer_WriteChar(&serialRB0_TX, ':');
        ringBuffer_WriteASCIIUnsignedInt(&serialRB0_TX, v);
        ringBuffer_WriteChar(&serialRB0_TX, 0x0A);
        serialModeTX0();
    } else if(strCompare("psugetv2", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        uint16_t v;
        {
            cli();
            v = currentADC[2];
            sei();
        }
        v = serialADC2VoltsHCP(v);
        ringBuffer_WriteChars(&serialRB0_TX, handleSerial0Messages_Response__VN_Part, sizeof(handleSerial0Messages_Response__VN_Part)-1);
        ringBuffer_WriteChar(&serialRB0_TX, '2');
        ringBuffer_WriteChar(&serialRB0_TX, ':');
        ringBuffer_WriteASCIIUnsignedInt(&serialRB0_TX, v);
        ringBuffer_WriteChar(&serialRB0_TX, 0x0A);
        serialModeTX0();
    } else if(strCompare("psugetv3", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        uint16_t v;
        {
            cli();
            v = currentADC[4];
            sei();
        }
        v = serialADC2VoltsHCP(v);
        ringBuffer_WriteChars(&serialRB0_TX, handleSerial0Messages_Response__VN_Part, sizeof(handleSerial0Messages_Response__VN_Part)-1);
        ringBuffer_WriteChar(&serialRB0_TX, '3');
        ringBuffer_WriteChar(&serialRB0_TX, ':');
        ringBuffer_WriteASCIIUnsignedInt(&serialRB0_TX, v);
        ringBuffer_WriteChar(&serialRB0_TX, 0x0A);
        serialModeTX0();
    } else if(strCompare("psugetv4", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        uint16_t v;
        {
            cli();
            v = currentADC[6];
            sei();
        }
        v = serialADC2VoltsHCP(v);
        ringBuffer_WriteChars(&serialRB0_TX, handleSerial0Messages_Response__VN_Part, sizeof(handleSerial0Messages_Response__VN_Part)-1);
        ringBuffer_WriteChar(&serialRB0_TX, '4');
        ringBuffer_WriteChar(&serialRB0_TX, ':');
        ringBuffer_WriteASCIIUnsignedInt(&serialRB0_TX, v);
        ringBuffer_WriteChar(&serialRB0_TX, 0x0A);
        serialModeTX0();
    } else if(strCompare("psugeta1", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        uint16_t a;
        {
            cli();
            a = currentADC[1];
            sei();
        }
        a = serialADC2TenthMicroampsHCP(a);
        ringBuffer_WriteChars(&serialRB0_TX, handleSerial0Messages_Response__AN_Part, sizeof(handleSerial0Messages_Response__AN_Part)-1);
        ringBuffer_WriteChar(&serialRB0_TX, '1');
        ringBuffer_WriteChar(&serialRB0_TX, ':');
        ringBuffer_WriteASCIIUnsignedInt(&serialRB0_TX, a);
        ringBuffer_WriteChar(&serialRB0_TX, 0x0A);
        serialModeTX0();
    } else if(strCompare("psugeta2", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        uint16_t a;
        {
            cli();
            a = currentADC[3];
            sei();
        }
        a = serialADC2TenthMicroampsHCP(a);
        ringBuffer_WriteChars(&serialRB0_TX, handleSerial0Messages_Response__AN_Part, sizeof(handleSerial0Messages_Response__AN_Part)-1);
        ringBuffer_WriteChar(&serialRB0_TX, '2');
        ringBuffer_WriteChar(&serialRB0_TX, ':');
        ringBuffer_WriteASCIIUnsignedInt(&serialRB0_TX, a);
        ringBuffer_WriteChar(&serialRB0_TX, 0x0A);
        serialModeTX0();
    } else if(strCompare("psugeta3", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        uint16_t a;
        {
            cli();
            a = currentADC[5];
            sei();
        }
        a = serialADC2TenthMicroampsHCP(a);
        ringBuffer_WriteChars(&serialRB0_TX, handleSerial0Messages_Response__AN_Part, sizeof(handleSerial0Messages_Response__AN_Part)-1);
        ringBuffer_WriteChar(&serialRB0_TX, '3');
        ringBuffer_WriteChar(&serialRB0_TX, ':');
        ringBuffer_WriteASCIIUnsignedInt(&serialRB0_TX, a);
        ringBuffer_WriteChar(&serialRB0_TX, 0x0A);
        serialModeTX0();
    } else if(strCompare("psugeta4", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        uint16_t a;
        {
            cli();
            a = currentADC[7];
            sei();
        }
        a = serialADC2TenthMicroampsHCP(a);
        ringBuffer_WriteChars(&serialRB0_TX, handleSerial0Messages_Response__AN_Part, sizeof(handleSerial0Messages_Response__AN_Part)-1);
        ringBuffer_WriteChar(&serialRB0_TX, '4');
        ringBuffer_WriteChar(&serialRB0_TX, ':');
        ringBuffer_WriteASCIIUnsignedInt(&serialRB0_TX, a);
        ringBuffer_WriteChar(&serialRB0_TX, 0x0A);
        serialModeTX0();
    } else if(strCompare("psupol1p", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[0].polPolarity = psuPolarity_Positive;
        rampMode.mode = controllerRampMode__None;
    } else if(strCompare("psupol1n", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[0].polPolarity = psuPolarity_Negative;
        rampMode.mode = controllerRampMode__None;
    } else if(strCompare("psupol2p", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[1].polPolarity = psuPolarity_Positive;
        rampMode.mode = controllerRampMode__None;
    } else if(strCompare("psupol2n", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[1].polPolarity = psuPolarity_Negative;
        rampMode.mode = controllerRampMode__None;
    } else if(strCompare("psupol3p", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[2].polPolarity = psuPolarity_Positive;
        rampMode.mode = controllerRampMode__None;
    } else if(strCompare("psupol3n", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[2].polPolarity = psuPolarity_Negative;
        rampMode.mode = controllerRampMode__None;
    } else if(strCompare("psupol4p", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[3].polPolarity = psuPolarity_Positive;
    } else if(strCompare("psupol4n", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[3].polPolarity = psuPolarity_Negative;
    } else if(strCompare("psuon1", 6, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[0].bOutputEnable = true;
    } else if(strCompare("psuoff1", 7, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[0].bOutputEnable = false;
        rampMode.mode = controllerRampMode__None;
    } else if(strCompare("psuon2", 6, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[1].bOutputEnable = true;
    } else if(strCompare("psuoff2", 7, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[1].bOutputEnable = false;
        rampMode.mode = controllerRampMode__None;
    } else if(strCompare("psuon3", 6, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[2].bOutputEnable = true;
    } else if(strCompare("psuoff3", 7, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[2].bOutputEnable = false;
        rampMode.mode = controllerRampMode__None;
    } else if(strCompare("psuon4", 6, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[3].bOutputEnable = true;
    } else if(strCompare("psuoff4", 7, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[3].bOutputEnable = false;
    } else if(strCompare("off", 3, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[0].bOutputEnable = false;
        psuStates[1].bOutputEnable = false;
        psuStates[2].bOutputEnable = false;
        psuStates[3].bOutputEnable = false;
        filamentCurrent_Enable(false);
        rampMode.mode = controllerRampMode__None;
        statusMessageOff();
    } else if(strCompare("filon", 5, handleSerial0Messages_StringBuffer, dwLen) == true) {
        filamentCurrent_Enable(true);
    } else if(strCompare("filoff", 6, handleSerial0Messages_StringBuffer, dwLen) == true) {
        filamentCurrent_Enable(false);
        rampMode.mode = controllerRampMode__None;
    } else if(strCompare("psumode", 7, handleSerial0Messages_StringBuffer, dwLen) == true) {
        unsigned long int iPSU;
        ringBuffer_WriteChars(&serialRB0_TX, handleSerial0Messages_Response__PSUSTATE_Part, sizeof(handleSerial0Messages_Response__PSUSTATE_Part)-1);
        for(iPSU = 0; iPSU < 4; iPSU = iPSU + 1) {
            if(psuStates[iPSU].bOutputEnable != true) {
                ringBuffer_WriteChar(&serialRB0_TX, '-');
            } else if(psuStates[iPSU].limitMode == psuLimit_Current) {
                ringBuffer_WriteChar(&serialRB0_TX, 'C');
            } else {
                ringBuffer_WriteChar(&serialRB0_TX, 'V');
            }
        }
        ringBuffer_WriteChar(&serialRB0_TX, 0x0A);
        serialModeTX0();
    } else if(strComparePrefix("psusetv1", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        setPSUVolts(strASCIIToDecimal(&(handleSerial0Messages_StringBuffer[8]), dwLen-8), 1);
        rampMode.mode = controllerRampMode__None;
    } else if(strComparePrefix("psusetv2", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        setPSUVolts(strASCIIToDecimal(&(handleSerial0Messages_StringBuffer[8]), dwLen-8), 2);
        rampMode.mode = controllerRampMode__None;
    } else if(strComparePrefix("psusetv3", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        setPSUVolts(strASCIIToDecimal(&(handleSerial0Messages_StringBuffer[8]), dwLen-8), 3);
        rampMode.mode = controllerRampMode__None;
    } else if(strComparePrefix("psusetv4", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        setPSUVolts(strASCIIToDecimal(&(handleSerial0Messages_StringBuffer[8]), dwLen-8), 4);
    } else if(strComparePrefix("psuseta1", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        setPSUMicroamps(strASCIIToDecimal(&(handleSerial0Messages_StringBuffer[8]), dwLen-8), 1);
        rampMode.mode = controllerRampMode__None;
    } else if(strComparePrefix("psuseta2", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        setPSUMicroamps(strASCIIToDecimal(&(handleSerial0Messages_StringBuffer[8]), dwLen-8), 2);
        rampMode.mode = controllerRampMode__None;
    } else if(strComparePrefix("psuseta3", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        setPSUMicroamps(strASCIIToDecimal(&(handleSerial0Messages_StringBuffer[8]), dwLen-8), 3);
        rampMode.mode = controllerRampMode__None;
    } else if(strComparePrefix("psuseta4", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        setPSUMicroamps(strASCIIToDecimal(&(handleSerial0Messages_StringBuffer[8]), dwLen-8), 4);
    } else if(strComparePrefix("fila", 4, handleSerial0Messages_StringBuffer, dwLen) == true) {
        filamentCurrent_GetCurrent();
    } else if(strComparePrefix("setfila", 7, handleSerial0Messages_StringBuffer, dwLen) == true) {
        filamentCurrent_SetCurrent(strASCIIToDecimal(&(handleSerial0Messages_StringBuffer[7]), dwLen-7));
        rampMode.mode = controllerRampMode__None;
    } else if(strCompare("insul", 5, handleSerial0Messages_StringBuffer, dwLen) == true) {
        rampStart_InsulationTest();
    } else if(strCompare("beamhvoff", 9, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[0].bOutputEnable = false;
        psuStates[1].bOutputEnable = false;
        psuStates[2].bOutputEnable = false;
        psuStates[3].bOutputEnable = false;
        setPSUVolts(0, 1);
        setPSUVolts(0, 2);
        setPSUVolts(0, 3);
        setPSUVolts(0, 4);
        rampMode.mode = controllerRampMode__None;
    } else if(strCompare("beamon", 6, handleSerial0Messages_StringBuffer, dwLen) == true) {
        rampStart_BeamOn();
    } else if(strCompare("noprotection", 12, handleSerial0Messages_StringBuffer, dwLen) == true) {
        protectionEnabled = 0;
        filamentCurrent_EnableProtection(false);
    } else if(strCompare("reset", 5, handleSerial0Messages_StringBuffer, dwLen) == true) {
        asm volatile ("jmp 0 \n");
    /* Passthrough commands to current controller */
    } else if(strComparePrefix("seta:", 5, handleSerial0Messages_StringBuffer, dwLen) == true) {
        uint32_t newAmps = strASCIIToDecimal(&(handleSerial0Messages_StringBuffer[5]), dwLen-5);
        if(newAmps > 0) {
            filamentCurrent_Enable(true);
        } else {
            filamentCurrent_Enable(false);
        }
        filamentCurrent_SetCurrent(newAmps);
    } else if(strCompare("getseta", 7, handleSerial0Messages_StringBuffer, dwLen) == true) {
        filamentCurrent_GetSetCurrent();
    } else if(strCompare("geta", 4, handleSerial0Messages_StringBuffer, dwLen) == true) {
        filamentCurrent_GetCurrent();
    } else if(strCompare("getadc0", 7, handleSerial0Messages_StringBuffer, dwLen) == true) {
        filamentCurrent_GetRawADC();
    } else if(strCompare("adccal0", 7, handleSerial0Messages_StringBuffer, dwLen) == true) {
        filamentCurrent_CalLow();
    } else if(strComparePrefix("adccalh:", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        uint32_t highMilliamps = strASCIIToDecimal(&(handleSerial0Messages_StringBuffer[8]), dwLen-8);
        filamentCurrent_CalHigh(highMilliamps);
    } else if(strCompare("adccalstore", 11, handleSerial0Messages_StringBuffer, dwLen) == true) {
        filamentCurrent_CalStore();
#ifdef DEBUG
    } else if(strCompare("rawadc", 6, handleSerial0Messages_StringBuffer, dwLen) == true) {
        /* Deliver raw adc value of frist channel for testing purpose ... */
        char bTemp[6];
        uint16_t adcValue = currentADC[0];

        int len = 0;
        unsigned long int i;

        if(adcValue == 0) {
            ringBuffer_WriteChars(&serialRB0_TX, "$$$0\n", 5);
        } else {
            ringBuffer_WriteChars(&serialRB0_TX, "$$$", 3);

            len = 0;
            /*@
                loop invariant adcValue >= 0;
                loop assigns bTemp[0 .. 5];
                loop assigns adcValue;
                loop variant adcValue;
            */
            while(adcValue > 0) {
                uint16_t nextVal = adcValue % 10;
                adcValue = adcValue / 10;

                nextVal = nextVal + 0x30;
                bTemp[len] = nextVal;
                len = len + 1;
            }

            /*@
                loop invariant 0 <= i <= len;
                loop assigns serialRB0_TX.buffer[0 .. SERIAL_RINGBUFFER_SIZE];
                loop variant len - i;
            */
            for(i = 0; i < len; i=i+1) {
                ringBuffer_WriteChar(&serialRB0_TX, bTemp[len - 1 - i]);
            }
            ringBuffer_WriteChar(&serialRB0_TX, '\n');
        }
        serialModeTX0();
#endif
    } else {
        /* Unknown: Send error response ... */
        ringBuffer_WriteChars(&serialRB0_TX, handleSerial0Messages_Response__ERR, sizeof(handleSerial0Messages_Response__ERR)-1);
        serialModeTX0();
    }
}

void handleSerial0Messages() {
    unsigned long int dwAvailableLength;
    unsigned long int dwMessageEnd;

    /*
        We simply check if a full message has arrived in the ringbuffer. If
        it has we will start to decode the message with the appropriate module
    */
    dwAvailableLength = ringBuffer_AvailableN(&serialRB0_RX);
    if(dwAvailableLength < 3) { return; } /* We cannot even see a full synchronization pattern ... */

    /*
        First we scan for the synchronization pattern. If the first character
        is not found - skip over any additional bytes ...
    */
    /*@
        loop assigns serialRB0_RX.dwTail;
    */
    while((ringBuffer_PeekChar(&serialRB0_RX) != '$') && (ringBuffer_AvailableN(&serialRB0_RX) > 3)) {
        ringBuffer_discardN(&serialRB0_RX, 1); /* Skip next character */
    }

    /* If we are too short to fit the entire synchronization packet - wait for additional data to arrive */
    if(ringBuffer_AvailableN(&serialRB0_RX) < 5) { return; }

    /*
        Discard additional bytes in case we don't see the full sync pattern and
        as long as data is available
    */
    /*@
        loop assigns serialRB0_RX.dwTail;
    */
    while(
        (
            (ringBuffer_PeekCharN(&serialRB0_RX, 0) != '$') ||
            (ringBuffer_PeekCharN(&serialRB0_RX, 1) != '$') ||
            (ringBuffer_PeekCharN(&serialRB0_RX, 2) != '$') ||
            (ringBuffer_PeekCharN(&serialRB0_RX, 3) == '$')
        )
        && (ringBuffer_AvailableN(&serialRB0_RX) > 4)
    ) {
        ringBuffer_discardN(&serialRB0_RX, 1);
    }

    /*
        If there is not enough data for a potential packet to be finished
        leave (we still have the sync pattern at the start so we can simply
        retry when additional data has arrived)
    */
    if(ringBuffer_AvailableN(&serialRB0_RX) < 5) { return; }
    dwAvailableLength = ringBuffer_AvailableN(&serialRB0_RX);

    /*
        Now check if we have already received a complete message OR are seeing
        another more sync pattern - in the latter case we ignore any message ...
    */
    dwMessageEnd = 3;
    /*@
        loop assigns dwMessageEnd;
    */
    while((dwMessageEnd < dwAvailableLength) && (ringBuffer_PeekCharN(&serialRB0_RX, dwMessageEnd) != 0x0A) && (ringBuffer_PeekCharN(&serialRB0_RX, dwMessageEnd) != '$')) {
        dwMessageEnd = dwMessageEnd + 1;
    }
    if(dwMessageEnd >= dwAvailableLength) {
        return;
    }

    if(ringBuffer_PeekCharN(&serialRB0_RX, dwMessageEnd) == 0x0A) {
        /* Received full message ... */
        handleSerial0Messages_CompleteMessage(dwMessageEnd+1);
    }
    if(ringBuffer_PeekCharN(&serialRB0_RX, dwMessageEnd) == '$') {
        /* Discard the whole packet but keep the next sync pattern */
        ringBuffer_discardN(&serialRB0_RX, dwMessageEnd);
    }

    /*
        In any other case ignore and continue without dropping the message ...
        we will wait till we received the whole message
    */
    return;
}

/*
    =================================================
    = Command handler for serial protocol on USART1 =
    =================================================
*/

#ifdef SERIAL_UART1_ENABLE
    static unsigned char handleSerial1Messages_StringBuffer[SERIAL_RINGBUFFER_SIZE];

    static unsigned char handleSerial1Messages_Response__ID[] = "$$$electronctrl_20221021_001\n";
    static unsigned char handleSerial1Messages_Response__ERR[] = "$$$err\n";
    static unsigned char handleSerial1Messages_Response__VN_Part[] = "$$$v";
    static unsigned char handleSerial1Messages_Response__AN_Part[] = "$$$a";
    static unsigned char handleSerial1Messages_Response__PSUSTATE_Part[] = "$$$psustate";

    static void handleSerial1Messages_CompleteMessage(
        unsigned long int dwLength
    ) {
        unsigned long int dwLen;

        /*
            We have received a complete message - now we will remove the sync
            pattern, calculate actual length
        */
        ringBuffer_discardN(&serialRB1_RX, 3); /* Skip sync pattern */
        dwLen = dwLength - 3;

        /* Remove end of line for next parser ... */
        dwLen = dwLen - 1; /* Remove LF */
        dwLen = dwLen - ((ringBuffer_PeekCharN(&serialRB1_RX, dwLen-1) == 0x0D) ? 1 : 0); /* Remove CR if present */


        /* Now copy message into a local buffer to make parsing WAY easier ... */
        ringBuffer_ReadChars(&serialRB1_RX, handleSerial1Messages_StringBuffer, dwLen);

        /*
            Now process that message at <handleSerial1Messages_StringBuffer, dwLen>
        */

        /*
            Parse different commands
        */
        if(strCompare("id", 2, handleSerial1Messages_StringBuffer, dwLen) == true) {
            /* Send ID response ... */
            ringBuffer_WriteChars(&serialRB1_TX, handleSerial1Messages_Response__ID, sizeof(handleSerial1Messages_Response__ID)-1);
            serialModeTX1();
            filamentCurrent_GetId();
            filamentCurrent_GetVersion();
        } else if(strCompare("psugetv1", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            uint16_t v;
            {
                cli();
                v = currentADC[0];
                sei();
            }
            v = serialADC2VoltsHCP(v);
            ringBuffer_WriteChars(&serialRB1_TX, handleSerial1Messages_Response__VN_Part, sizeof(handleSerial1Messages_Response__VN_Part)-1);
            ringBuffer_WriteChar(&serialRB1_TX, '1');
            ringBuffer_WriteChar(&serialRB1_TX, ':');
            ringBuffer_WriteASCIIUnsignedInt(&serialRB1_TX, v);
            ringBuffer_WriteChar(&serialRB1_TX, 0x0A);
            serialModeTX1();
        } else if(strCompare("psugetv2", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            uint16_t v;
            {
                cli();
                v = currentADC[2];
                sei();
            }
            v = serialADC2VoltsHCP(v);
            ringBuffer_WriteChars(&serialRB1_TX, handleSerial1Messages_Response__VN_Part, sizeof(handleSerial1Messages_Response__VN_Part)-1);
            ringBuffer_WriteChar(&serialRB1_TX, '2');
            ringBuffer_WriteChar(&serialRB1_TX, ':');
            ringBuffer_WriteASCIIUnsignedInt(&serialRB1_TX, v);
            ringBuffer_WriteChar(&serialRB1_TX, 0x0A);
            serialModeTX1();
        } else if(strCompare("psugetv3", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            uint16_t v;
            {
                cli();
                v = currentADC[4];
                sei();
            }
            v = serialADC2VoltsHCP(v);
            ringBuffer_WriteChars(&serialRB1_TX, handleSerial1Messages_Response__VN_Part, sizeof(handleSerial1Messages_Response__VN_Part)-1);
            ringBuffer_WriteChar(&serialRB1_TX, '3');
            ringBuffer_WriteChar(&serialRB1_TX, ':');
            ringBuffer_WriteASCIIUnsignedInt(&serialRB1_TX, v);
            ringBuffer_WriteChar(&serialRB1_TX, 0x0A);
            serialModeTX1();
        } else if(strCompare("psugetv4", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            uint16_t v;
            {
                cli();
                v = currentADC[6];
                sei();
            }
            v = serialADC2VoltsHCP(v);
            ringBuffer_WriteChars(&serialRB1_TX, handleSerial1Messages_Response__VN_Part, sizeof(handleSerial1Messages_Response__VN_Part)-1);
            ringBuffer_WriteChar(&serialRB1_TX, '4');
            ringBuffer_WriteChar(&serialRB1_TX, ':');
            ringBuffer_WriteASCIIUnsignedInt(&serialRB1_TX, v);
            ringBuffer_WriteChar(&serialRB1_TX, 0x0A);
            serialModeTX1();
        } else if(strCompare("psugeta1", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            uint16_t a;
            {
                cli();
                a = currentADC[1];
                sei();
            }
            a = serialADC2TenthMicroampsHCP(a);
            ringBuffer_WriteChars(&serialRB1_TX, handleSerial1Messages_Response__AN_Part, sizeof(handleSerial1Messages_Response__AN_Part)-1);
            ringBuffer_WriteChar(&serialRB1_TX, '1');
            ringBuffer_WriteChar(&serialRB1_TX, ':');
            ringBuffer_WriteASCIIUnsignedInt(&serialRB1_TX, a);
            ringBuffer_WriteChar(&serialRB1_TX, 0x0A);
            serialModeTX1();
        } else if(strCompare("psugeta2", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            uint16_t a;
            {
                cli();
                a = currentADC[3];
                sei();
            }
            a = serialADC2TenthMicroampsHCP(a);
            ringBuffer_WriteChars(&serialRB1_TX, handleSerial1Messages_Response__AN_Part, sizeof(handleSerial1Messages_Response__AN_Part)-1);
            ringBuffer_WriteChar(&serialRB1_TX, '2');
            ringBuffer_WriteChar(&serialRB1_TX, ':');
            ringBuffer_WriteASCIIUnsignedInt(&serialRB1_TX, a);
            ringBuffer_WriteChar(&serialRB1_TX, 0x0A);
            serialModeTX1();
        } else if(strCompare("psugeta3", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            uint16_t a;
            {
                cli();
                a = currentADC[5];
                sei();
            }
            a = serialADC2TenthMicroampsHCP(a);
            ringBuffer_WriteChars(&serialRB1_TX, handleSerial1Messages_Response__AN_Part, sizeof(handleSerial1Messages_Response__AN_Part)-1);
            ringBuffer_WriteChar(&serialRB1_TX, '3');
            ringBuffer_WriteChar(&serialRB1_TX, ':');
            ringBuffer_WriteASCIIUnsignedInt(&serialRB1_TX, a);
            ringBuffer_WriteChar(&serialRB1_TX, 0x0A);
            serialModeTX1();
        } else if(strCompare("psugeta4", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            uint16_t a;
            {
                cli();
                a = currentADC[7];
                sei();
            }
            a = serialADC2TenthMicroampsHCP(a);
            ringBuffer_WriteChars(&serialRB1_TX, handleSerial1Messages_Response__AN_Part, sizeof(handleSerial1Messages_Response__AN_Part)-1);
            ringBuffer_WriteChar(&serialRB1_TX, '4');
            ringBuffer_WriteChar(&serialRB1_TX, ':');
            ringBuffer_WriteASCIIUnsignedInt(&serialRB1_TX, a);
            ringBuffer_WriteChar(&serialRB1_TX, 0x0A);
            serialModeTX1();
        } else if(strCompare("psupol1p", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            psuStates[0].polPolarity = psuPolarity_Positive;
            rampMode.mode = controllerRampMode__None;
        } else if(strCompare("psupol1n", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            psuStates[0].polPolarity = psuPolarity_Negative;
            rampMode.mode = controllerRampMode__None;
        } else if(strCompare("psupol2p", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            psuStates[1].polPolarity = psuPolarity_Positive;
            rampMode.mode = controllerRampMode__None;
        } else if(strCompare("psupol2n", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            psuStates[1].polPolarity = psuPolarity_Negative;
            rampMode.mode = controllerRampMode__None;
        } else if(strCompare("psupol3p", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            psuStates[2].polPolarity = psuPolarity_Positive;
            rampMode.mode = controllerRampMode__None;
        } else if(strCompare("psupol3n", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            psuStates[2].polPolarity = psuPolarity_Negative;
            rampMode.mode = controllerRampMode__None;
        } else if(strCompare("psupol4p", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            psuStates[3].polPolarity = psuPolarity_Positive;
        } else if(strCompare("psupol4n", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            psuStates[3].polPolarity = psuPolarity_Negative;
        } else if(strCompare("psuon1", 6, handleSerial1Messages_StringBuffer, dwLen) == true) {
            psuStates[0].bOutputEnable = true;
        } else if(strCompare("psuoff1", 7, handleSerial1Messages_StringBuffer, dwLen) == true) {
            psuStates[0].bOutputEnable = false;
            rampMode.mode = controllerRampMode__None;
        } else if(strCompare("psuon2", 6, handleSerial1Messages_StringBuffer, dwLen) == true) {
            psuStates[1].bOutputEnable = true;
        } else if(strCompare("psuoff2", 7, handleSerial1Messages_StringBuffer, dwLen) == true) {
            psuStates[1].bOutputEnable = false;
            rampMode.mode = controllerRampMode__None;
        } else if(strCompare("psuon3", 6, handleSerial1Messages_StringBuffer, dwLen) == true) {
            psuStates[2].bOutputEnable = true;
        } else if(strCompare("psuoff3", 7, handleSerial1Messages_StringBuffer, dwLen) == true) {
            psuStates[2].bOutputEnable = false;
            rampMode.mode = controllerRampMode__None;
        } else if(strCompare("psuon4", 6, handleSerial1Messages_StringBuffer, dwLen) == true) {
            psuStates[3].bOutputEnable = true;
        } else if(strCompare("psuoff4", 7, handleSerial1Messages_StringBuffer, dwLen) == true) {
            psuStates[3].bOutputEnable = false;
        } else if(strCompare("off", 3, handleSerial1Messages_StringBuffer, dwLen) == true) {
            psuStates[0].bOutputEnable = false;
            psuStates[1].bOutputEnable = false;
            psuStates[2].bOutputEnable = false;
            psuStates[3].bOutputEnable = false;
            setPSUVolts(0, 1);
            setPSUVolts(0, 2);
            setPSUVolts(0, 3);
            setPSUVolts(0, 4);
            filamentCurrent_Enable(false);
            rampMode.mode = controllerRampMode__None;
            statusMessageOff();
        } else if(strCompare("filon", 5, handleSerial1Messages_StringBuffer, dwLen) == true) {
            filamentCurrent_Enable(true);
        } else if(strCompare("filoff", 6, handleSerial1Messages_StringBuffer, dwLen) == true) {
            filamentCurrent_Enable(false);
            rampMode.mode = controllerRampMode__None;
        } else if(strCompare("psumode", 7, handleSerial1Messages_StringBuffer, dwLen) == true) {
            unsigned long int iPSU;
            ringBuffer_WriteChars(&serialRB1_TX, handleSerial1Messages_Response__PSUSTATE_Part, sizeof(handleSerial1Messages_Response__PSUSTATE_Part)-1);
            for(iPSU = 0; iPSU < 4; iPSU = iPSU + 1) {
                if(psuStates[iPSU].bOutputEnable != true) {
                    ringBuffer_WriteChar(&serialRB1_TX, '-');
                } else if(psuStates[iPSU].limitMode == psuLimit_Current) {
                    ringBuffer_WriteChar(&serialRB1_TX, 'C');
                } else {
                    ringBuffer_WriteChar(&serialRB1_TX, 'V');
                }
            }
            ringBuffer_WriteChar(&serialRB1_TX, 0x0A);
            serialModeTX1();
        } else if(strComparePrefix("psusetv1", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            setPSUVolts(strASCIIToDecimal(&(handleSerial1Messages_StringBuffer[8]), dwLen-8), 1);
            rampMode.mode = controllerRampMode__None;
        } else if(strComparePrefix("psusetv2", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            setPSUVolts(strASCIIToDecimal(&(handleSerial1Messages_StringBuffer[8]), dwLen-8), 2);
            rampMode.mode = controllerRampMode__None;
        } else if(strComparePrefix("psusetv3", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            setPSUVolts(strASCIIToDecimal(&(handleSerial1Messages_StringBuffer[8]), dwLen-8), 3);
            rampMode.mode = controllerRampMode__None;
        } else if(strComparePrefix("psusetv4", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            setPSUVolts(strASCIIToDecimal(&(handleSerial1Messages_StringBuffer[8]), dwLen-8), 4);
        } else if(strComparePrefix("psuseta1", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            setPSUMicroamps(strASCIIToDecimal(&(handleSerial1Messages_StringBuffer[8]), dwLen-8), 1);
            rampMode.mode = controllerRampMode__None;
        } else if(strComparePrefix("psuseta2", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            setPSUMicroamps(strASCIIToDecimal(&(handleSerial1Messages_StringBuffer[8]), dwLen-8), 2);
            rampMode.mode = controllerRampMode__None;
        } else if(strComparePrefix("psuseta3", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            setPSUMicroamps(strASCIIToDecimal(&(handleSerial1Messages_StringBuffer[8]), dwLen-8), 3);
            rampMode.mode = controllerRampMode__None;
        } else if(strComparePrefix("psuseta4", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
            setPSUMicroamps(strASCIIToDecimal(&(handleSerial1Messages_StringBuffer[8]), dwLen-8), 4);
        } else if(strComparePrefix("fila", 4, handleSerial1Messages_StringBuffer, dwLen) == true) {
            filamentCurrent_GetCurrent();
        } else if(strComparePrefix("setfila", 7, handleSerial1Messages_StringBuffer, dwLen) == true) {
            /* Currently setting PWM cycles instead of mA, will require calibration with working filament ... */
            filamentCurrent_SetCurrent(strASCIIToDecimal(&(handleSerial1Messages_StringBuffer[7]), dwLen-7));
            rampMode.mode = controllerRampMode__None;
        } else if(strCompare("insul", 5, handleSerial1Messages_StringBuffer, dwLen) == true) {
            rampStart_InsulationTest();
        } else if(strCompare("beamhvoff", 9, handleSerial1Messages_StringBuffer, dwLen) == true) {
            psuStates[0].bOutputEnable = false;
            psuStates[1].bOutputEnable = false;
            psuStates[2].bOutputEnable = false;
            psuStates[3].bOutputEnable = false;
            rampMode.mode = controllerRampMode__None;
        } else if(strCompare("beamon", 6, handleSerial1Messages_StringBuffer, dwLen) == true) {
            rampStart_BeamOn();
        } else if(strCompare("noprotection", 12, handleSerial1Messages_StringBuffer, dwLen) == true) {
            protectionEnabled = 0;
            filamentCurrent_EnableProtection(false);
        } else if(strCompare("reset", 5, handleSerial1Messages_StringBuffer, dwLen) == true) {
            asm volatile ("jmp 0 \n");
    /* Passthrough commands to current controller */
    } else if(strComparePrefix("seta:", 5, handleSerial1Messages_StringBuffer, dwLen) == true) {
        uint32_t newAmps = strASCIIToDecimal(&(handleSerial1Messages_StringBuffer[5]), dwLen-5);
        if(newAmps > 0) {
            filamentCurrent_Enable(true);
        } else {
            filamentCurrent_Enable(false);
        }
        filamentCurrent_SetCurrent(newAmps);
    } else if(strCompare("getseta", 7, handleSerial1Messages_StringBuffer, dwLen) == true) {
        filamentCurrent_GetSetCurrent();
    } else if(strCompare("geta", 4, handleSerial1Messages_StringBuffer, dwLen) == true) {
        filamentCurrent_GetCurrent();
    } else if(strCompare("getadc0", 7, handleSerial1Messages_StringBuffer, dwLen) == true) {
        filamentCurrent_GetRawADC();
    } else if(strCompare("adccal0", 7, handleSerial1Messages_StringBuffer, dwLen) == true) {
        filamentCurrent_CalLow();
    } else if(strComparePrefix("adccalh:", 8, handleSerial1Messages_StringBuffer, dwLen) == true) {
        uint32_t highMilliamps = strASCIIToDecimal(&(handleSerial1Messages_StringBuffer[8]), dwLen-8);
        filamentCurrent_CalHigh(highMilliamps);
    } else if(strCompare("adccalstore", 11, handleSerial1Messages_StringBuffer, dwLen) == true) {
        filamentCurrent_CalStore();
    #ifdef DEBUG
        } else if(strCompare("rawadc", 6, handleSerial1Messages_StringBuffer, dwLen) == true) {
            /* Deliver raw adc value of frist channel for testing purpose ... */
            char bTemp[6];
            uint16_t adcValue = currentADC[0];

            int len = 0;
            unsigned long int i;

            if(adcValue == 0) {
                ringBuffer_WriteChars(&serialRB1_TX, "$$$0\n", 5);
            } else {
                ringBuffer_WriteChars(&serialRB1_TX, "$$$", 3);

                len = 0;
                while(adcValue > 0) {
                    uint16_t nextVal = adcValue % 10;
                    adcValue = adcValue / 10;

                    nextVal = nextVal + 0x30;
                    bTemp[len] = nextVal;
                    len = len + 1;
                }

                for(i = 0; i < len; i=i+1) {
                    ringBuffer_WriteChar(&serialRB1_TX, bTemp[len - 1 - i]);
                }
                ringBuffer_WriteChar(&serialRB1_TX, '\n');
            }
            serialModeTX1();
    #endif
        } else {
            /* Unknown: Send error response ... */
            ringBuffer_WriteChars(&serialRB1_TX, handleSerial1Messages_Response__ERR, sizeof(handleSerial1Messages_Response__ERR)-1);
            serialModeTX1();
        }
    }

    void handleSerial1Messages() {
        unsigned long int dwAvailableLength;
        unsigned long int dwMessageEnd;

        /*
            We simply check if a full message has arrived in the ringbuffer. If
            it has we will start to decode the message with the appropriate module
        */
        dwAvailableLength = ringBuffer_AvailableN(&serialRB1_RX);
        if(dwAvailableLength < 3) { return; } /* We cannot even see a full synchronization pattern ... */

        /*
            First we scan for the synchronization pattern. If the first character
            is not found - skip over any additional bytes ...
        */
        while((ringBuffer_PeekChar(&serialRB1_RX) != '$') && (ringBuffer_AvailableN(&serialRB1_RX) > 3)) {
            ringBuffer_discardN(&serialRB1_RX, 1); /* Skip next character */
        }

        /* If we are too short to fit the entire synchronization packet - wait for additional data to arrive */
        if(ringBuffer_AvailableN(&serialRB1_RX) < 5) { return; }

        /*
            Discard additional bytes in case we don't see the full sync pattern and
            as long as data is available
        */
        while(
            (
                (ringBuffer_PeekCharN(&serialRB1_RX, 0) != '$') ||
                (ringBuffer_PeekCharN(&serialRB1_RX, 1) != '$') ||
                (ringBuffer_PeekCharN(&serialRB1_RX, 2) != '$') ||
                (ringBuffer_PeekCharN(&serialRB1_RX, 3) == '$')
            )
            && (ringBuffer_AvailableN(&serialRB1_RX) > 4)
        ) {
            ringBuffer_discardN(&serialRB1_RX, 1);
        }

        /*
            If there is not enough data for a potential packet to be finished
            leave (we still have the sync pattern at the start so we can simply
            retry when additional data has arrived)
        */
        if(ringBuffer_AvailableN(&serialRB1_RX) < 5) { return; }
        dwAvailableLength = ringBuffer_AvailableN(&serialRB1_RX);

        /*
            Now check if we have already received a complete message OR are seeing
            another more sync pattern - in the latter case we ignore any message ...
        */
        dwMessageEnd = 3;
        while((dwMessageEnd < dwAvailableLength) && (ringBuffer_PeekCharN(&serialRB1_RX, dwMessageEnd) != 0x0A) && (ringBuffer_PeekCharN(&serialRB1_RX, dwMessageEnd) != '$')) {
            dwMessageEnd = dwMessageEnd + 1;
        }
        if(dwMessageEnd >= dwAvailableLength) {
            return;
        }

        if(ringBuffer_PeekCharN(&serialRB1_RX, dwMessageEnd) == 0x0A) {
            /* Received full message ... */
            handleSerial1Messages_CompleteMessage(dwMessageEnd+1);
        }
        if(ringBuffer_PeekCharN(&serialRB1_RX, dwMessageEnd) == '$') {
            /* Discard the whole packet but keep the next sync pattern */
            ringBuffer_discardN(&serialRB1_RX, dwMessageEnd);
        }

        /*
            In any other case ignore and continue without dropping the message ...
            we will wait till we received the whole message
        */
        return;
    }
#endif

/*
    =================================================
    = Command handler for serial protocol on USART2 =
    =================================================
*/

static unsigned char handleSerial2Messages_StringBuffer[SERIAL_RINGBUFFER_SIZE];

static void handleSerial2Messages_CompleteMessage(
    unsigned long int dwLength
) {
    unsigned long int dwLen;
    bool bPassthrough = false;

    /*
        We have received a complete message - now we will remove the sync
        pattern, calculate actual length
    */
    ringBuffer_discardN(&serialRB2_RX, 3); /* Skip sync pattern */
    dwLen = dwLength - 3;
    //@ assert dwLen > 2;

    /* Remove end of line for next parser ... */
    dwLen = dwLen - 1; /* Remove LF */
    //@ assert dwLen > 0;
    dwLen = dwLen - ((ringBuffer_PeekCharN(&serialRB2_RX, dwLen-1) == 0x0D) ? 1 : 0); /* Remove CR if present */
    //@ assert dwLen >= 0;

    /* Now copy message into a local buffer to make parsing WAY easier ... */
    ringBuffer_ReadChars(&serialRB2_RX, handleSerial2Messages_StringBuffer, dwLen);

    /*
        Now process that message at <handleSerial2Messages_StringBuffer, dwLen>
    */

    /*
        Parse different replies from current controller board
    */

    if(strComparePrefix("id:", 3, handleSerial2Messages_StringBuffer, dwLen)) {
        /* Filament controller ID response. Pass through to other ports */
        bPassthrough = true;
    } else if(strComparePrefix("ver:", 4, handleSerial2Messages_StringBuffer, dwLen)) {
        /* Filament controller version response */
        bPassthrough = true;
    } else if(strComparePrefix("seta:", 5, handleSerial2Messages_StringBuffer, dwLen)) {
        /* Filament controller query for set current response */
        bPassthrough = true;
    } else if(strComparePrefix("adc0:", 5, handleSerial2Messages_StringBuffer, dwLen)) {
        /* Filament controller raw ADC query response */
        bPassthrough = true;
    } else if(strComparePrefix("ra:", 3, handleSerial2Messages_StringBuffer, dwLen)) {
        /* Filament controller measured current response */
        bPassthrough = true;
    } else if(strCompare("ok", 2, handleSerial2Messages_StringBuffer, dwLen)) {
        /* Filament controller ok response */
        bPassthrough = true;
    } else if(strCompare("err", 3, handleSerial2Messages_StringBuffer, dwLen)) {
        /* Filament controller error response */
        bPassthrough = true;
    } else {
        /* Unknown ... */
    }

    if(bPassthrough != false) {
        ringBuffer_WriteChar(&serialRB0_TX, '$');
        ringBuffer_WriteChar(&serialRB0_TX, '$');
        ringBuffer_WriteChar(&serialRB0_TX, '$');
        ringBuffer_WriteChars(&serialRB0_TX, handleSerial2Messages_StringBuffer, dwLen);
        ringBuffer_WriteChar(&serialRB0_TX, 0x0A);

        ringBuffer_WriteChar(&serialRB1_TX, '$');
        ringBuffer_WriteChar(&serialRB1_TX, '$');
        ringBuffer_WriteChar(&serialRB1_TX, '$');
        ringBuffer_WriteChars(&serialRB1_TX, handleSerial2Messages_StringBuffer, dwLen);
        ringBuffer_WriteChar(&serialRB1_TX, 0x0A);

        serialModeTX0();
        serialModeTX1();
    }
}

void handleSerial2Messages() {
    unsigned long int dwAvailableLength;
    unsigned long int dwMessageEnd;

    /*
        We simply check if a full message has arrived in the ringbuffer. If
        it has we will start to decode the message with the appropriate module
    */
    dwAvailableLength = ringBuffer_AvailableN(&serialRB2_RX);
    if(dwAvailableLength < 3) { return; } /* We cannot even see a full synchronization pattern ... */

    /*
        First we scan for the synchronization pattern. If the first character
        is not found - skip over any additional bytes ...
    */
    /*@
        loop assigns serialRB0_RX.dwTail;
    */
    while((ringBuffer_PeekChar(&serialRB2_RX) != '$') && (ringBuffer_AvailableN(&serialRB0_RX) > 3)) {
        ringBuffer_discardN(&serialRB2_RX, 1); /* Skip next character */
    }

    /* If we are too short to fit the entire synchronization packet - wait for additional data to arrive */
    if(ringBuffer_AvailableN(&serialRB2_RX) < 5) { return; }

    /*
        Discard additional bytes in case we don't see the full sync pattern and
        as long as data is available
    */
    /*@
        loop assigns serialRB0_RX.dwTail;
    */
    while(
        (
            (ringBuffer_PeekCharN(&serialRB2_RX, 0) != '$') ||
            (ringBuffer_PeekCharN(&serialRB2_RX, 1) != '$') ||
            (ringBuffer_PeekCharN(&serialRB2_RX, 2) != '$') ||
            (ringBuffer_PeekCharN(&serialRB2_RX, 3) == '$')
        )
        && (ringBuffer_AvailableN(&serialRB2_RX) > 4)
    ) {
        ringBuffer_discardN(&serialRB2_RX, 1);
    }

    /*
        If there is not enough data for a potential packet to be finished
        leave (we still have the sync pattern at the start so we can simply
        retry when additional data has arrived)
    */
    if(ringBuffer_AvailableN(&serialRB2_RX) < 5) { return; }
    dwAvailableLength = ringBuffer_AvailableN(&serialRB2_RX);

    /*
        Now check if we have already received a complete message OR are seeing
        another more sync pattern - in the latter case we ignore any message ...
    */
    dwMessageEnd = 3;
    /*@
        loop assigns dwMessageEnd;
    */
    while((dwMessageEnd < dwAvailableLength) && (ringBuffer_PeekCharN(&serialRB2_RX, dwMessageEnd) != 0x0A) && (ringBuffer_PeekCharN(&serialRB2_RX, dwMessageEnd) != '$')) {
        dwMessageEnd = dwMessageEnd + 1;
    }
    if(dwMessageEnd >= dwAvailableLength) {
        return;
    }

    if(ringBuffer_PeekCharN(&serialRB2_RX, dwMessageEnd) == 0x0A) {
        /* Received full message ... */
        handleSerial2Messages_CompleteMessage(dwMessageEnd+1);
    }
    if(ringBuffer_PeekCharN(&serialRB2_RX, dwMessageEnd) == '$') {
        /* Discard the whole packet but keep the next sync pattern */
        ringBuffer_discardN(&serialRB2_RX, dwMessageEnd);
    }

    /*
        In any other case ignore and continue without dropping the message ...
        we will wait till we received the whole message
    */
    return;
}



/*
    ==============================
    Status message output routines
    ==============================
*/

void rampMessage_ReportVoltages() {
    unsigned long int i;
    /*@
        loop invariant 0 <= i <= 4;
        loop assigns serialRB0_TX.buffer[0 .. SERIAL_RINGBUFFER_SIZE];
        loop variant 4 - i;
    */
    for(i = 0; i < 4; i=i+1) {
        uint16_t v;
        {
            cli();
            v = currentADC[i*2];
            sei();
        }
        v = serialADC2VoltsHCP(v);

        ringBuffer_WriteChars(&serialRB0_TX, handleSerial0Messages_Response__VN_Part, sizeof(handleSerial0Messages_Response__VN_Part)-1);
        ringBuffer_WriteChar(&serialRB0_TX, 0x31+i);
        ringBuffer_WriteChar(&serialRB0_TX, ':');
        ringBuffer_WriteASCIIUnsignedInt(&serialRB0_TX, v);
        ringBuffer_WriteChar(&serialRB0_TX, 0x0A);

        #ifdef SERIAL_UART1_ENABLE
            ringBuffer_WriteChars(&serialRB1_TX, handleSerial0Messages_Response__VN_Part, sizeof(handleSerial0Messages_Response__VN_Part)-1);
            ringBuffer_WriteChar(&serialRB1_TX, 0x31+i);
            ringBuffer_WriteChar(&serialRB1_TX, ':');
            ringBuffer_WriteASCIIUnsignedInt(&serialRB1_TX, v);
            ringBuffer_WriteChar(&serialRB1_TX, 0x0A);
        #endif
    }
    serialModeTX0();
    #ifdef SERIAL_UART1_ENABLE
        serialModeTX1();
    #endif
}

static unsigned char rampMessage_ReportFilaCurrents__Message1[] = "$$$filseta:";
static unsigned char rampMessage_ReportFilaCurrents__MessageDisabled[] = "disabled\n";
// static unsigned char rampMessage_ReportFilaCurrents__Message2[] = "$$$fila:";
void rampMessage_ReportFilaCurrents() {
    // if (bFilament__EnableCurrent != false) {
        uint16_t a = (uint16_t)dwFilament__SetCurrent;

        ringBuffer_WriteChars(&serialRB0_TX, rampMessage_ReportFilaCurrents__Message1, sizeof(rampMessage_ReportFilaCurrents__Message1)-1);
        ringBuffer_WriteASCIIUnsignedInt(&serialRB0_TX, a);
        ringBuffer_WriteChar(&serialRB0_TX, ':');

        #ifdef SERIAL_UART1_ENABLE
            ringBuffer_WriteChars(&serialRB1_TX, rampMessage_ReportFilaCurrents__Message1, sizeof(rampMessage_ReportFilaCurrents__Message1)-1);
            ringBuffer_WriteASCIIUnsignedInt(&serialRB1_TX, a);
            ringBuffer_WriteChar(&serialRB1_TX, ':');
        #endif

        a = (uint16_t)dwFilament__SetCurrent;

        ringBuffer_WriteASCIIUnsignedInt(&serialRB0_TX, a);
        ringBuffer_WriteChar(&serialRB0_TX, 0x0A);

        #ifdef SERIAL_UART1_ENABLE
            ringBuffer_WriteASCIIUnsignedInt(&serialRB1_TX, a);
            ringBuffer_WriteChar(&serialRB1_TX, 0x0A);
        #endif
    /* } else {
        ringBuffer_WriteChars(&serialRB0_TX, rampMessage_ReportFilaCurrents__Message1, sizeof(rampMessage_ReportFilaCurrents__Message1)-1);
        ringBuffer_WriteChars(&serialRB0_TX, rampMessage_ReportFilaCurrents__MessageDisabled, sizeof(rampMessage_ReportFilaCurrents__MessageDisabled)-1);

        #ifdef SERIAL_UART1_ENABLE
            ringBuffer_WriteChars(&serialRB1_TX, rampMessage_ReportFilaCurrents__Message1, sizeof(rampMessage_ReportFilaCurrents__Message1)-1);
            ringBuffer_WriteChars(&serialRB1_TX, rampMessage_ReportFilaCurrents__MessageDisabled, sizeof(rampMessage_ReportFilaCurrents__MessageDisabled)-1);
        #endif
    } */
    serialModeTX0();
    #ifdef SERIAL_UART1_ENABLE
        serialModeTX1();
    #endif
}

static unsigned char rampMessage_InsulationTestSuccess__Message[] = "$$$insulok\n";
void rampMessage_InsulationTestSuccess() {
    ringBuffer_WriteChars(&serialRB0_TX, rampMessage_InsulationTestSuccess__Message, sizeof(rampMessage_InsulationTestSuccess__Message)-1);
    serialModeTX0();

    #ifdef SERIAL_UART1_ENABLE
        ringBuffer_WriteChars(&serialRB1_TX, rampMessage_InsulationTestSuccess__Message, sizeof(rampMessage_InsulationTestSuccess__Message)-1);
        serialModeTX1();
    #endif
}

static unsigned char rampMessage_BeamOnSuccess__Message[] = "$$$beamon\n";
void rampMessage_BeamOnSuccess() {
    ringBuffer_WriteChars(&serialRB0_TX, rampMessage_BeamOnSuccess__Message, sizeof(rampMessage_BeamOnSuccess__Message)-1);
    serialModeTX0();

    #ifdef SERIAL_UART1_ENABLE
        ringBuffer_WriteChars(&serialRB1_TX, rampMessage_BeamOnSuccess__Message, sizeof(rampMessage_BeamOnSuccess__Message)-1);
        serialModeTX1();
    #endif
}

static unsigned char rampMessage_InsulationTestFailure__Message[] = "$$$insulfailed:";
void rampMessage_InsulationTestFailure() {
    unsigned long int i;
    ringBuffer_WriteChars(&serialRB0_TX, rampMessage_InsulationTestFailure__Message, sizeof(rampMessage_InsulationTestFailure__Message)-1);
    for(i = 0; i < 4; i=i+1) {
        ringBuffer_WriteChar(&serialRB0_TX, ((rampMode.vTargets[i] != 0) && (psuStates[i].limitMode == psuLimit_Current)) ? 'F' : '-');
    }
    ringBuffer_WriteChar(&serialRB0_TX, 0x0A);
    serialModeTX0();

    #ifdef SERIAL_UART1_ENABLE
        ringBuffer_WriteChars(&serialRB1_TX, rampMessage_InsulationTestFailure__Message, sizeof(rampMessage_InsulationTestFailure__Message)-1);
        for(i = 0; i < 4; i=i+1) {
            ringBuffer_WriteChar(&serialRB1_TX, ((rampMode.vTargets[i] != 0) && (psuStates[i].limitMode == psuLimit_Current)) ? 'F' : '-');
        }
        ringBuffer_WriteChar(&serialRB1_TX, 0x0A);
        serialModeTX1();
    #endif
}

static unsigned char statusMessageOff_Msg[] = "$$$off\n";
void statusMessageOff() {
    ringBuffer_WriteChars(&serialRB0_TX, statusMessageOff_Msg, sizeof(statusMessageOff_Msg)-1);
    serialModeTX0();

    #ifdef SERIAL_UART1_ENABLE
        ringBuffer_WriteChars(&serialRB1_TX, statusMessageOff_Msg, sizeof(statusMessageOff_Msg)-1);
        serialModeTX1();
    #endif
}

/* Methods to communicate with filament current board */

static unsigned char filamentCurrent__Msg_ID[] = "$$$id\n";
static unsigned char filamentCurrent__Msg_Version[] = "$$$ver\n";
static unsigned char filamentCurrent__Msg_SetCurrent_Part[] = "$$$seta:";
static unsigned char filamentCurrent__Msg_GetSetCurrent[] = "$$$getseta\n";
static unsigned char filamentCurrent__Msg_GetCurrent[] = "$$$geta\n";
static unsigned char filamentCurrent__Msg_GetCurrentADCRaw[] = "$$$getadc0\n";
static unsigned char filamentCurrent__Msg_AdcCal0[] = "$$$adccal0\n";
static unsigned char filamentCurrent__Msg_AdcCalH_Part[] = "$$$adccalh:";
static unsigned char filamentCurrent__Msg_AdcCalStore[] = "$$$adccalstore\n";
static unsigned char filamentCurrent__Msg_DisableProt[] = "$$$disableprotection\n";
static unsigned char filamentCurrent__Msg_EnableProt[] = "$$$enableprotection\n";


void filamentCurrent_Enable(bool bEnabled) {
    bFilament__EnableCurrent = bEnabled;
    filamentCurrent_SetCurrent(dwFilament__SetCurrent);
}
void filamentCurrent_GetId() {
    ringBuffer_WriteChars(&serialRB2_TX, filamentCurrent__Msg_ID, sizeof(filamentCurrent__Msg_ID)-1);
    serialModeTX2();
}
void filamentCurrent_GetVersion() {
    ringBuffer_WriteChars(&serialRB2_TX, filamentCurrent__Msg_Version, sizeof(filamentCurrent__Msg_Version)-1);
    serialModeTX2();
}
void filamentCurrent_SetCurrent(unsigned long int newCurrent) {
    dwFilament__SetCurrent = newCurrent;
    if(bFilament__EnableCurrent != false) {
        ringBuffer_WriteChars(&serialRB2_TX, filamentCurrent__Msg_SetCurrent_Part, sizeof(filamentCurrent__Msg_SetCurrent_Part)-1);
        ringBuffer_WriteASCIIUnsignedInt(&serialRB2_TX, newCurrent);
        ringBuffer_WriteChar(&serialRB2_TX, 0x0A);
        serialModeTX2();
    } else {
        ringBuffer_WriteChars(&serialRB2_TX, filamentCurrent__Msg_SetCurrent_Part, sizeof(filamentCurrent__Msg_SetCurrent_Part)-1);
        ringBuffer_WriteASCIIUnsignedInt(&serialRB2_TX, 0);
        ringBuffer_WriteChar(&serialRB2_TX, 0x0A);
        serialModeTX2();
    }

    rampMessage_ReportFilaCurrents();
}
unsigned long int filamentCurrent_GetCachedCurrent() {
    return dwFilament__SetCurrent;
}
void filamentCurrent_GetSetCurrent() {
    ringBuffer_WriteChars(&serialRB2_TX, filamentCurrent__Msg_GetSetCurrent, sizeof(filamentCurrent__Msg_GetSetCurrent)-1);
    serialModeTX2();
}
void filamentCurrent_GetCurrent() {
    ringBuffer_WriteChars(&serialRB2_TX, filamentCurrent__Msg_GetCurrent, sizeof(filamentCurrent__Msg_GetCurrent)-1);
    serialModeTX2();
}
void filamentCurrent_GetRawADC() {
    ringBuffer_WriteChars(&serialRB2_TX, filamentCurrent__Msg_GetCurrentADCRaw, sizeof(filamentCurrent__Msg_GetCurrentADCRaw)-1);
    serialModeTX2();
}
void filamentCurrent_CalLow() {
    ringBuffer_WriteChars(&serialRB2_TX, filamentCurrent__Msg_AdcCal0, sizeof(filamentCurrent__Msg_AdcCal0)-1);
    serialModeTX2();
}
void filamentCurrent_CalHigh(unsigned long int measuredCurrent) {
    ringBuffer_WriteChars(&serialRB2_TX, filamentCurrent__Msg_AdcCalH_Part, sizeof(filamentCurrent__Msg_AdcCalH_Part)-1);
    ringBuffer_WriteASCIIUnsignedInt(&serialRB2_TX, measuredCurrent);
    ringBuffer_WriteChar(&serialRB2_TX, 0x0A);
    serialModeTX2();
}
void filamentCurrent_CalStore() {
    ringBuffer_WriteChars(&serialRB2_TX, filamentCurrent__Msg_AdcCalStore, sizeof(filamentCurrent__Msg_AdcCalStore)-1);
    serialModeTX2();
}
void filamentCurrent_EnableProtection(bool bEnabled) {
    if(bEnabled != false) {
        ringBuffer_WriteChars(&serialRB2_TX, filamentCurrent__Msg_EnableProt, sizeof(filamentCurrent__Msg_EnableProt)-1);
    } else {
        ringBuffer_WriteChars(&serialRB2_TX, filamentCurrent__Msg_DisableProt, sizeof(filamentCurrent__Msg_DisableProt)-1);
    }
    serialModeTX2();
}
