#include <avr/io.h>
#include <avr/interrupt.h>
#include <math.h>
#include <util/twi.h>
#include <stdint.h>
#include <limits.h>

#include "./serial.h"
#include "./controller.h"
#include "./sysclock.h"
#include "./adc.h"
#include "./psu.h"
#include "./pwmout.h"

/*
    Ramp controller
*/

int protectionEnabled;
struct rampMode rampMode;

/*@
    assigns rampMode.clkLastTick;
    assigns rampMode.mode;
    assigns rampMode.vTargets[0];
    assigns rampMode.vTargets[1];
    assigns rampMode.vTargets[2];
    assigns rampMode.vTargets[3];
    assigns rampMode.aTargetFilament;
    assigns rampMode.vCurrent[0];
    assigns rampMode.vCurrent[1];
    assigns rampMode.vCurrent[2];
    assigns rampMode.vCurrent[3];
    assigns rampMode.filamentCurrent;

    ensures rampMode.mode == controllerRampMode__InsulationTest;
    ensures rampMode.vTargets[0] == CONTROLLER_RAMP_TARGETV__K;
    ensures rampMode.vTargets[1] == CONTROLLER_RAMP_TARGETV__W;
    ensures rampMode.vTargets[2] == CONTROLLER_RAMP_TARGETV__FOC;
    ensures rampMode.vTargets[3] == 0;
    ensures rampMode.aTargetFilament == 0;
    ensures (rampMode.vCurrent[0] == 0) && (rampMode.vCurrent[1] == 0) && (rampMode.vCurrent[2] == 0);
    ensures rampMode.filamentCurrent == 0;
*/
void rampStart_InsulationTest() {
    unsigned long int i;
    /*@
        loop invariant 1 <= i <= 5;
        loop assigns psuStates[i-1].bOutputEnable;
        loop variant 5-i;
    */
    for(i = 1; i < 5; i=i+1) {
        psuStates[i-1].bOutputEnable = false;
        setPSUVolts(0, i);
        setPSUMicroamps(CONTROLLER_RAMP_VOLTAGE_CURRENTLIMIT, i);
    }
    filamentCurrent_Enable(false);
    filamentCurrent_SetCurrent(0);

    rampMode.mode = controllerRampMode__InsulationTest;
    rampMode.vTargets[0] = CONTROLLER_RAMP_TARGETV__K;
    rampMode.vTargets[1] = CONTROLLER_RAMP_TARGETV__W;
    rampMode.vTargets[2] = CONTROLLER_RAMP_TARGETV__FOC;
    rampMode.vTargets[3] = 0;
    rampMode.aTargetFilament = 0;

    rampMode.vCurrent[0] = 0;
    rampMode.vCurrent[1] = 0;
    rampMode.vCurrent[2] = 0;
    rampMode.vCurrent[3] = 0;
    rampMode.filamentCurrent = 0;
    rampMode.clkLastTick = micros();
}

void rampStart_BeamOn() {
    unsigned long int i;
    /*@
        loop invariant 1 <= i <= 5;
        loop assigns psuStates[i-1].bOutputEnable;
        loop variant 5-i;
    */
    for(i = 1; i < 5; i=i+1) {
        psuStates[i-1].bOutputEnable = false;
        setPSUVolts(0, i);
        setPSUMicroamps(CONTROLLER_RAMP_VOLTAGE_CURRENTLIMIT, i);
    }
    filamentCurrent_Enable(false);

    rampMode.mode = controllerRampMode__BeamOn;
    rampMode.vTargets[0] = CONTROLLER_RAMP_TARGETV__K;
    rampMode.vTargets[1] = CONTROLLER_RAMP_TARGETV__W;
    rampMode.vTargets[2] = CONTROLLER_RAMP_TARGETV__FOC;
    rampMode.vTargets[3] = 0;
    rampMode.aTargetFilament = filamentCurrent_GetCachedCurrent(); /* We use the currently selected filament current as target */

    rampMode.vCurrent[0] = 0;
    rampMode.vCurrent[1] = 0;
    rampMode.vCurrent[2] = 0;
    rampMode.vCurrent[3] = 0;
    rampMode.filamentCurrent = 0;
    rampMode.clkLastTick = micros();

    filamentCurrent_SetCurrent(0);
}

