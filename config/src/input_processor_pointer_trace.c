#define DT_DRV_COMPAT kobitokey_input_processor_pointer_trace

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/input_processor.h>

LOG_MODULE_REGISTER(kobitokey_pointer_trace, LOG_LEVEL_INF);

struct pointer_trace_data {
    int32_t dx;
    int32_t dy;
    int64_t last_frame_ms;
};

static int pointer_trace_handle_event(const struct device *dev,
                                      struct input_event *event,
                                      uint32_t param1,
                                      uint32_t param2,
                                      struct zmk_input_processor_state *state) {
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    ARG_UNUSED(state);

    if (event->type != INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    struct pointer_trace_data *data = dev->data;
    if (event->code == INPUT_REL_X) {
        data->dx += event->value;
    } else if (event->code == INPUT_REL_Y) {
        data->dy += event->value;
    } else {
        return ZMK_INPUT_PROC_CONTINUE;
    }

#if IS_ENABLED(CONFIG_KOBITOKEY_POINTER_TRACE)
    if (event->sync) {
        const int64_t now_ms = k_uptime_get();
        const int64_t gap_ms = data->last_frame_ms == 0
                                   ? -1
                                   : now_ms - data->last_frame_ms;
        LOG_INF("pointer raw dx=%d dy=%d gap=%lldms event_ts=%lld age=%lldms",
                data->dx, data->dy, (long long)gap_ms,
                (long long)event->timestamp,
                (long long)(now_ms - event->timestamp));
        data->last_frame_ms = now_ms;
        data->dx = 0;
        data->dy = 0;
    }
#endif

    return ZMK_INPUT_PROC_CONTINUE;
}

static const struct zmk_input_processor_driver_api pointer_trace_driver_api = {
    .handle_event = pointer_trace_handle_event,
};

#define POINTER_TRACE_INST(n)                                                 \
    static struct pointer_trace_data pointer_trace_data_##n;                 \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, &pointer_trace_data_##n, NULL,       \
                          POST_KERNEL, 27, &pointer_trace_driver_api);

DT_INST_FOREACH_STATUS_OKAY(POINTER_TRACE_INST)
