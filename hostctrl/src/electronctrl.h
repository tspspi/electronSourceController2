#ifndef __is_included__d3cf826b_df20_11eb_ba7e_b499badf00a1
#define __is_included__d3cf826b_df20_11eb_ba7e_b499badf00a1 1

#ifdef __cplusplus
    extern "C" {
#endif

enum egunError {
    egunE_Ok                        = 0,

    egunE_Failed,
    egunE_OutOfMemory,
    egunE_ConnectionError,
    egunE_InvalidParam,
};

struct electronGun;
struct electronGun_VTBL;

typedef enum egunError (*electronGun_Release)(
    struct electronGun* lpSelf
);
typedef enum egunError (*electronGun_RequestID)(
    struct electronGun* lpSelf
);


struct electronGun_VTBL {
    electronGun_Release             release;
    electronGun_RequestID           requestId;
};

struct electronGun {
    struct electronGun_VTBL*        vtbl;
    void*                           lpReserved;
};

enum egunError egunConnect_Serial(
    struct electronGun** lpOut,
    char* lpPort
);

#ifdef __cplusplus
    } /* extern "C" { */
#endif

#endif /* #ifndef __is_included__d3cf826b_df20_11eb_ba7e_b499badf00a1 */
