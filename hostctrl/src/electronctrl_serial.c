#ifndef ELECTRONCTRL_SERIAL__RINGBUFFER_SIZE
    #define ELECTRONCTRL_SERIAL__RINGBUFFER_SIZE    512
#endif

#include <stdlib.h>
#if defined(__FreeBSD__) || defined(__linux__)
    #ifdef DEBUG
        #include <stdio.h>
    #endif
    #include <fcntl.h>
    #include <unistd.h>
    #include <termios.h>
    #include <errno.h>
    #include <string.h>

    #if defined(__FreeBSD__)
        #include <sys/event.h>
    #endif

    #include <pthread.h>
#endif
#include "./electronctrl.h"

#if defined(__FreeBSD__)
    #define ELECTRONCTRL_SERIAL__PROCESSINGHTREAD_EVENT__TERMINATE      0x00000001
#endif

#if defined(__FreeBSD__) || defined(__linux__)
    /*
        Helper routines for serial port handling - setting basic parameters
        and async operation
    */
    static enum egunError setInterfaceAttributes(
        int hPort,
        int speed,
        int parity
    ) {
        struct termios tty;
        if(tcgetattr(hPort, &tty) != 0) {
            #ifdef DEBUG
                printf("%s:%u Get attributes failed: %d\n", __FILE__, __LINE__, errno);
            #endif
            return egunE_Failed;
        }

        cfsetospeed(&tty, speed);
        cfsetispeed(&tty, speed);

        tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     /* 8 bit bytes */
        tty.c_iflag &= ~IGNBRK;                         /* no break processing */
        tty.c_lflag = 0;                            /* No echo, canonical processing, remapping, delays, etc. */
        tty.c_oflag = 0;
        tty.c_cc[VMIN]  = 0;                        /* No block for read */
        tty.c_cc[VTIME] = 5;                        /* Half second of read timeout */
        tty.c_iflag &= ~(IXON | IXOFF | IXANY);     /* No XON/XOFF flow control */
        tty.c_cflag |= (CLOCAL | CREAD);            /* No modem controls ... */
        tty.c_cflag &= ~(PARENB | PARODD);          /* Disable partiy */
        tty.c_cflag |= parity;                      /* And set partiy flag from parameters */
        tty.c_cflag &= ~CSTOPB;                     /* Only 1 stop bit */
        tty.c_cflag &= ~CRTSCTS;                    /* No RTS/CTS flow control */

        if(tcsetattr (hPort, TCSANOW, &tty) != 0) {
            #ifdef DEBUG
                printf("%s:%u Setting attributed failed: %d\n", __FILE__, __LINE__, errno);
            #endif
            return egunE_Failed;
        }
        return egunE_Ok;
    }

    static enum egunError setInterfaceBlocking(int hPort, int bShouldBlock) {
        struct termios tty;

        memset(&tty, 0, sizeof tty);

        if(tcgetattr(hPort, &tty) != 0) {
            #ifdef DEBUG
                printf("%s:%u Failed to get current attributes: %d\n", __FILE__, __LINE__, errno);
            #endif
            return egunE_Failed;
        }

        tty.c_cc[VMIN]  = bShouldBlock ? 1 : 0;
        tty.c_cc[VTIME] = 5;

        if(tcsetattr(hPort, TCSANOW, &tty) != 0) {
            #ifdef DEBUG
                printf("%s:%u Failed to set attributes: %d\n", __FILE__, __LINE__, errno);
            #endif
            return egunE_Failed;
        }
        return egunE_Ok;
    }
#endif

