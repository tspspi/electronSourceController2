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

/*
    ADC counts to current or voltage:

        HCP 14-6500:        max. 6.5 kV, 2 mA <-> 0...10V

        0...5V <--> 3.25 kV, 1 mA
        5V <-> 1024 ADC counts
        1 ADC count <-> 0.0048828125V
            0.0048828125 <-> 0.003173828125 kV = 3.1738 V
                         <-> 0.0009765625 mA = 0.9765625 uA
*/
static inline uint16_t serialADC2VoltsHCP(
    uint16_t adcCounts
) {
    return (uint16_t)((double)(adcCounts) * 3.1738 * 1.015);
}
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
static inline void ringBuffer_Init(volatile struct ringBuffer* lpBuf) {
    lpBuf->dwHead = 0;
    lpBuf->dwTail = 0;
}
static inline bool ringBuffer_Available(volatile struct ringBuffer* lpBuf) {
    return (lpBuf->dwHead != lpBuf->dwTail) ? true : false;
}
static inline bool ringBuffer_Writable(volatile struct ringBuffer* lpBuf) {
    return (((lpBuf->dwHead + 1) % SERIAL_RINGBUFFER_SIZE) != lpBuf->dwTail) ? true : false;
}
static inline unsigned long int ringBuffer_AvailableN(volatile struct ringBuffer* lpBuf) {
    if(lpBuf->dwHead >= lpBuf->dwTail) {
        return lpBuf->dwHead - lpBuf->dwTail;
    } else {
        return (SERIAL_RINGBUFFER_SIZE - lpBuf->dwTail) + lpBuf->dwHead;
    }
}
static inline unsigned long int ringBuffer_WriteableN(volatile struct ringBuffer* lpBuf) {
    return SERIAL_RINGBUFFER_SIZE - ringBuffer_AvailableN(lpBuf);
}

static unsigned char ringBuffer_ReadChar(volatile struct ringBuffer* lpBuf) {
    char t;

    if(lpBuf->dwHead == lpBuf->dwTail) {
        return 0x00;
    }

    t = lpBuf->buffer[lpBuf->dwTail];
    lpBuf->dwTail = (lpBuf->dwTail + 1) % SERIAL_RINGBUFFER_SIZE;

    return t;
}
static unsigned char ringBuffer_PeekChar(volatile struct ringBuffer* lpBuf) {
    if(lpBuf->dwHead == lpBuf->dwTail) {
        return 0x00;
    }

    return lpBuf->buffer[lpBuf->dwTail];
}
static unsigned char ringBuffer_PeekCharN(
    volatile struct ringBuffer* lpBuf,
    unsigned long int dwDistance
) {
    if(lpBuf->dwHead == lpBuf->dwTail) {
        return 0x00;
    }
    if(ringBuffer_AvailableN(lpBuf) <= dwDistance) {
        return 0x00;
    }

    return lpBuf->buffer[(lpBuf->dwTail + dwDistance) % SERIAL_RINGBUFFER_SIZE];
}
static inline void ringBuffer_discardN(
    volatile struct ringBuffer* lpBuf,
    unsigned long int dwCount
) {
    lpBuf->dwTail = (lpBuf->dwTail + dwCount) % SERIAL_RINGBUFFER_SIZE;
    return;
}
static unsigned long int ringBuffer_ReadChars(
    volatile struct ringBuffer* lpBuf,
    unsigned char* lpOut,
    unsigned long int dwLen
) {
    char t;
    unsigned long int i;

    if(dwLen > ringBuffer_AvailableN(lpBuf)) {
        return 0;
    }

    for(i = 0; i < dwLen; i=i+1) {
        t = lpBuf->buffer[lpBuf->dwTail];
        lpBuf->dwTail = (lpBuf->dwTail + 1) % SERIAL_RINGBUFFER_SIZE;
        lpOut[i] = t;
    }

    return i;
}

static void ringBuffer_WriteChar(
    volatile struct ringBuffer* lpBuf,
    unsigned char bData
) {
    if(((lpBuf->dwHead + 1) % SERIAL_RINGBUFFER_SIZE) == lpBuf->dwTail) {
        return; /* Simply discard data */
    }

    lpBuf->buffer[lpBuf->dwHead] = bData;
    lpBuf->dwHead = (lpBuf->dwHead + 1) % SERIAL_RINGBUFFER_SIZE;
}
static void ringBuffer_WriteChars(
    volatile struct ringBuffer* lpBuf,
    unsigned char* bData,
    unsigned long int dwLen
) {
    unsigned long int i;

    for(i = 0; i < dwLen; i=i+1) {
        ringBuffer_WriteChar(lpBuf, bData[i]);
    }
}

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
        while(current != 0) {
            bTemp[len] = ((uint8_t)(current % 10)) + 0x30;
            len = len + 1;
            current = current / 10;
        }

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
    #ifndef FRAMAC_SKIP
        SREG = sregOld;
    #endif
}
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
ISR(USART0_RX_vect) {
    ringBuffer_WriteChar(&serialRB0_RX, UDR0);
    serialRXFlag = 1;
}
ISR(USART0_UDRE_vect) {
    if(ringBuffer_Available(&serialRB0_TX) == true) {
        /* Shift next byte to the outside world ... */
        UDR0 = ringBuffer_ReadChar(&serialRB0_TX);
    } else {
        /*
            Since no more data is available for shifting simply stop
            the transmitter and associated interrupts
        */
        UCSR0B = UCSR0B & (~(0x08 | 0x20));
    }
}

