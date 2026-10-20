#ifndef ST_CAL_ENDPOINT_H
#define ST_CAL_ENDPOINT_H
#include "st_cal_input.h"
#include "st_link2.h"
typedef struct
{
    st_cal_service *service;
    st_logger *logger;
    st_cal_input input;
    bool last_ok;
} st_cal_endpoint;
void st_cal_endpoint_init(st_cal_endpoint *, st_cal_service *, st_logger *);
void st_cal_endpoint_step(st_cal_endpoint *);
st2_cal_port st_cal_endpoint_port(st_cal_endpoint *);
void st_cal_view_encode(const st_cal_view *, uint8_t out[64]);
bool st_cal_view_decode(st_cal_view *, const uint8_t in[64]);
#endif
