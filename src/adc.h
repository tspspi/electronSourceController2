#ifndef __is_included__4b28e79c_dfeb_11eb_b607_b499badf00a1
#define __is_included__4b28e79c_dfeb_11eb_b607_b499badf00a1 1

#ifdef __cplusplus
    extern "C" {
#endif

#ifndef ADC_CHANNELS16
    extern uint16_t currentADC[8];
#else
    extern uint16_t currentADC[16];
#endif


void adcInit();

#ifdef __cplusplus
    } /* extern "C" { */
#endif

#endif /* #ifndef __is_included__4b28e79c_dfeb_11eb_b607_b499badf00a1 */
