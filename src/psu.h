#ifndef __is_included__a2cd9f9e_e0c1_11eb_a444_b499badf00a1
#define __is_included__a2cd9f9e_e0c1_11eb_a444_b499badf00a1 1

#ifndef __cplusplus
    typedef unsigned char bool;
    #define true 1
    #define false 0
#endif

enum psuPolarity {
    psuPolarity_Positive,
    psuPolarity_Negative
};

enum limitingMode {
    psuLimit_Current,
    psuLimit_Voltage
};

struct psuState {
    bool                    bOutputEnable;
    enum psuPolarity        polPolarity;
    uint16_t                setVTarget;
    uint16_t                setILimit;

    /* Sensing */
    enum limitingMode       limitMode;
    uint16_t                realV;
    uint16_t                realI;
    #if 0
        enum psuPolarity        realPolarity;
    #endif
};

#ifdef __cplusplus
    extern "C" {
#endif

void psuInit();
void psuSetOutput(int psuIndex);
void psuSetOutputs();
void psuUpdateMeasuredState();

#ifdef __cplusplus
    } /* extern "C" { */
#endif

#endif /* #ifndef __is_included__a2cd9f9e_e0c1_11eb_a444_b499badf00a1 */
