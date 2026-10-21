#ifndef ST_CAL_INPUT_H
#define ST_CAL_INPUT_H
#include "st_cal_service.h"
#define ST_CAL_INPUT_CAPACITY 64u
typedef struct
{
    st_capture entries[ST_CAL_INPUT_CAPACITY];
    _Atomic(uint32_t) head, tail, errors;
    _Atomic(bool) enabled;
    uint32_t seen_errors;
    void *observer_ctx;
    bool (*observer_active)(void *);
    void (*observer_feed)(void *, const st_capture *);
    void (*observer_error)(void *);
} st_cal_input;
/* One DMA-completion producer, one serialized foreground consumer. Never reset
 * a live queue. Arm only for calibration; logging keeps its independent queue. */
void st_cal_input_init(st_cal_input *);
bool st_cal_input_offer_isr(st_cal_input *, const st_capture *);
void st_cal_input_error_isr(st_cal_input *);
void st_cal_input_step(st_cal_input *, st_cal_service *);
#endif
