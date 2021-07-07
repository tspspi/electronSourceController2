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

#if defined(__FreeBSD__) || defined(__linux__)
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
};


static enum egunError egunSerial__Release(
    struct electronGun* lpSelf
) {
    return egunE_Failed;
}


static char egunSerial__RequestID__Message[] = "$$$id\r\n";
static enum egunError egunSerial__RequestID(
    struct electronGun* lpSelf
) {
    struct egunSerial_Impl* lpThis;
    int r;

    if(lpSelf == NULL) { return egunE_InvalidParam; }
    lpThis = (struct egunSerial_Impl*)(lpSelf->lpReserved);

    r = write(lpThis->hSerialPort, egunSerial__RequestID__Message, sizeof(egunSerial__RequestID__Message));

    if(r != sizeof(egunSerial__RequestID__Message)) {
        return egunE_Failed;
    } else {
        return egunE_Ok;
    }
}






struct electronGun_VTBL egunSerial_VTBL = {
    &egunSerial__Release,
    &egunSerial__RequestID
};


#if defined(__FreeBSD__) || defined(__linux__)
    static char* egunSerial_DefaultDevices[] = {
        "/dev/ttyU0"
    };
    #define egunSerial_DefaultDevices_LEN (sizeof(egunSerial_DefaultDevices)/sizeof(char*))
#endif

static void egunSerial_ProcessingThread_HandleSerialData(
    struct egunSerial_Impl* lpThis
) {
    char bData;
    int r;

    r = read(lpThis->hSerialPort, &bData, 1);
    if(r > 0) {
        printf("%c", bData);
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
                #ifdef DEBUG
                    printf("%s:%u idle wakeup ...\n", __FILE__, __LINE__);
                #endif
            } else {
                /* Received an event - most likely our EVFILT_READ */
                if((kev.ident == lpThis->hSerialPort) && (kev.filter == EVFILT_READ)) {
                    /* We are going to read serial data ... */
                    egunSerial_ProcessingThread_HandleSerialData(lpThis);
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
        e = setInterfaceAttributes(lpNew->hSerialPort, B115200, 0);
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

    sleep(3); /* We have to introduce a delay for USB since the device is reset while opening the USB port ... */

    (*lpOut) = &(lpNew->objEgun);
    return egunE_Ok;
}


#ifdef __cplusplus
    } /* extern "C" { */
#endif