/*@
    assigns psuStates[0 .. 3].bOutputEnable;
    assigns rampMode.mode;

    ensures \forall int i; 0 < i < 4 ==>
        psuStates[i].bOutputEnable == false;
    ensures rampMode.mode == controllerRampMode__None;
*/
static void rampInsulationError() {
    /*
        Write message
    */
    rampMessage_InsulationTestFailure();

    /*
        Disable everything
    */
    psuStates[0].bOutputEnable = false;
    psuStates[1].bOutputEnable = false;
    psuStates[2].bOutputEnable = false;
    psuStates[3].bOutputEnable = false;
    filamentCurrent_Enable(false);

    /*
        Stop ramp
    */
    rampMode.mode = controllerRampMode__None;
}

static void handleRamp() {
    unsigned long int curTime = micros();
    unsigned long int i;
    unsigned long int timeElapsed;

    if(curTime > rampMode.clkLastTick) {
        timeElapsed = curTime - rampMode.clkLastTick;
    } else {
        timeElapsed = (ULONG_MAX - rampMode.clkLastTick) + curTime;
    }

    if(rampMode.mode == controllerRampMode__None) { return; }

    /*
        Check if we should do anything with voltages on next tick ...
    */
    if((rampMode.mode == controllerRampMode__BeamOn) || (rampMode.mode == controllerRampMode__InsulationTest)) {
        if((rampMode.aTargetFilament != rampMode.filamentCurrent) && (rampMode.mode == controllerRampMode__BeamOn)) {
            if(timeElapsed < CONTROLLER_RAMP_FILCURRENT_STEPDURATIONMILLIS) { return; }

            if(rampMode.filamentCurrent == 0) {
                filamentCurrent_Enable(true);
                for(i = 0; i < 4; i=i+1) {
                    setPSUMicroamps(CONTROLLER_RAMP_VOLTAGE_CURRENTLIMIT_BEAM, i+1);
                }
            }

            rampMode.filamentCurrent = ((rampMode.filamentCurrent + CONTROLLER_RAMP_FILCURRENT_STEPSIZE) > rampMode.aTargetFilament) ? rampMode.aTargetFilament : (rampMode.filamentCurrent + CONTROLLER_RAMP_FILCURRENT_STEPSIZE);
            if(rampMode.filamentCurrent > rampMode.aTargetFilament) {
                rampMode.filamentCurrent = rampMode.aTargetFilament;
            }
            filamentCurrent_SetCurrent(rampMode.filamentCurrent);
            rampMode.clkLastTick = curTime;
            return;
        }

        /*
            In case of beam on mode the next step is to increase filament current
            SLOWLY
        */
        if((rampMode.vCurrent[0] == 0) && (rampMode.vCurrent[1] == 0) && (rampMode.vCurrent[2] == 0) && (rampMode.vCurrent[3] == 0)) {
            /* This is the initial delay ... */
            if(timeElapsed < CONTROLLER_RAMP_VOLTAGE_INITDURATION) { return; }

            /* We start the sequence by setting voltage and PSU enable ... */
            /*@
                loop invariant 0 <= i <= 4;
                loop assigns rampMode.vCurrent[0 .. 3];
                loop assigns psuStates[0 .. 3].bOutputEnable;
                loop variant 4-i;
            */
            for(i = 0; i < 4; i=i+1) {
                rampMode.vCurrent[i] = ((rampMode.vCurrent[i] + CONTROLLER_RAMP_VOLTAGE_STEPSIZE) > rampMode.vTargets[i]) ? rampMode.vTargets[i] : (rampMode.vCurrent[i] + CONTROLLER_RAMP_VOLTAGE_STEPSIZE);
                setPSUVolts(rampMode.vCurrent[i], i+1);
                psuStates[i].bOutputEnable = (rampMode.vTargets[i] != 0) ? true : false;
                rampMessage_ReportVoltages();
            }
            rampMode.clkLastTick = curTime;
            return;
        }
        if((rampMode.vCurrent[0] != rampMode.vTargets[0]) || (rampMode.vCurrent[1] != rampMode.vTargets[1]) || (rampMode.vCurrent[2] != rampMode.vTargets[2]) || (rampMode.vCurrent[3] != rampMode.vTargets[3])) {
            if(timeElapsed < CONTROLLER_RAMP_VOLTAGE_STEPDURATIONMILLIS) { return; }

            /*@
                loop invariant 0 <= i <= 4;
                loop assigns rampMode.vCurrent[0 .. 3];
                loop variant 4-i;
            */
            for(i = 0; i < 4; i=i+1) {
                rampMode.vCurrent[i] = ((rampMode.vCurrent[i] + CONTROLLER_RAMP_VOLTAGE_STEPSIZE) > rampMode.vTargets[i]) ? rampMode.vTargets[i] : (rampMode.vCurrent[i] + CONTROLLER_RAMP_VOLTAGE_STEPSIZE);
                setPSUVolts(rampMode.vCurrent[i], i+1);
                rampMessage_ReportVoltages();
            }
            rampMode.clkLastTick = curTime;
            return;
        }

        /*
            In case voltages have reached the target and finished the insulation test ...
        */
        if(rampMode.mode == controllerRampMode__InsulationTest) {
            rampMode.mode = controllerRampMode__None;
            for(i = 0; i < 4; i=i+1) {
                setPSUVolts(0, i+1);
                psuStates[i].bOutputEnable = false;
            }
            rampMessage_InsulationTestSuccess();
            return;
        } else {
            /*
                Rampd one for beam on
            */
            rampMode.mode = controllerRampMode__None;
            rampMessage_BeamOnSuccess();
            return;
        }
    }
}

