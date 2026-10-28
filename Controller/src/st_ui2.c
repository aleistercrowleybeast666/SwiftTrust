#include "st_ui.h"
#include <stdio.h>
#include <string.h>
enum
{
    UI_START_CAPTURE = 1,
    UI_ARM_OUTPUT,
    UI_FIRE_OUTPUT,
    UI_ZERO_SENSOR
};
static bool Ui_IsMode(st_ui_page page)
{
    return page == ST_UI_CAPTURE || page == ST_UI_COMBINED || page == ST_UI_IGNITION_ONLY;
}
static st_ui_page Ui_ModePage(StTaskMode mode)
{
    return mode == ST_TASK_COMBINED ? ST_UI_COMBINED
           : mode == ST_TASK_OUTPUT ? ST_UI_IGNITION_ONLY
                                    : ST_UI_CAPTURE;
}
unsigned Ui_GetItemCount(const st_ui *ui)
{
    if (ui->page == ST_UI_HOME || ui->page == ST_UI_CAPTURE || ui->page == ST_UI_COMBINED ||
        ui->page == ST_UI_SETTINGS)
        return 4;
    if (ui->page == ST_UI_IGNITION_ONLY || ui->page == ST_UI_CONFIRM)
        return 2;
    return ui->page == ST_UI_STATUS ? 9 : 1;
}
static void Ui_Open(st_ui *ui, st_ui_page page)
{
    ui->page = page;
    ui->selection = 0;
    ui->top = 0;
    ui->notice = ST_UI_NOTICE_NONE;
}
static void Ui_Confirm(st_ui *ui, uint8_t action)
{
    ui->parent = ui->page;
    Ui_Open(ui, ST_UI_CONFIRM);
    ui->confirm_action = action;
}
static bool Ui_Cancel(st_ui *ui, st2_client *client, uint32_t now)
{
    if (ui->page == ST_UI_CONFIRM)
    {
        if (ui->confirm_action == UI_FIRE_OUTPUT && client->ignition &&
            client->ignition->have_grant)
            (void)IgnitionClient_RequestDisarm(client->ignition, now);
        Ui_Open(ui, ui->parent);
        return false;
    }
    if (ui->page == ST_UI_SETTINGS)
    {
        Ui_Open(ui, Ui_ModePage(ui->mode));
        return false;
    }
    Ui_Open(ui, ST_UI_HOME);
    return false; /* Navigation never sends capture STOP. */
}
static bool Ui_Start(st_ui *ui, st2_client *client)
{
    if (Task_IsActive(&client->task))
    {
        if (ui->mode == ST_TASK_COMBINED && client->task.mode == ST_TASK_COMBINED &&
            client->task.state == ST_TASK_RUNNING && !client->output_started)
            Ui_Confirm(ui,
                       UI_ARM_OUTPUT); /* A separate execution confirmation, not another task. */
        return false;
    }
    if (client->task.state == ST_TASK_UNKNOWN)
    {
        ui->notice = ST_UI_NOTICE_STATE;
        return false;
    }
    Ui_Confirm(ui, ui->mode == ST_TASK_OUTPUT ? UI_ARM_OUTPUT : UI_START_CAPTURE);
    return false;
}
bool st_ui2_handle(st_ui *ui, st_ui_event event, st2_client *client, uint32_t now)
{
    ui->notice = ST_UI_NOTICE_NONE;
    if (ui->page == ST_UI_CALIBRATION)
    {
        bool idle = st2_client_fresh(client, now) && (client->flags & 7u) == ST_IDLE;
        if (ui->calibration && st_cal_ui_handle(ui->calibration, (st_cal_ui_event)event, idle))
            Ui_Open(ui, ST_UI_SETTINGS);
        return false;
    }
    if (event == ST_UI_BACK)
        return Ui_Cancel(ui, client, now);
    unsigned count = Ui_GetItemCount(ui);
    if (event == ST_UI_NEXT || event == ST_UI_PREV)
    {
        ui->selection = (ui->selection + (event == ST_UI_NEXT ? 1u : count - 1u)) % count;
        if (ui->selection < ui->top)
            ui->top = ui->selection;
        if (ui->selection >= ui->top + ST_OLED_ROWS)
            ui->top = ui->selection - ST_OLED_ROWS + 1u;
        return false;
    }
    if (event != ST_UI_PRESS)
        return false;
    if (ui->page == ST_UI_HOME)
    {
        static const st_ui_page pages[] = {ST_UI_CAPTURE, ST_UI_COMBINED, ST_UI_IGNITION_ONLY,
                                           ST_UI_STATUS};
        unsigned selection = ui->selection;
        if (selection < 3)
            ui->mode = (StTaskMode)(selection == 0   ? ST_TASK_CAPTURE
                                    : selection == 1 ? ST_TASK_COMBINED
                                                     : ST_TASK_OUTPUT);
        Ui_Open(ui, pages[selection]);
        return false;
    }
    if (ui->selection == 0)
        return Ui_Cancel(ui, client, now);
    if (Ui_IsMode(ui->page))
    {
        if (ui->selection == 1)
            return Ui_Start(ui, client);
        if (ui->selection == 2)
        {
            /* Recording STOP also revokes pending execution at the receiver.
             * Do not depend on a second RF packet to make STOP safe. */
            ui->await_arm = false;
            if (client->ignition)
                client->ignition->have_grant = false;
            return st2_client_command(client, ST2_STOP, now);
        }
        if (ui->selection == 3)
        {
            Ui_Open(ui, ST_UI_SETTINGS);
            return false;
        }
    }
    if (ui->page == ST_UI_CONFIRM)
    {
        uint8_t action = ui->confirm_action;
        st_ui_page parent = ui->parent;
        Ui_Open(ui, parent);
        if (!st2_client_fresh(client, now) || client->pending || client->need_recovery)
        {
            ui->notice = ST_UI_NOTICE_LINK;
            return false;
        }
        if (action == UI_START_CAPTURE)
        {
            client->task.mode = ui->mode;
            client->output_started = false;
            return st2_client_command(client, ST2_START, now);
        }
        if (action == UI_ZERO_SENSOR)
            return Link_RequestSensor(client, 7, false, now);
        IgnitionClient *output = client->ignition;
        if (!output || output->pending)
            return false;
        output->transaction_source = &client->next_command;
        if (action == UI_ARM_OUTPUT)
        {
            if (client->output_started && client->task.state != ST_TASK_COMPLETED &&
                client->task.state != ST_TASK_STOPPED)
                return false;
            client->output_started = false;
            if (!output->profile_id)
            {
                (void)IgnitionClient_Query(output, true, 0, now);
                ui->notice = ST_UI_NOTICE_STATE;
                return false;
            }
            if (IgnitionClient_RequestArm(output, ui->mode == ST_TASK_COMBINED ? 2 : 1,
                                          output->profile_id, now) != IGNITION_REQUEST_OK)
                return false;
            if (ui->mode == ST_TASK_OUTPUT)
            {
                uint32_t id = st_get32(output->request + IGNITION_ID_OFFSET);
                if (Task_Begin(&client->task, ST_TASK_OUTPUT, id) != ST_TASK_OK)
                {
                    IgnitionClient_Fail(output);
                    return false;
                }
            }
            ui->await_arm = true;
            return true;
        }
        if (action == UI_FIRE_OUTPUT &&
            IgnitionClient_RequestFire(output, now) == IGNITION_REQUEST_OK)
        {
            ui->ignition_target = st_get32(output->request + IGNITION_ID_OFFSET);
            client->output_started = true;
            return true;
        }
        return false;
    }
    if (ui->page == ST_UI_SETTINGS)
    {
        if (ui->selection == 1)
        {
            Ui_Confirm(ui, UI_ZERO_SENSOR);
            return false;
        }
        if (ui->selection == 2)
        {
            if ((client->flags & 7u) != ST_IDLE || !ui->calibration)
                return false;
            st_cal_ui_open(ui->calibration);
            Ui_Open(ui, ST_UI_CALIBRATION);
            return false;
        }
        if (!client->sensor_have_view)
            return Link_RequestSensor(client, 0, false, now);
        return Link_RequestSensor(client, 8, client->sensor_view[1] == 0, now);
    }
    if (ui->page == ST_UI_STATUS)
    {
        if (ui->selection == 4 || ui->selection == 5)
            return Link_QueryDetails(client, now);
        if (ui->selection == 7)
            return st2_client_connect(client, now);
        if (ui->selection == 8)
            return Link_RequestSensor(client, 0, false, now);
    }
    return false;
}
void st_ui2_service(st_ui *ui, st2_client *client, uint32_t now)
{
    IgnitionClient *output = client->ignition;
    if (output)
    {
        output->transaction_source = &client->next_command;
        if (output->have_grant && (int32_t)(now - output->grant_deadline_ms) >= 0)
            output->have_grant = false;
        if (ui->await_arm && !output->pending)
        {
            ui->await_arm = false;
            if (output->have_grant && Ui_IsMode(ui->page))
            {
                Ui_Confirm(ui, UI_FIRE_OUTPUT);
            }
            else if (output->have_grant)
                (void)IgnitionClient_RequestDisarm(output, now);
        }
        if (!output->pending && !client->pending && st2_client_fresh(client, now) &&
            (uint32_t)(now - ui->ignition_poll_ms) >= ST_LINK_HEARTBEAT_MS)
        {
            ui->ignition_poll_ms = now;
            if (output->state == IGNITION_LOCKOUT && client->output_started)
                (void)IgnitionClient_RequestDisarm(output, now);
            else if (!output->have_grant && ((Ui_IsMode(ui->page) && ui->mode != ST_TASK_CAPTURE &&
                                              !output->have_details) ||
                                             (client->output_started &&
                                              (output->state == IGNITION_FIRING ||
                                               output->operation == IGNITION_OPERATION_UNKNOWN)) ||
                                             (output->state == IGNITION_ARMED)))
                (void)IgnitionClient_Query(output, !output->have_details, ui->ignition_target, now);
        }
    }
    if (ui->page == ST_UI_SETTINGS && !client->pending &&
        (!client->sensor_have_view || (uint32_t)(now - ui->sensor_poll_ms) >= ST_LINK_HEARTBEAT_MS))
    {
        ui->sensor_poll_ms = now;
        (void)Link_RequestSensor(client, 0, false, now);
    }
}
static const char *Ui_TaskState(const st2_client *client, uint32_t now)
{
    static const char *zh[] = {"空闲",   "启动中", "采集中",   "停止中",
                               "已结束", "已停止", "确定失败", "未确认"};
    static const char *en[] = {"IDLE",  "STARTING", "RECORDING", "STOPPING",
                               "ENDED", "STOPPED",  "FAILED",    "UNKNOWN"};
    unsigned state = (unsigned)client->task.state;
    if (Task_IsActive(&client->task) && !st2_client_fresh(client, now))
        state = ST_TASK_UNKNOWN;
    if (state > ST_TASK_UNKNOWN)
        state = ST_TASK_UNKNOWN;
    if (state == ST_TASK_RUNNING && client->task.mode == ST_TASK_OUTPUT)
        return Ui_Text("控制中", "OUTPUT ACTIVE");
    if (state == ST_TASK_STARTING && client->task.mode == ST_TASK_OUTPUT && client->ignition &&
        client->ignition->have_grant)
        return Ui_Text("已准备", "PREPARED");
    return Ui_Text(zh[state], en[state]);
}
static void Ui_GetLine(const st_ui *ui, const st2_client *client, st_voltage_view own, uint32_t now,
                       unsigned item, char *line)
{
    const char *text = "";
    if (ui->page == ST_UI_HOME)
    {
        static const char *zh[] = {"仅采集", "采集与点火", "仅点火", "状态与设置"};
        static const char *en[] = {"CAPTURE ONLY", "CAPTURE + OUTPUT", "OUTPUT ONLY",
                                   "STATUS / SETTINGS"};
        text = Ui_Text(zh[item], en[item]);
    }
    else if (Ui_IsMode(ui->page))
    {
        if (!item)
            text = Ui_Text("返回", "BACK");
        else if (item == 1)
        {
            const char *state =
                ui->notice == ST_UI_NOTICE_LINK    ? Ui_Text("链路或任务忙", "LINK / BUSY")
                : ui->notice == ST_UI_NOTICE_STATE ? Ui_Text("未启用或未确认", "DISABLED")
                : client->task.mode == ui->mode    ? Ui_TaskState(client, now)
                                                   : Ui_Text("空闲", "IDLE");
            (void)snprintf(line, ST_UI_ROW_BYTES, "%s%s%s", Ui_Text("开始", "START"),
                           Ui_Text("·", " / "), state);
            return;
        }
        else
            text = item == 2 ? Ui_Text("停止", "STOP") : Ui_Text("传感器设置", "SENSOR SETTINGS");
    }
    else if (ui->page == ST_UI_CONFIRM)
        text = item == 0                                ? Ui_Text("返回", "BACK")
               : ui->confirm_action == UI_START_CAPTURE ? Ui_Text("确认开始", "CONFIRM START")
               : ui->confirm_action == UI_ARM_OUTPUT    ? Ui_Text("确认准备", "CONFIRM ARM")
               : ui->confirm_action == UI_FIRE_OUTPUT ? Ui_Text("确认控制动作", "CONFIRM OUTPUT")
                                                      : Ui_Text("确认无载荷置零", "ZERO: NO LOAD");
    else if (ui->page == ST_UI_SETTINGS)
    {
        if (item == 3)
        {
            (void)snprintf(line, ST_UI_ROW_BYTES, "%s%s", Ui_Text("自动置零：", "AUTO ZERO: "),
                           !client->sensor_have_view ? Ui_Text("未知", "UNKNOWN")
                           : client->sensor_view[1]  ? Ui_Text("开", "ON")
                                                     : Ui_Text("关", "OFF"));
            return;
        }
        text = item == 0   ? Ui_Text("返回", "BACK")
               : item == 1 ? Ui_Text("置零", "ZERO")
                           : Ui_Text("重新标定", "RECALIBRATE");
    }
    else if (ui->page == ST_UI_STATUS)
    {
        if (item == 0)
            text = Ui_Text("返回", "BACK");
        else if (item == 1)
        {
            StLinkHealth health =
                Link_GetHealth(client->have_status, client->received_ms, now, client->driver_fault);
            const char *state = health == ST_LINK_OK             ? Ui_Text("正常", "OK")
                                : health == ST_LINK_SLOW         ? Ui_Text("延迟", "DELAY")
                                : health == ST_LINK_DRIVER_FAULT ? Ui_Text("驱动故障", "RF FAULT")
                                                                 : Ui_Text("未连接", "LOST");
            (void)snprintf(line, ST_UI_ROW_BYTES, "%s%s", Ui_Text("链路：", "LINK: "), state);
            return;
        }
        else if (item == 2)
        {
            if (!st2_client_fresh(client, now) || !(client->flags & 32u) ||
                client->mv == UINT16_MAX)
                text = Ui_Text("采集电池：未知", "ACQ BAT UNKNOWN");
            else
            {
                (void)snprintf(line, ST_UI_ROW_BYTES, "%s%u.%02uV",
                               Ui_Text("采集电池：", "ACQ BAT "), (unsigned)client->mv / 1000u,
                               ((unsigned)client->mv % 1000u) / 10u);
                return;
            }
        }
        else if (item == 3)
        {
            const char *state = !st2_client_fresh(client, now) ? Ui_Text("未知", "UNKNOWN")
                                : ((client->flags >> 3) & 3u) == ST2_TF_OPEN
                                    ? Ui_Text("记录中", "OPEN")
                                : ((client->flags >> 3) & 3u) == ST2_TF_READY
                                    ? Ui_Text("已关闭", "CLOSED")
                                    : Ui_Text("异常未知", "ERROR / UNKNOWN");
            (void)snprintf(line, ST_UI_ROW_BYTES, "TF: %s", state);
            return;
        }
        else if (item == 4 && !st2_client_fresh(client, now))
            text = Ui_Text("质量：未知", "QUALITY: UNKNOWN");
        else if (item == 4)
            text = (client->flags & 128u) ? Ui_Text("质量：需查询", "QUALITY: CHECK")
                                          : Ui_Text("质量：正常", "QUALITY: OK");
        else if (item == 5 && !st2_client_fresh(client, now))
            text = Ui_Text("ADC：未知", "ADC: UNKNOWN");
        else if (item == 5 && client->details.complete &&
                 (uint32_t)(now - client->details_received_ms) < ST_LINK_LOST_MS)
        {
            (void)snprintf(line, ST_UI_ROW_BYTES, "ADC %luHz %ums",
                           (unsigned long)(st_get32(client->details.data) / 1000u),
                           (unsigned)st_get16(client->details.data + 20));
            return;
        }
        else if (item == 5)
            text = (client->flags & 64u) ? Ui_Text("ADC：采样新鲜", "ADC: FRESH")
                                         : Ui_Text("ADC：无新数据", "ADC: NO FRESH DATA");
        else if (item == 6)
        {
            if (own.kind == ST_V_UNKNOWN || own.kind == ST_V_STALE)
                text = Ui_Text("遥控电池：未知", "CTRL BAT UNKNOWN");
            else
            {
                (void)snprintf(line, ST_UI_ROW_BYTES, "%s%lu.%02luV",
                               Ui_Text("遥控电池：", "CTRL BAT "),
                               (unsigned long)(own.mv / 1000u),
                               (unsigned long)((own.mv % 1000u) / 10u));
                return;
            }
        }
        else
            text = item == 7 ? Ui_Text("恢复连接", "RESUME LINK")
                             : Ui_Text("查询传感器", "QUERY SENSOR");
    }
    (void)snprintf(line, ST_UI_ROW_BYTES, "%s", text);
}
void st_ui2_render(const st_ui *ui, const st2_client *client, st_voltage_view own, uint32_t now,
                   st_ui_model *model)
{
    memset(model, 0, sizeof *model);
    if (ui->page == ST_UI_CALIBRATION && ui->calibration)
    {
        st_cal_panel panel;
        st_cal_ui_render(ui->calibration, &panel);
        memcpy(model->rows, panel.rows, sizeof model->rows);
        model->inverse = panel.inverse;
        model->inverse_row = panel.inverse_row;
        model->inverse_column = panel.inverse_column;
        model->inverse_digit = panel.inverse_digit;
        return;
    }
    unsigned count = Ui_GetItemCount(ui), top = ui->top;
    if (ui->selection < top)
        top = ui->selection;
    if (ui->selection >= top + ST_OLED_ROWS)
        top = ui->selection - ST_OLED_ROWS + 1u;
    for (unsigned row = 0; row < ST_OLED_ROWS && top + row < count; row++)
        Ui_GetLine(ui, client, own, now, top + row, model->rows[row]);
    model->inverse = true;
    model->inverse_row = (uint8_t)(ui->selection - top);
}
