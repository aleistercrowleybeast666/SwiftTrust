#ifndef __ST_TASK_H
#define __ST_TASK_H
#include "st_project_config.h"
#include <stdbool.h>
typedef enum
{
    ST_TASK_CAPTURE = 1,
    ST_TASK_COMBINED = 2,
    ST_TASK_OUTPUT = 3
} StTaskMode;
typedef enum
{
    ST_TASK_IDLE,
    ST_TASK_STARTING,
    ST_TASK_RUNNING,
    ST_TASK_STOPPING,
    ST_TASK_COMPLETED,
    ST_TASK_STOPPED,
    ST_TASK_FAILED,
    ST_TASK_UNKNOWN
} StTaskState;
typedef struct
{
    StTaskMode mode;
    StTaskState state;
    uint32_t start_id, stop_id;
} StTask;
typedef enum
{
    ST_TASK_OK,
    ST_TASK_BUSY,
    ST_TASK_INVALID
} StTaskResult;
bool Task_IsRecordingMode(StTaskMode);
bool Task_IsActive(const StTask *);
StTaskResult Task_Begin(StTask *, StTaskMode, uint32_t transaction);
void Task_CompleteStart(StTask *, uint32_t transaction, bool succeeded);
void Task_RequestStop(StTask *, uint32_t transaction);
void Task_CompleteStop(StTask *, uint32_t transaction, bool succeeded);
void Task_CompleteOutput(StTask *, bool succeeded);
#endif
