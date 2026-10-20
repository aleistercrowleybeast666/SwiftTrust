#ifndef ST_PAIRING_H
#define ST_PAIRING_H
#include "st_nv.h"
typedef struct
{
    st_nv_store store;
    st_nv_storage storage;
    uint8_t own_uid[12], role;
    bool have_history;
} st_pairing;
/* Boot discovers identity only from CRC-valid records matching real MCU UID/role.
 * It never writes/erases/provisions implicitly. Unknown factory FRAM bytes are
 * not assumed erased. Explicit NVINIT is required before PAIR on new media. */
st_nv_status st_pairing_open(st_pairing *, st_nv_storage, const uint8_t uid[12], uint8_t role);
/* Explicit local-console action, only IDLE, and only when no valid history exists.
 * A service recovery after lost/corrupt history must assign a NEW network range. */
bool st_pairing_initialize_new(st_pairing *, bool idle, uint32_t explicit_confirmation);
bool st_pairing_provision(st_pairing *, const uint8_t peer[12], unsigned network, bool idle);
enum
{
    ST_PAIR_NEW_MEDIA_CONFIRM = 0x4E56494E
};
#endif