static void handleOvercurrentDetection() {
    unsigned long int i;

    /*
        Periodically check if any of the PSUs went into current limiting
        mode which is usually an indication for an insulation fault - in this
        case disable everything and signal fault ...
    */

    if(protectionEnabled != 0) {
        for(i = 0; i < 4; i=i+1) {
            if((rampMode.vTargets[i] > 0) && (rampMode.vCurrent[i] > 0) && (psuStates[i].limitMode == psuLimit_Current)) {
                rampInsulationError();
                return;
            }
        }
    }
}


int main() {
    #ifndef FRAMAC_SKIP
		cli();
	#endif

    protectionEnabled = 1;

    /*
        Initialize state (power on reset)
    */

    /*
        PORTA & PORTC:
            PSU digital in and out
        PORTL:
            Analog out for PSUs
        PORTD:
            PD7: Filament power on/off
        PORTB:
            PB7: Onboard LED (OC0A)
    */
    DDRA = 0x33;    PORTA = 0x22;
    DDRC = 0xCC;    PORTC = 0x40;
    DDRL = 0xFF;    PORTL = 0xFF;
    DDRD = 0x80;    PORTD = 0x00;

    DDRB = DDRB | 0x80;

    /*
        Load voltage values from EEPROM
    */

    /* Clear debug LED */
    PORTB = PORTB & 0x7F;

    /* Setup system clock */
    sysclockInit();

    /*
        Setup serial
            USART0 is used to communicate via serial or USB interface
    */
    serialInit0();
    #ifdef SERIAL_UART1_ENABLE
        serialInit1();
    #endif
    #ifdef SERIAL_UART2_ENABLE
        serialInit2();
    #endif

    #ifndef FRAMAC_SKIP
		sei();
	#endif

    /*
        INitialize ADCs:
            * Free running mode
            * MUX set to ADC0 for first conversion
            * prescaler 128 (125 kHz ADC frequency -> 10 kHz sampling frqeuency
                with 8 channels for 4 PSUs -> little bit more than 1 kHz sampling
                of all voltages and currents - more than sufficient)
    */
    adcInit();

    /*
        Initialize power supply interface
    */
    psuInit();
    pwmoutInit();

    /*
        Disable ramping on power on
    */
    rampMode.mode = controllerRampMode__None;

    for(;;) {
        /*
            This is the main application loop. It works in a hybrid synchronous
            and asynchronous way. ISRs are usually only used as a data pump
            into queues, any actions are taken by this main loop - of course
            this won't satisfy any timing constraints but there is no need
            for such constraints during slow control of the experiment
        */
        handleSerial0Messages(); /* main external serial interface */
        #ifdef SERIAL_UART1_ENABLE
            handleSerial1Messages(); /* Serial port for status reports */
        #endif
        #ifdef SERIAL_UART2_ENABLE
            handleSerial2Messages(); /* Serial port for status reports */
        #endif

        psuUpdateMeasuredState();
        psuSetOutputs();

        handleRamp();
        handleOvercurrentDetection();
    }
}
