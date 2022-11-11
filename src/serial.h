#ifndef __is_included__fd8eb7f9_df0f_11eb_ba7e_b499badf00a1
#define __is_included__fd8eb7f9_df0f_11eb_ba7e_b499badf00a1 1

#ifndef __cplusplus
    #ifndef true
        #define true 1
        #define false 0
        typedef unsigned char bool;
    #endif
#endif

#ifndef SERIAL_RINGBUFFER_SIZE
    #define SERIAL_RINGBUFFER_SIZE 64
#endif

struct ringBuffer {
    volatile unsigned long int dwHead;
    volatile unsigned long int dwTail;

    volatile unsigned char buffer[SERIAL_RINGBUFFER_SIZE];
};

void serialInit0();
void serialInit1();
void serialInit2();

void handleSerial0Messages();
void handleSerial1Messages();
void handleSerial2Messages();

void rampMessage_ReportVoltages();
void rampMessage_ReportFilaCurrents();

void rampMessage_InsulationTestSuccess();
void rampMessage_InsulationTestFailure();
void rampMessage_BeamOnSuccess();

void statusMessageOff();

void filamentCurrent_Enable(bool bEnabled);
void filamentCurrent_GetId();
void filamentCurrent_GetVersion();
void filamentCurrent_SetCurrent(unsigned long int newCurrent);
void filamentCurrent_GetSetCurrent();
void filamentCurrent_GetCurrent();
void filamentCurrent_GetRawADC();
void filamentCurrent_CalLow();
void filamentCurrent_CalHigh(unsigned long int measuredCurrent);
void filamentCurrent_CalStore();
void filamentCurrent_EnableProtection(bool bEnabled);
unsigned long int filamentCurrent_GetCachedCurrent();


#endif /* __is_included__fd8eb7f9_df0f_11eb_ba7e_b499badf00a1 */