#ifdef __cplusplus
    extern "C" {
#endif

struct egunSerial_Impl {
    struct electronGun          objEgun;

    #if defined(__FreeBSD__) || defined(__linux__)
        int                     hSerialPort;
        pthread_t               thrThread;
    #endif
    #if defined(__FreeBSD__)
        int                     kq;
    #endif

    struct {
        char                    bData[ELECTRONCTRL_SERIAL__RINGBUFFER_SIZE];
        unsigned long int       dwHead;
        unsigned long int       dwTail;
    } ringbufferIn;
};


static enum egunError egunSerial__Release(
    struct electronGun* lpSelf
) {
    struct egunSerial_Impl* lpThis;
    #if defined(__FreeBSD__)
        struct kevent kev;
    #endif

    if(lpSelf == NULL) {
        return egunE_InvalidParam;
    }

    lpThis = (struct egunSerial_Impl*)(lpSelf->lpReserved);

    /*
        Shutdown and release requires:
            * Closing the serial port handle (this will remove kevent references
              for the kqueue)
            * Posting the termination event
            * Stopping the thread / waiting for the thread to terminate. This
              is easy for pthreads since the thread will terminate itself & we
              will just join on it ...
            * Releasing memory
    */
    #if defined(__FreeBSD__) || defined(__linux__)
        close(lpThis->hSerialPort);
        lpThis->hSerialPort = -1;

        EV_SET(&kev, ELECTRONCTRL_SERIAL__PROCESSINGHTREAD_EVENT__TERMINATE, EVFILT_USER, EV_ENABLE, NOTE_TRIGGER, 0, NULL);
        kevent(lpThis->kq, &kev, 1, NULL, 0, NULL);

        pthread_join(lpThis->thrThread, NULL);
        free(lpThis);

        return egunE_Ok;
    #else
            #error "Missing implementation"
    #endif

    return egunE_Failed;
}


static char egunSerial__RequestID__Message[] = "$$$id\n";
static enum egunError egunSerial__RequestID(
    struct electronGun* lpSelf
) {
    struct egunSerial_Impl* lpThis;
    int r;

    if(lpSelf == NULL) { return egunE_InvalidParam; }
    lpThis = (struct egunSerial_Impl*)(lpSelf->lpReserved);

    r = write(lpThis->hSerialPort, egunSerial__RequestID__Message, sizeof(egunSerial__RequestID__Message)-1);

    if(r != (sizeof(egunSerial__RequestID__Message)-1)) {
        return egunE_Failed;
    } else {
        return egunE_Ok;
    }
}

static char egunSerial__electronGun_GetCurrentVoltage__MessageFmt[] = "$$$psugetv%u\n";
static enum egunError egunSerial__electronGun_GetCurrentVoltage(
    struct electronGun* lpSelf,
    unsigned long int dwPSUIndex
) {
    struct egunSerial_Impl* lpThis;
    int r;

    if(lpSelf == NULL) { return egunE_InvalidParam; }
    if((dwPSUIndex < 1) || (dwPSUIndex > 4)) { return egunE_InvalidParam; }

    lpThis = (struct egunSerial_Impl*)(lpSelf->lpReserved);

    char bCommand[strlen(egunSerial__electronGun_GetCurrentVoltage__MessageFmt)]; /* Note: Do not require +1 since %u occupies two places */

    sprintf(bCommand, egunSerial__electronGun_GetCurrentVoltage__MessageFmt, dwPSUIndex);

    r = write(lpThis->hSerialPort, bCommand, strlen(bCommand));
    if(r != strlen(bCommand)) {
        return egunE_Failed;
    } else {
        return egunE_Ok;
    }
}
static char egunSerial__electronGun_GetCurrentCurrent__MessageFmt[] = "$$$psugeta%u\n";
static enum egunError egunSerial__electronGun_GetCurrentCurrent(
    struct electronGun* lpSelf,
    unsigned long int dwPSUIndex
) {
    struct egunSerial_Impl* lpThis;
    int r;

    if(lpSelf == NULL) { return egunE_InvalidParam; }
    if((dwPSUIndex < 1) || (dwPSUIndex > 4)) { return egunE_InvalidParam; }

    lpThis = (struct egunSerial_Impl*)(lpSelf->lpReserved);

    char bCommand[strlen(egunSerial__electronGun_GetCurrentCurrent__MessageFmt)]; /* Note: Do not require +1 since %u occupies two places */

    sprintf(bCommand, egunSerial__electronGun_GetCurrentCurrent__MessageFmt, dwPSUIndex);
    r = write(lpThis->hSerialPort, bCommand, strlen(bCommand));
    if(r != strlen(bCommand)) {
        return egunE_Failed;
    } else {
        return egunE_Ok;
    }
}
static char egunSerial__electronGun_GetPSUModes__Message[] = "$$$psumode\n";
static enum egunError egunSerial__electronGun_GetPSUModes(
    struct electronGun* lpSelf
) {
    struct egunSerial_Impl* lpThis;
    int r;

    if(lpSelf == NULL) { return egunE_InvalidParam; }

    lpThis = (struct egunSerial_Impl*)(lpSelf->lpReserved);

    r = write(lpThis->hSerialPort, egunSerial__electronGun_GetPSUModes__Message, strlen(egunSerial__electronGun_GetPSUModes__Message));
    if(r != strlen(egunSerial__electronGun_GetPSUModes__Message)) {
        return egunE_Failed;
    } else {
        return egunE_Ok;
    }
}


static char egunSerial__electronGun_GetFilamentCurrent__MessageFmt[] = "$$$fila\n";
static enum egunError egunSerial__electronGun_GetFilamentCurrent(
    struct electronGun* lpSelf
) {
    struct egunSerial_Impl* lpThis;
    int r;

    if(lpSelf == NULL) { return egunE_InvalidParam; }

    lpThis = (struct egunSerial_Impl*)(lpSelf->lpReserved);

    r = write(lpThis->hSerialPort, egunSerial__electronGun_GetFilamentCurrent__MessageFmt, strlen(egunSerial__electronGun_GetFilamentCurrent__MessageFmt));
    if(r != strlen(egunSerial__electronGun_GetFilamentCurrent__MessageFmt)) {
        return egunE_Failed;
    } else {
        return egunE_Ok;
    }
}

static char egunSerial__electronGun_Off__Message[] = "$$$off\n";
static enum egunError egunSerial__electronGun_Off(
    struct electronGun* lpSelf
) {
    struct egunSerial_Impl* lpThis;
    int r;

    if(lpSelf == NULL) { return egunE_InvalidParam; }

    lpThis = (struct egunSerial_Impl*)(lpSelf->lpReserved);
    r = write(lpThis->hSerialPort, egunSerial__electronGun_Off__Message, strlen(egunSerial__electronGun_Off__Message));
    if(r != strlen(egunSerial__electronGun_Off__Message)) {
        return egunE_Failed;
    } else {
        return egunE_Ok;
    }
}

static char egunSerial__electronGun_NoProtection__Message[] = "$$$noprotection\n";
static enum egunError egunSerial__electronGun_NoProtection(
    struct electronGun* lpSelf
) {
    struct egunSerial_Impl* lpThis;
    int r;

    if(lpSelf == NULL) { return egunE_InvalidParam; }

    lpThis = (struct egunSerial_Impl*)(lpSelf->lpReserved);
    r = write(lpThis->hSerialPort, egunSerial__electronGun_NoProtection__Message, strlen(egunSerial__electronGun_NoProtection__Message));
    if(r != strlen(egunSerial__electronGun_NoProtection__Message)) {
        return egunE_Failed;
    } else {
        return egunE_Ok;
    }
}

static char egunSerial__electronGun_SetPSUPolarity__MessageFmt[] = "$$$psupol%u%c\n";
static enum egunError egunSerial__electronGun_SetPSUPolarity(
    struct electronGun* lpSelf,
    unsigned long int dwPSUIndex,
    enum egunPolarity polPolarity
) {
    struct egunSerial_Impl* lpThis;
    int r;

    if(lpSelf == NULL) { return egunE_InvalidParam; }
    if((dwPSUIndex < 1) || (dwPSUIndex > 4)) { return egunE_InvalidParam; }
    if((polPolarity != egunPolarity_Pos) && (polPolarity != egunPolarity_Neg)) { return egunE_InvalidParam; }

    lpThis = (struct egunSerial_Impl*)(lpSelf->lpReserved);

    char bCommand[strlen(egunSerial__electronGun_GetCurrentCurrent__MessageFmt)-2];

    sprintf(bCommand, egunSerial__electronGun_SetPSUPolarity__MessageFmt, dwPSUIndex, (polPolarity == egunPolarity_Pos) ? 'p' : 'n');
    r = write(lpThis->hSerialPort, bCommand, strlen(bCommand));
    if(r != strlen(bCommand)) {
        return egunE_Failed;
    } else {
        return egunE_Ok;
    }
}

static char egunSerial__electronGun_SetPSUOn__MessageFmt[] = "$$$psuon%u\n";
static char egunSerial__electronGun_SetPSUOff__MessageFmt[] = "$$$psuoff%u\n";
static enum egunError egunSerial__electronGun_SetPSUEnabled(
    struct electronGun* lpSelf,
    unsigned long int dwPSUIndex,
    bool bEnable
) {
    struct egunSerial_Impl* lpThis;
    int r;

    if(lpSelf == NULL) { return egunE_InvalidParam; }
    if((dwPSUIndex < 1) || (dwPSUIndex > 4)) { return egunE_InvalidParam; }

    lpThis = (struct egunSerial_Impl*)(lpSelf->lpReserved);

    char bCommand[strlen(egunSerial__electronGun_GetCurrentCurrent__MessageFmt)];

    sprintf(bCommand, (bEnable == true) ? egunSerial__electronGun_SetPSUOn__MessageFmt : egunSerial__electronGun_SetPSUOff__MessageFmt, dwPSUIndex);
    r = write(lpThis->hSerialPort, bCommand, strlen(bCommand));
    if(r != strlen(bCommand)) {
        return egunE_Failed;
    } else {
        return egunE_Ok;
    }
}

static char egunSerial__electronGun_SetVoltage__MessageFmt[] = "$$$psusetv%u%u\n";
static enum egunError egunSerial__electronGun_SetVoltage(
    struct electronGun* lpSelf,
    unsigned long int dwPSUIndex,
    unsigned long int dwVolts
) {
    struct egunSerial_Impl* lpThis;
    char bCommand[32];
    int r;

    if(lpSelf == NULL) { return egunE_InvalidParam; }
    if((dwPSUIndex < 1) || (dwPSUIndex > 4)) { return egunE_InvalidParam; }
    if(dwVolts > 3250) { return egunE_InvalidParam; }

    lpThis = (struct egunSerial_Impl*)(lpSelf->lpReserved);

    sprintf(bCommand, egunSerial__electronGun_SetVoltage__MessageFmt, dwPSUIndex, dwVolts);
    r = write(lpThis->hSerialPort, bCommand, strlen(bCommand));
    if(r != strlen(bCommand)) {
        return egunE_Failed;
    } else {
        return egunE_Ok;
    }
}
static char egunSerial__electronGun_SetCurrent__MessageFmt[] = "$$$psuseta%u%u\n";
static enum egunError egunSerial__electronGun_SetCurrent(
    struct electronGun* lpSelf,
    unsigned long int dwPSUIndex,
    unsigned long int dwMicroamps
) {
    struct egunSerial_Impl* lpThis;
    char bCommand[32];
    int r;

    if(lpSelf == NULL) { return egunE_InvalidParam; }
    if((dwPSUIndex < 1) || (dwPSUIndex > 4)) { return egunE_InvalidParam; }
    if(dwMicroamps > 1000) { return egunE_InvalidParam; }

    lpThis = (struct egunSerial_Impl*)(lpSelf->lpReserved);

    sprintf(bCommand, egunSerial__electronGun_SetCurrent__MessageFmt, dwPSUIndex, dwMicroamps);
    r = write(lpThis->hSerialPort, bCommand, strlen(bCommand));
    if(r != strlen(bCommand)) {
        return egunE_Failed;
    } else {
        return egunE_Ok;
    }
}

static char egunSerial__electronGun_SetFilamentCurrent__MessageFmt[] = "$$$setfila%u\n";
static enum egunError egunSerial__electronGun_SetFilamentCurrent(
    struct electronGun* lpSelf,
    uint16_t wCurrent
) {
    struct egunSerial_Impl* lpThis;
    char bCommand[32];
    int r;

    if(lpSelf == NULL) { return egunE_InvalidParam; }
    /* if(wCurrent > 0x7F) { return egunE_InvalidParam; } */

    lpThis = (struct egunSerial_Impl*)(lpSelf->lpReserved);
    sprintf(bCommand, egunSerial__electronGun_SetFilamentCurrent__MessageFmt, wCurrent);
    r = write(lpThis->hSerialPort, bCommand, strlen(bCommand));
    if(r != strlen(bCommand)) {
        return egunE_Failed;
    } else {
        return egunE_Ok;
    }
}

static char egunSerial__electronGun_SetFilamentOn__OnMsg[]  = "$$$filon\n";
static char egunSerial__electronGun_SetFilamentOn__OffMsg[] = "$$$filoff\n";
static enum egunError egunSerial__electronGun_SetFilamentOn(
    struct electronGun* lpSelf,
    bool bOn
) {
    struct egunSerial_Impl* lpThis;
    char* lpCmd;
    int r;

    if(lpSelf == NULL) { return egunE_InvalidParam; }
    lpThis = (struct egunSerial_Impl*)(lpSelf->lpReserved);

    lpCmd = (bOn != false) ? egunSerial__electronGun_SetFilamentOn__OnMsg : egunSerial__electronGun_SetFilamentOn__OffMsg;

    r = write(lpThis->hSerialPort, lpCmd, strlen(lpCmd));
    if(r != strlen(lpCmd)) {
        return egunE_Failed;
    } else {
        return egunE_Ok;
    }
}

static char egunSerial__electronGun_InsulationTest_Message[] = "$$$insul\n";
static enum egunError egunSerial__electronGun_InsulationTest(
    struct electronGun* lpSelf
) {
    struct egunSerial_Impl* lpThis;
    int r;

    if(lpSelf == NULL) { return egunE_InvalidParam; }
    lpThis = (struct egunSerial_Impl*)(lpSelf->lpReserved);

    r = write(lpThis->hSerialPort, egunSerial__electronGun_InsulationTest_Message, strlen(egunSerial__electronGun_InsulationTest_Message));
    if(r != strlen(egunSerial__electronGun_InsulationTest_Message)) {
        return egunE_Failed;
    } else {
        return egunE_Ok;
    }
}

static char egunSerial__electronGun_BeamOn_Message[] = "$$$beamon\n";
static enum egunError egunSerial__electronGun_BeamOn(
    struct electronGun* lpSelf
) {
    struct egunSerial_Impl* lpThis;
    int r;

    if(lpSelf == NULL) { return egunE_InvalidParam; }
    lpThis = (struct egunSerial_Impl*)(lpSelf->lpReserved);

    r = write(lpThis->hSerialPort, egunSerial__electronGun_BeamOn_Message, strlen(egunSerial__electronGun_BeamOn_Message));
    if(r != strlen(egunSerial__electronGun_BeamOn_Message)) {
        return egunE_Failed;
    } else {
        return egunE_Ok;
    }
}



struct electronGun_VTBL egunSerial_VTBL = {
    &egunSerial__Release,
    &egunSerial__RequestID,
    &egunSerial__electronGun_GetCurrentVoltage,
    &egunSerial__electronGun_GetCurrentCurrent,
    &egunSerial__electronGun_GetPSUModes,

    &egunSerial__electronGun_Off,
    &egunSerial__electronGun_NoProtection,

    &egunSerial__electronGun_SetPSUPolarity,
    &egunSerial__electronGun_SetPSUEnabled,

    &egunSerial__electronGun_SetVoltage,
    &egunSerial__electronGun_SetCurrent,

    &egunSerial__electronGun_GetFilamentCurrent,
    &egunSerial__electronGun_SetFilamentCurrent,
    &egunSerial__electronGun_SetFilamentOn,

    &egunSerial__electronGun_InsulationTest,
    &egunSerial__electronGun_BeamOn
};


#if defined(__FreeBSD__) || defined(__linux__)
    static char* egunSerial_DefaultDevices[] = {
        "/dev/ttyU0"
    };
    #define egunSerial_DefaultDevices_LEN (sizeof(egunSerial_DefaultDevices)/sizeof(char*))
#endif


static inline unsigned long int egunSerial_ProcessingThread_HandleSerialData__RBAvail(
    struct egunSerial_Impl* lpThis
) {
    if(lpThis->ringbufferIn.dwHead >= lpThis->ringbufferIn.dwTail) {
        return lpThis->ringbufferIn.dwHead - lpThis->ringbufferIn.dwTail;
    } else {
        return (ELECTRONCTRL_SERIAL__RINGBUFFER_SIZE - lpThis->ringbufferIn.dwTail) + lpThis->ringbufferIn.dwHead;
    }
}
static inline void egunSerial_ProcessingThread_HandleSerialData__Discard(
    struct egunSerial_Impl* lpThis,
    unsigned long int dwLen
) {
    if(dwLen > egunSerial_ProcessingThread_HandleSerialData__RBAvail(lpThis)) {
        lpThis->ringbufferIn.dwTail = lpThis->ringbufferIn.dwHead;
    } else {
        lpThis->ringbufferIn.dwTail = (lpThis->ringbufferIn.dwTail + dwLen) % ELECTRONCTRL_SERIAL__RINGBUFFER_SIZE;
    }
}
static inline char egunSerial_ProcessingThread_HandleSerialData__RBPeek(
    struct egunSerial_Impl* lpThis,
    unsigned long int dwDistance
) {
    if(dwDistance >= egunSerial_ProcessingThread_HandleSerialData__RBAvail(lpThis)) {
        return 0x00; /* Simply return a zero for out of bounds ... */
    }
    return lpThis->ringbufferIn.bData[(lpThis->ringbufferIn.dwTail + dwDistance) % ELECTRONCTRL_SERIAL__RINGBUFFER_SIZE];
}

static void egunSerial_ProcessingThread_HandleSerialData_MsgInRingbuffer(
    struct egunSerial_Impl* lpThis,
    unsigned long int dwLen
) {
    unsigned long int i;
    printf("Received message: ");
    for(i = 0; i < dwLen; i=i+1) {
        printf("%c", egunSerial_ProcessingThread_HandleSerialData__RBPeek(lpThis, i));
    }
    printf("\n");
    egunSerial_ProcessingThread_HandleSerialData__Discard(lpThis, dwLen);
}

static void egunSerial_ProcessingThread_HandleSerialData(
    struct egunSerial_Impl* lpThis
) {
    char bData;
    int r;
    unsigned long int i;

    r = read(lpThis->hSerialPort, &bData, 1);
    if(r > 0) {
        /* Add to ringbuffer */
        if(((lpThis->ringbufferIn.dwHead+1) % ELECTRONCTRL_SERIAL__RINGBUFFER_SIZE) == lpThis->ringbufferIn.dwTail) {
            #ifdef DEBUG
                printf("%s:%u Ringbuffer overflow. Dropping data\n", __FILE__, __LINE__);
            #endif
            return; /* Ignore anything that would lead to an overflow in the ringbuffer */
        }

        lpThis->ringbufferIn.bData[lpThis->ringbufferIn.dwHead] = bData;
        lpThis->ringbufferIn.dwHead = (lpThis->ringbufferIn.dwHead + 1) % ELECTRONCTRL_SERIAL__RINGBUFFER_SIZE;
    } else {
        return;
    }

    /* Check if we received a full message or garbage ... */
    for(;;) {
        for(;;) {
            if(egunSerial_ProcessingThread_HandleSerialData__RBAvail(lpThis) < 4) { return; }

            /* Locate synchronization pattern */
            if(
                (egunSerial_ProcessingThread_HandleSerialData__RBPeek(lpThis,0) == '$')
                && (egunSerial_ProcessingThread_HandleSerialData__RBPeek(lpThis,1) == '$')
                && (egunSerial_ProcessingThread_HandleSerialData__RBPeek(lpThis,2) == '$')
            ) {
                break;
            }

            /* Discard first byte since the sync pattern doesn't seem to be fully present */
            egunSerial_ProcessingThread_HandleSerialData__Discard(lpThis, 1);
        }

        /*
            We found a sync pattern at position 0 ...
            Check if we received a full message by scanning for a LF. In case
            we encounter another sync pattern discard everything up until there ...
        */
        for(i = 3; i < egunSerial_ProcessingThread_HandleSerialData__RBAvail(lpThis); i = i + 1) {
            if(egunSerial_ProcessingThread_HandleSerialData__RBPeek(lpThis, i) == 0x0A) {
                /* Got a full message - handle that message ... */
                egunSerial_ProcessingThread_HandleSerialData_MsgInRingbuffer(lpThis, i+1);
                break;
            }
            if(egunSerial_ProcessingThread_HandleSerialData__RBPeek(lpThis, i) == '$') {
                /* Skip everything up until here ... and restart process ... (resync) */
                egunSerial_ProcessingThread_HandleSerialData__Discard(lpThis, i);
                break;
            }
        }
        return; /* Did not fully receive a message */
    }
}

#if defined(__FreeBSD__)
    static void* egunSerial_ProcessingThread(void* lpArg) {
        struct egunSerial_Impl* lpThis;

        struct kevent kev;
        struct timespec ts;

        int r;

        lpThis = (struct egunSerial_Impl*)lpArg;

        /* Attach the serial port to kqueue */
        EV_SET(&kev, lpThis->hSerialPort, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, (void*)lpThis);
        if(kevent(lpThis->kq, &kev, 1, NULL, 0, NULL) < 0) {
            #ifdef DEBUG
                printf("%s:%u Failed to add serial port to kqueue ...\n", __FILE__, __LINE__);
            #endif
            return NULL; /* Terminate thread ... */
        }

        EV_SET(&kev, ELECTRONCTRL_SERIAL__PROCESSINGHTREAD_EVENT__TERMINATE, EVFILT_USER, EV_ADD|EV_ENABLE|EV_ONESHOT, 0, 0, NULL);
        if(kevent(lpThis->kq, &kev, 1, NULL, 0, NULL) < 0) {
            #ifdef DEBUG
                printf("%s:%u Failed to add termination event to our user event listening queue\n", __FILE__, __LINE__);
            #endif
            return NULL; /* Terminate thread ... */
        }

        for(;;) {
            /* Wait for events to process ... */
            ts.tv_sec = 10; ts.tv_nsec = 0;
            r = kevent(lpThis->kq, NULL, 0, &kev, 1, &ts);
            if(r < 0) {
                #ifdef DEBUG
                    printf("%s:%u kevent returned an error ...\n", __FILE__, __LINE__);
                #endif
                break;
            } else if(r == 0) {
                #if 0
                    printf("%s:%u idle wakeup ...\n", __FILE__, __LINE__);
                #endif
            } else {
                /* Received an event - most likely our EVFILT_READ */
                if((kev.ident == lpThis->hSerialPort) && (kev.filter == EVFILT_READ)) {
                    /* We are going to read serial data ... */
                    egunSerial_ProcessingThread_HandleSerialData(lpThis);
                } else if((kev.filter == EVFILT_USER) && (kev.ident == ELECTRONCTRL_SERIAL__PROCESSINGHTREAD_EVENT__TERMINATE)) {
                    break; /* Leave loop ... */
                }
            }
        }
        return NULL;
    }
#endif



enum egunError egunConnect_Serial(
    struct electronGun** lpOut,
    char* lpPort
) {
    struct egunSerial_Impl* lpNew;
    #if defined(__FreeBSD__) || defined(__linux__)
        enum egunError e;
        unsigned long int i;
    #endif

    if((lpNew = malloc(sizeof(struct egunSerial_Impl))) == NULL) {
        return egunE_OutOfMemory;
    }

    lpNew->objEgun.lpReserved = (void*)lpNew;
    lpNew->objEgun.vtbl = &egunSerial_VTBL;

    lpNew->ringbufferIn.dwHead = 0;
    lpNew->ringbufferIn.dwTail = 0;

    #if defined(__FreeBSD__) || defined(__linux__)
        lpNew->hSerialPort = -1;
        if(lpPort != NULL) {
            lpNew->hSerialPort = open(lpPort, O_RDWR | O_SYNC | O_NONBLOCK);
        } else {
            for(i = 0; i < egunSerial_DefaultDevices_LEN; i=i+1) {
                lpNew->hSerialPort = open(egunSerial_DefaultDevices[i], O_RDWR | O_SYNC | O_NONBLOCK);
                if(lpNew->hSerialPort != -1) {
                    break;
                }
            }
        }

        if(lpNew->hSerialPort == -1) {
            free(lpNew);
            return egunE_ConnectionError;
        }



        /* Setup serial port ... */
        e = setInterfaceAttributes(lpNew->hSerialPort, B19200, 0);
        if(e != egunE_Ok) {
            close(lpNew->hSerialPort);
            free(lpNew);
            return egunE_Failed;
        }

        e = setInterfaceBlocking(lpNew->hSerialPort, 0);
        if(e != egunE_Ok) {
            close(lpNew->hSerialPort);
            free(lpNew);
            return egunE_Failed;
        }

        #if defined(__FreeBSD__)
            lpNew->kq = kqueue();
            if(lpNew->kq == -1) {
                close(lpNew->hSerialPort);
                free(lpNew);
                return egunE_Failed;
            }
        #else
            #error ToDo
        #endif

        /* Launch message handling thread ... */
        if(pthread_create(&(lpNew->thrThread), NULL, &egunSerial_ProcessingThread, (void*)lpNew) != 0) {
            close(lpNew->kq);
            close(lpNew->hSerialPort);
            free(lpNew);
            return egunE_Failed;
        }
    #endif

    sleep(7); /* We have to introduce a delay for USB since the device is reset while opening the USB port ... */

    (*lpOut) = &(lpNew->objEgun);
    return egunE_Ok;
}


#ifdef __cplusplus
    } /* extern "C" { */
#endif
