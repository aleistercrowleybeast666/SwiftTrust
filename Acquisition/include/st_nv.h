#ifndef ST_NV_H
#define ST_NV_H
#include "st_link2.h"
enum
{
    ST_NV_BYTES = 96,
    ST_NV_CONTROLLER = 1,
    ST_NV_ACQUISITION = 2
};
typedef struct
{
    void *ctx;
    bool (*read)(void *, unsigned slot, uint8_t out[ST_NV_BYTES]);
    /* Replace/erase only this slot, never both. True means durable write complete.
     * Readback is additionally checked here. Storage addresses/Flash geometry and
     * a permitted write window must be provided by the board-specific backend. */
    bool (*write)(void *, unsigned slot, const uint8_t in[ST_NV_BYTES]);
    bool verified;
} st_nv_storage;
typedef struct
{
    uint8_t own_uid[12], peer_uid[12], role;
    uint32_t epoch_first, epoch_last; /* Acquisition's centrally assigned disjoint range. */
} st_nv_identity;
typedef struct
{
    uint64_t generation, counter, peer_counter;
    uint32_t epoch;
} st_nv_values;
typedef enum
{
    ST_NV_UNBOUND,
    ST_NV_EMPTY,
    ST_NV_VALID,
    ST_NV_DAMAGED,
    ST_NV_FOREIGN,
    ST_NV_IO_ERROR,
    ST_NV_EXHAUSTED
} st_nv_status;
typedef struct
{
    st_nv_storage storage;
    st_nv_identity identity;
    st_nv_values values;
    st_nv_status status;
    unsigned active;
} st_nv_store;
void st_nv_bind(st_nv_store *, st_nv_storage, st_nv_identity);
st_nv_status st_nv_open(st_nv_store *);
/* Explicit provisioning only when BOTH slots are all 0xFF; never auto-format
 * corrupt/missing history. This is not a calibration or general settings API. */
bool st_nv_provision(st_nv_store *);
st2_nv_port st_nv_link_port(st_nv_store *);
bool st_nv_server_init(st2_server *, st_nv_store *, st_logger *, st2_timing);
bool st_nv_client_init(st2_client *, st_nv_store *, st2_timing);
#endif
