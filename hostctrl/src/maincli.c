#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include "./electronctrl.h"


static void printUsage(int argc, char* argv[]) {
    /* ToDo */
}

enum egunCliCommand {
    egunCliCommand_ID               = 0,
};

int main(int argc, char* argv[]) {
    struct electronGun* lpEgun;
    enum egunError e;
    enum egunCliCommand cmd = egunCliCommand_ID;

    char* lpPort = NULL;

    if(argc < 1) {
        printUsage(argc, argv);
        return 1;
    }

    /* "Parse" CLI arguments ... */
    {
        unsigned long int i;
        for(i = 1; i < argc; i = i + 1) {
            if(strcmp(argv[i], "-port") == 0) {
                if(i+1 >= argc) {
                    printf("Missing port argument\n");
                    printUsage(argc, argv); return 1;
                }
                lpPort = argv[i+1];
                i = i + 1;
                continue;
            } else {
                printf("Unknown command %s\n\n", argv[i]);
                printUsage(argc, argv);
                return 1;
            }
        }
    }


    /* Connect to the device */
    #ifdef DEBUG
        printf("Command summary:\n\tPort:\t%s\n", lpPort);
    #endif

    e = egunConnect_Serial(&lpEgun, lpPort);
    if(e != egunE_Ok) {
        printf("%s:%u Failed to connect (%u)\n", __FILE__, __LINE__, e);
        return 1;
    }


    if(cmd == egunCliCommand_ID) {
        e = lpEgun->vtbl->requestId(lpEgun);
        if(e != egunE_Ok) {
            printf("%s:%u Failed to send ID request (%u)\n", __FILE__, __LINE__, e);
        }
    }

sleep(5);
e = lpEgun->vtbl->requestId(lpEgun);
if(e != egunE_Ok) { printf("%s:%u Failed to send command (%u)\n", __FILE__, __LINE__, e); }
sleep(5);
e = lpEgun->vtbl->getCurrentVoltage(lpEgun, 1);
if(e != egunE_Ok) { printf("%s:%u Failed to send command (%u)\n", __FILE__, __LINE__, e); }
e = lpEgun->vtbl->getCurrentVoltage(lpEgun, 2);
if(e != egunE_Ok) { printf("%s:%u Failed to send command (%u)\n", __FILE__, __LINE__, e); }
e = lpEgun->vtbl->getCurrentVoltage(lpEgun, 3);
if(e != egunE_Ok) { printf("%s:%u Failed to send command (%u)\n", __FILE__, __LINE__, e); }
e = lpEgun->vtbl->getCurrentVoltage(lpEgun, 4);
if(e != egunE_Ok) { printf("%s:%u Failed to send command (%u)\n", __FILE__, __LINE__, e); }
e = lpEgun->vtbl->getCurrentCurrent(lpEgun, 1);
if(e != egunE_Ok) { printf("%s:%u Failed to send command (%u)\n", __FILE__, __LINE__, e); }
e = lpEgun->vtbl->getCurrentCurrent(lpEgun, 2);
if(e != egunE_Ok) { printf("%s:%u Failed to send command (%u)\n", __FILE__, __LINE__, e); }
e = lpEgun->vtbl->getCurrentCurrent(lpEgun, 3);
if(e != egunE_Ok) { printf("%s:%u Failed to send command (%u)\n", __FILE__, __LINE__, e); }
e = lpEgun->vtbl->getCurrentCurrent(lpEgun, 4);
if(e != egunE_Ok) { printf("%s:%u Failed to send command (%u)\n", __FILE__, __LINE__, e); }
sleep(5);
e = lpEgun->vtbl->requestId(lpEgun);
if(e != egunE_Ok) { printf("%s:%u Failed to send command (%u)\n", __FILE__, __LINE__, e); }
sleep(10);

    e = lpEgun->vtbl->release(lpEgun);
    #ifdef DEBUG
        printf("%s:%u Release returned %u (%s)\n", __FILE__, __LINE__, e, (e == egunE_Ok) ? "ok" : "failed");
    #endif
    return 0;
}
