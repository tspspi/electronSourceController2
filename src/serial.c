#include <avr/io.h>
#include <avr/interrupt.h>
#include <math.h>
#include <util/twi.h>
#include <stdint.h>
#include <string.h>

#include "./serial.h"
#include "./controller.h"
#include "./sysclock.h"

extern volatile struct cfgConfiguration configuration;

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

    for(i = 0; i < dwLen; i=i+1) {
        if(lpBuf->dwHead == lpBuf->dwTail) {
            return 0x00;
        }

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

/*
    Serial handler (UART0)

    This is an full duplex serial communication bus that's accessible
    either via USB or used by an external ethernet controller.

    The main command protocol is spoken over this bus.
*/
volatile struct ringBuffer serialRB0_TX;
volatile struct ringBuffer serialRB0_RX;

void serialModeTX0() {
    /*
        This starts the transmitter ... other than for the RS485 half duplex
        lines it does _not_ interfer with the receiver part and would not
        interfer with an already running transmitter.
    */
    #ifdef FRAMAC_SKIP
        cli();
    #endif
    UCSR0A = UCSR0A | 0x40; /* Reset TXCn bit */
    UCSR0B = UCSR0B | 0x08 | 0x20;
    #ifdef FRAMAC_SKIP
        sei();
    #endif
}
void serialInit0() {
    ringBuffer_Init(&serialRB0_TX);
    ringBuffer_Init(&serialRB0_RX);

    UBRR0   = 16; // 34;
    UCSR0A  = 0x02;
    UCSR0B  = 0x10 | 0x80; /* Enable receiver and RX interrupt */
    UCSR0C  = 0x06;

    return;
}

ISR(USART0_RX_vect) {
    #ifndef FRAMAC_SKIP
        cli();
    #endif
    ringBuffer_WriteChar(&serialRB0_RX, UDR0);
    #ifndef FRAMAC_SKIP
        sei();
    #endif
}
ISR(USART0_UDRE_vect) {
    #ifndef FRAMAC_SKIP
        cli();
    #endif

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

    #ifndef FRAMAC_SKIP
        sei();
    #endif
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


static unsigned char handleSerial0Messages_Response__ID[] = "$$$electronctrl_20210707_001\n";
static unsigned char handleSerial0Messages_Response__ERR[] = "$$$ERR\n";


static void handleSerial0Messages_CompleteMessage(
    unsigned long int dwLength
) {
    unsigned long int dwLen;
    unsigned long int dwDiscardBytes;

    /*
        We have received a complete message - now we will remove the sync
        pattern, calculate actual length
    */
    ringBuffer_discardN(&serialRB0_RX, 3); /* Skip sync pattern */
    dwLen = dwLength - 3;
    dwDiscardBytes = dwLen;

    /* Remove end of line for next parser ... */
    dwLen = dwLen - ((ringBuffer_PeekCharN(&serialRB0_RX, dwLen-1) == 0x0D) ? 1 : 0);


    /* Now copy message into a local buffer to make parsing WAY easier ... */
    ringBuffer_ReadChars(&serialRB0_RX, handleSerial0Messages_StringBuffer, dwLen);
    ringBuffer_discardN(&serialRB0_RX, dwDiscardBytes-dwLen);

    /*
        Now process that message at <handleSerial0Messages_StringBuffer, dwLen>
    */

    /*
        Parse different commands
    */
    if(strCompare("id", 2, handleSerial0Messages_StringBuffer, dwLen) == true) {
        /* Send ID response ... */
        ringBuffer_WriteChars(&serialRB0_TX, handleSerial0Messages_Response__ID, sizeof(handleSerial0Messages_Response__ID));
        serialModeTX0();
    } else {
        /* Unknown: Send error response ... */
        ringBuffer_WriteChars(&serialRB0_TX, handleSerial0Messages_Response__ERR, sizeof(handleSerial0Messages_Response__ERR));
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

    /*
        First we scan for the synchronization pattern
    */
    if(dwAvailableLength < 1) {
        return; /* Impossible */
    }

    while(ringBuffer_PeekChar(&serialRB0_RX) != '$') {
        ringBuffer_ReadChar(&serialRB0_RX); /* Skip character */
    }

    if(dwAvailableLength < 4) {
        return; /* Impossible */
    }
    if(
        (ringBuffer_PeekCharN(&serialRB0_RX, 0) != '$') ||
        (ringBuffer_PeekCharN(&serialRB0_RX, 1) != '$') ||
        (ringBuffer_PeekCharN(&serialRB0_RX, 2) != '$')
    ) {
        while(ringBuffer_PeekChar(&serialRB0_RX) == '$') {
            ringBuffer_discardN(&serialRB0_RX, 1);
        }
        return; /* We will resynchronize on the next pattern anyways ... */
    }

    /*
        Now check if we have already received a complete message OR are seeing
        another more sync pattern - in the latter case we ignore any message ...
    */
    dwMessageEnd = 3;
    while((dwMessageEnd < dwAvailableLength) && (ringBuffer_PeekCharN(&serialRB0_RX, dwMessageEnd) != 0x0A) && (ringBuffer_PeekCharN(&serialRB0_RX, dwMessageEnd) != '$')) {
        dwMessageEnd = dwMessageEnd + 1;
    }
    if(ringBuffer_PeekCharN(&serialRB0_RX, dwMessageEnd) == 0x0A) {
        /* Received full message ... */
        handleSerial0Messages_CompleteMessage(dwMessageEnd);
    }
    if(ringBuffer_PeekCharN(&serialRB0_RX, dwMessageEnd) == '$') {
        /* Discard ... we do this by simply skipping the sync pattern ... */
        ringBuffer_discardN(&serialRB0_RX, 3);
    }

    /*
        In any other case ignore and continue without dropping the message ...
        we will wait till we received the whole message (note that means
        busy waiting in some way in this simple implementation - one could
        set a flag later on (TODO) ...)
    */
    return;
}
