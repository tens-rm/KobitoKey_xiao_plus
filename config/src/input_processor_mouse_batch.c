#define DT_DRV_COMPAT kobitokey_input_processor_mouse_batch

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <drivers/input_processor.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>

struct mouse_batch_data {
    struct k_work_delayable flush_work;
    struct k_spinlock lock;
    int64_t last_report_ms;
    bool pending;
};

K_THREAD_STACK_DEFINE(mouse_batch_workq_stack, 768);
static struct k_work_q mouse_batch_workq;

static int mouse_batch_workq_init(void) {
    k_work_queue_start(&mouse_batch_workq, mouse_batch_workq_stack,
                       K_THREAD_STACK_SIZEOF(mouse_batch_workq_stack),
                       K_PRIO_PREEMPT(10), NULL);
    return 0;
}

SYS_INIT(mouse_batch_workq_init, POST_KERNEL, 25);

static void mouse_batch_flush(struct k_work *work) {
    struct k_work_delayable *delayable = k_work_delayable_from_work(work);
    struct mouse_batch_data *data =
        CONTAINER_OF(delayable, struct mouse_batch_data, flush_work);

    k_spinlock_key_t key = k_spin_lock(&data->lock);
    bool pending = data->pending;
    data->pending = false;
    if (pending) {
        data->last_report_ms = k_uptime_get();
    }
    k_spin_unlock(&data->lock, key);

    if (pending) {
        zmk_endpoints_send_mouse_report();
        zmk_hid_mouse_movement_set(0, 0);
    }
}

static int mouse_batch_init(const struct device *dev) {
    struct mouse_batch_data *data = dev->data;
    k_work_init_delayable(&data->flush_work, mouse_batch_flush);
    return 0;
}

static int mouse_batch_handle_event(const struct device *dev,
                                    struct input_event *event,
                                    uint32_t interval_ms,
                                    uint32_t param2,
                                    struct zmk_input_processor_state *state) {
    ARG_UNUSED(param2);
    ARG_UNUSED(state);

    if (event->type != INPUT_EV_REL || !event->sync ||
        (event->code != INPUT_REL_X && event->code != INPUT_REL_Y)) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    const uint32_t interval = interval_ms == 0 ? 12 : interval_ms;
    struct mouse_batch_data *data = dev->data;
    const int64_t now = k_uptime_get();
    uint32_t delay_ms = 0;

    k_spinlock_key_t key = k_spin_lock(&data->lock);
    const int64_t elapsed = now - data->last_report_ms;
    if (data->last_report_ms == 0 || elapsed >= interval) {
        data->last_report_ms = now;
        data->pending = false;
    } else {
        event->sync = false;
        data->pending = true;
        delay_ms = interval - elapsed;
    }
    k_spin_unlock(&data->lock, key);

    if (delay_ms > 0) {
        k_work_reschedule_for_queue(&mouse_batch_workq, &data->flush_work,
                                    K_MSEC(delay_ms));
    } else {
        k_work_cancel_delayable(&data->flush_work);
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static const struct zmk_input_processor_driver_api mouse_batch_driver_api = {
    .handle_event = mouse_batch_handle_event,
};

#define MOUSE_BATCH_INST(n)                                                    \
    static struct mouse_batch_data mouse_batch_data_##n;                      \
    DEVICE_DT_INST_DEFINE(n, mouse_batch_init, NULL, &mouse_batch_data_##n,    \
                          NULL, POST_KERNEL, 27, &mouse_batch_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MOUSE_BATCH_INST)