/*
    =================================================
    = Command handler for serial protocol on USART0 =
    =================================================
*/

static unsigned char handleSerial0Messages_StringBuffer[SERIAL_RINGBUFFER_SIZE];

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
static inline char strCasefoldIfChar(char c) {
    if((c >= 0x41) && (c <= 0x5A)) { c = c + 0x20; }
    return c;
}
static bool strCompare(
    char* lpA,
    unsigned long int dwLenA,
    unsigned char* lpB,
    unsigned long int dwLenB
) {
    unsigned long int i;

    if(dwLenA != dwLenB) { return false; }
    for(i = 0; i < dwLenA; i=i+1) {
        if(lpA[i] != lpB[i]) {
            return false;
        }
    }

    return true;
}
static bool strComparePrefix(
    char* lpA,
    unsigned long int dwLenA,
    unsigned char* lpB,
    unsigned long int dwLenB
) {
    unsigned long int i;

    if(dwLenA > dwLenB) { return false; }
    for(i = 0; i < dwLenA; i=i+1) {
        if(lpA[i] != lpB[i]) {
            return false;
        }
    }

    return true;
}
static uint32_t strASCIIToDecimal(
    uint8_t* lpStr,
    unsigned long int dwLen
) {
    unsigned long int i;
    uint8_t currentChar;
    uint32_t currentValue = 0;

    for(i = 0; i < dwLen; i=i+1) {
        currentChar = lpStr[i];
        if((currentChar >= 0x30) && (currentChar <= 0x39)) {
            currentChar = currentChar - 0x30;
            currentValue = currentValue * 10 + currentChar;
        }
    }    return currentValue;
}

static unsigned char handleSerial0Messages_Response__ID[] = "$$$electronctrl_20210707_001\n";
static unsigned char handleSerial0Messages_Response__ERR[] = "$$$err\n";
static unsigned char handleSerial0Messages_Response__VN_Part[] = "$$$v";
static unsigned char handleSerial0Messages_Response__AN_Part[] = "$$$a";
static unsigned char handleSerial0Messages_Response__PSUSTATE_Part[] = "$$$psustate";

