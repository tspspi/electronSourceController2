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
    #define SERIAL_RINGBUFFER_SIZE 1024
#endif

struct ringBuffer {
    volatile unsigned long int dwHead;
    volatile unsigned long int dwTail;

    volatile unsigned char buffer[SERIAL_RINGBUFFER_SIZE];
};

void serialInit0();

void handleSerial0Messages();

void rampMessage_ReportVoltages();
void rampMessage_ReportFilaCurrents();

void rampMessage_InsulationTestSuccess();
void rampMessage_InsulationTestFailure();
void rampMessage_BeamOnSuccess();

void statusMessageOff();


#endif /* __is_included__fd8eb7f9_df0f_11eb_ba7e_b499badf00a1 */
