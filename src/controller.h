#ifndef __is_included__d81475f8_df0f_11eb_ba7e_b499badf00a1
#define __is_included__d81475f8_df0f_11eb_ba7e_b499badf00a1 1

#define SERIAL_UART1_ENABLE 1
#define SERIAL_UART2_ENABLE 0

#ifndef CONTROLLER_RAMP_TARGETV__K
    #define CONTROLLER_RAMP_TARGETV__K 2018
#endif
#ifndef CONTROLLER_RAMP_TARGETV__W
    #define CONTROLLER_RAMP_TARGETV__W 2020
#endif
#ifndef CONTROLLER_RAMP_TARGETV__FOC
    #define CONTROLLER_RAMP_TARGETV__FOC 2060
#endif

#ifndef CONTROLLER_RAMP_VOLTAGE_STEPSIZE
    #define CONTROLLER_RAMP_VOLTAGE_STEPSIZE 5
#endif

#ifndef CONTROLLER_RAMP_VOLTAGE_CURRENTLIMIT
    #define CONTROLLER_RAMP_VOLTAGE_CURRENTLIMIT 10
#endif

#ifndef CONTROLLER_RAMP_VOLTAGE_STEPDURATIONMILLIS
    #define CONTROLLER_RAMP_VOLTAGE_STEPDURATIONMILLIS 90000
#endif
#ifndef  CONTROLLER_RAMP_VOLTAGE_INITDURATION
    #define CONTROLLER_RAMP_VOLTAGE_INITDURATION 10000000
#endif

#ifndef CONTROLLER_RAMP_VOLTAGE_CURRENTLIMIT_BEAM
    #define CONTROLLER_RAMP_VOLTAGE_CURRENTLIMIT_BEAM 900
#endif

#ifndef CONTROLLER_RAMP_FILCURRENT_STEPDURATIONMILLIS
    #define CONTROLLER_RAMP_FILCURRENT_STEPDURATIONMILLIS 250000
#endif
#ifndef CONTROLLER_RAMP_FILCURRENT_STEPSIZE
    #define CONTROLLER_RAMP_FILCURRENT_STEPSIZE 10
#endif

enum controllerRampMode {
    controllerRampMode__None,
    controllerRampMode__BeamOn,
    controllerRampMode__FilamentCondition,
    controllerRampMode__InsulationTest
};

extern struct rampMode rampMode;
extern int protectionEnabled;

struct rampMode {
    /*
        Modes:
            None                    Ramp control disabled
            BeamOn                  Increase HV checking for insulation faults
                                    Increase filament current
            Filament condition      Run filament conditioning
            Inulation test          Increase HV checking for insulation fault
    */
    enum controllerRampMode         mode;
    uint16_t                        vTargets[4];
    uint16_t                        aTargetFilament;

    /*
        State
            vCurrent            The currently set voltage target
            filamentCurrent     The currently set filament current
            clkStarted          millis() when the process started (note wrap around when calculating)
    */
    uint16_t                        vCurrent[4];
    uint16_t                        filamentCurrent;
    unsigned long int               clkLastTick;
};

void rampStart_InsulationTest();
void rampStart_BeamOn();

#endif /* #ifndef __is_included__d81475f8_df0f_11eb_ba7e_b499badf00a1 */