static void handleSerial0Messages_CompleteMessage(
    unsigned long int dwLength
) {
    unsigned long int dwLen;
#if 0
    unsigned long int dwDiscardBytes;
#endif

    /*
        We have received a complete message - now we will remove the sync
        pattern, calculate actual length
    */
    ringBuffer_discardN(&serialRB0_RX, 3); /* Skip sync pattern */
    dwLen = dwLength - 3;
#if 0
    dwDiscardBytes = dwLen; /* Remember how many bytes we have to skip in the end ... */
#endif

    /* Remove end of line for next parser ... */
    dwLen = dwLen - 1; /* Remove LF */
    dwLen = dwLen - ((ringBuffer_PeekCharN(&serialRB0_RX, dwLen-1) == 0x0D) ? 1 : 0); /* Remove CR if present */


    /* Now copy message into a local buffer to make parsing WAY easier ... */
    ringBuffer_ReadChars(&serialRB0_RX, handleSerial0Messages_StringBuffer, dwLen);
#if 0
    ringBuffer_discardN(&serialRB0_RX, dwDiscardBytes-dwLen);
#endif
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
    } else if(strCompare("psupol1n", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[0].polPolarity = psuPolarity_Negative;
    } else if(strCompare("psupol2p", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[1].polPolarity = psuPolarity_Positive;
    } else if(strCompare("psupol2n", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[1].polPolarity = psuPolarity_Negative;
    } else if(strCompare("psupol3p", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[2].polPolarity = psuPolarity_Positive;
    } else if(strCompare("psupol3n", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[2].polPolarity = psuPolarity_Negative;
    } else if(strCompare("psupol4p", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[3].polPolarity = psuPolarity_Positive;
    } else if(strCompare("psupol4n", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[3].polPolarity = psuPolarity_Negative;
    } else if(strCompare("psuon1", 6, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[0].bOutputEnable = true;
    } else if(strCompare("psuoff1", 7, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[0].bOutputEnable = false;
    } else if(strCompare("psuon2", 6, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[1].bOutputEnable = true;
    } else if(strCompare("psuoff2", 7, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[1].bOutputEnable = false;
    } else if(strCompare("psuon3", 6, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[2].bOutputEnable = true;
    } else if(strCompare("psuoff3", 7, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[2].bOutputEnable = false;
    } else if(strCompare("psuon4", 6, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[3].bOutputEnable = true;
    } else if(strCompare("psuoff4", 7, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[3].bOutputEnable = false;
    } else if(strCompare("off", 3, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[0].bOutputEnable = false;
        psuStates[1].bOutputEnable = false;
        psuStates[2].bOutputEnable = false;
        psuStates[3].bOutputEnable = false;
        setFilamentOn(false);
    } else if(strCompare("filon", 5, handleSerial0Messages_StringBuffer, dwLen) == true) {
        setFilamentOn(true);
    } else if(strCompare("filoff", 6, handleSerial0Messages_StringBuffer, dwLen) == true) {
        setFilamentOn(false);
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
    } else if(strComparePrefix("psusetv2", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        setPSUVolts(strASCIIToDecimal(&(handleSerial0Messages_StringBuffer[8]), dwLen-8), 2);
    } else if(strComparePrefix("psusetv3", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        setPSUVolts(strASCIIToDecimal(&(handleSerial0Messages_StringBuffer[8]), dwLen-8), 3);
    } else if(strComparePrefix("psusetv4", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        setPSUVolts(strASCIIToDecimal(&(handleSerial0Messages_StringBuffer[8]), dwLen-8), 4);
    } else if(strComparePrefix("psuseta1", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        setPSUMicroamps(strASCIIToDecimal(&(handleSerial0Messages_StringBuffer[8]), dwLen-8), 1);
    } else if(strComparePrefix("psuseta2", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        setPSUMicroamps(strASCIIToDecimal(&(handleSerial0Messages_StringBuffer[8]), dwLen-8), 2);
    } else if(strComparePrefix("psuseta3", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        setPSUMicroamps(strASCIIToDecimal(&(handleSerial0Messages_StringBuffer[8]), dwLen-8), 3);
    } else if(strComparePrefix("psuseta4", 8, handleSerial0Messages_StringBuffer, dwLen) == true) {
        setPSUMicroamps(strASCIIToDecimal(&(handleSerial0Messages_StringBuffer[8]), dwLen-8), 4);
    } else if(strComparePrefix("fila", 4, handleSerial0Messages_StringBuffer, dwLen) == true) {
        uint16_t a;
        {
            cli();
            a = currentADC[8];
            sei();
        }
        a = serialADC2MilliampsFILA(a);
        ringBuffer_WriteChars(&serialRB0_TX, handleSerial0Messages_Response__AN_Part, sizeof(handleSerial0Messages_Response__AN_Part)-1);
        ringBuffer_WriteChar(&serialRB0_TX, 'f');
        ringBuffer_WriteChar(&serialRB0_TX, ':');
        ringBuffer_WriteASCIIUnsignedInt(&serialRB0_TX, a);
        ringBuffer_WriteChar(&serialRB0_TX, 0x0A);
        serialModeTX0();
    } else if(strComparePrefix("setfila", 7, handleSerial0Messages_StringBuffer, dwLen) == true) {
        /* Currently setting PWM cycles instead of mA, will require calibration with working filament ... */
        setFilamentPWM(strASCIIToDecimal(&(handleSerial0Messages_StringBuffer[7]), dwLen-7));
    } else if(strCompare("insul", 5, handleSerial0Messages_StringBuffer, dwLen) == true) {
        rampStart_InsulationTest();
    } else if(strCompare("beamhvoff", 9, handleSerial0Messages_StringBuffer, dwLen) == true) {
        psuStates[0].bOutputEnable = false;
        psuStates[1].bOutputEnable = false;
        psuStates[2].bOutputEnable = false;
        psuStates[3].bOutputEnable = false;
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
            while(adcValue > 0) {
                uint16_t nextVal = adcValue % 10;
                adcValue = adcValue / 10;

                nextVal = nextVal + 0x30;
                bTemp[len] = nextVal;
                len = len + 1;
            }

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
        Check if we received new data. It makes no sense to re-check message formats
        in case nothing arrived.
    */
#if 0
    if(serialRXFlag == 0) { return; }
    serialRXFlag = 0;
#endif
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
    while((ringBuffer_PeekChar(&serialRB0_RX) != '$') && (ringBuffer_AvailableN(&serialRB0_RX) > 3)) {
        ringBuffer_discardN(&serialRB0_RX, 1); /* Skip next character */
    }

    /* If we are too short to fit the entire synchronization packet - wait for additional data to arrive */
    if(ringBuffer_AvailableN(&serialRB0_RX) < 5) { return; }

    /*
        Discard additional bytes in case we don't see the full sync pattern and
        as long as data is available
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


void rampMessage_ReportVoltages() {
    unsigned long int i;
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
    }
    serialModeTX0();
}

static unsigned char rampMessage_InsulationTestSuccess__Message[] = "$$$insulok\n";
void rampMessage_InsulationTestSuccess() {
    ringBuffer_WriteChars(&serialRB0_TX, rampMessage_InsulationTestSuccess__Message, sizeof(rampMessage_InsulationTestSuccess__Message)-1);
    serialModeTX0();
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
}
