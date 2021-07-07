#include <stdlib.h>
#include "./electronctrl.h"

#ifdef __cplusplus
    extern "C" {
#endif

struct egunSerial_Impl {
    struct electronGun          objEgun;
};

struct electronGun_VTBL egunSerial_VTBL = {
    NULL
};

enum egunError egunConnect_Serial(
    struct electronGun** lpOut,
    char* lpPort
) {
    struct egunSerial_Impl* lpNew;

    if((lpNew = malloc(sizeof(struct egunSerial_Impl))) == NULL) {
        return egunE_OutOfMemory;
    }

    lpNew->objEgun.lpReserved = (void*)lpNew;
    lpNew->objEgun.vtbl = &egunSerial_VTBL;

    (*lpOut) = &(lpNew->objEgun);
    return egunE_Ok;
}


#ifdef __cplusplus
    } /* extern "C" { */
#endif
