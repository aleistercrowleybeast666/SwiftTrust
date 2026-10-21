#include "../Inc/st_task.h"
bool Task_IsRecordingMode(StTaskMode mode)
{
    return mode == ST_TASK_CAPTURE || mode == ST_TASK_COMBINED;
}
bool Task_IsActive(const StTask *task)
{
    return task && (task->state == ST_TASK_STARTING || task->state == ST_TASK_RUNNING ||
                    task->state == ST_TASK_STOPPING);
}
StTaskResult Task_Begin(StTask *task, StTaskMode mode, uint32_t id)
{
    if (!task || !id || mode < ST_TASK_CAPTURE || mode > ST_TASK_OUTPUT)
        return ST_TASK_INVALID;
    if (Task_IsActive(task) || task->state == ST_TASK_UNKNOWN)
        return ST_TASK_BUSY;
    *task = (StTask){mode, ST_TASK_STARTING, id, 0};
    return ST_TASK_OK;
}
void Task_CompleteStart(StTask *task, uint32_t id, bool succeeded)
{
    if (task && task->start_id == id && task->state == ST_TASK_STARTING)
        task->state = succeeded ? ST_TASK_RUNNING : ST_TASK_FAILED;
}
void Task_RequestStop(StTask *task, uint32_t id)
{
    if (task && id)
    {
        task->stop_id = id;
        task->state = ST_TASK_STOPPING;
    }
}
void Task_CompleteStop(StTask *task, uint32_t id, bool succeeded)
{
    if (task && task->stop_id == id)
        task->state = succeeded ? ST_TASK_STOPPED : ST_TASK_FAILED;
}
void Task_CompleteOutput(StTask *task, bool succeeded)
{
    if (task && task->mode == ST_TASK_OUTPUT)
        task->state = succeeded ? ST_TASK_COMPLETED : ST_TASK_FAILED;
}
