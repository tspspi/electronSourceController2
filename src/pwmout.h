#ifndef __is_included__caec5c77_e2e8_11eb_a58f_b499badf00a1
#define __is_included__caec5c77_e2e8_11eb_a58f_b499badf00a1 1

#ifndef __cplusplus
    #ifndef true
        #define true 1
        #define false 0
        typedef unsigned char bool;
    #endif
#endif

#ifdef __cplusplus
    extern "C" {
#endif

extern uint16_t pwmoutOnCycles[9];

void pwmoutInit();

void setPSUVolts(
    uint16_t v,
    uint8_t psu
);
void setPSUMicroamps(
    uint16_t ua,
    uint8_t psu
);
void setFilamentPWM(
    uint16_t pwmCycles
);
uint16_t getFilamentPWM();
void setFilamentOn(
    bool bOn
);
#ifdef __cplusplus
    } /* extern "C" { */
#endif

#endif /* #ifndef __is_included__caec5c77_e2e8_11eb_a58f_b499badf00a1 */
