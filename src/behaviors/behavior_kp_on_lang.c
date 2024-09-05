/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_kp_on_lang

#include <stdlib.h>
#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/keymap.h>
#include <zmk/behavior.h>
#include <zmk/language.h>
#include <zmk/events/keycode_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct behavior_kp_on_lang_config {
    struct zmk_behavior_binding switch_behavior;
};

struct behavior_kp_on_lang_data {
    bool switch_back;
    struct zmk_behavior_binding switch_back_behavior;
};

static int behavior_kp_on_lang_init(const struct device *dev) { return 0; };

struct zmk_behavior_binding get_switch_back_binding(uint8_t target_lang,
                                                    const struct zmk_behavior_binding *original) {
    struct zmk_behavior_binding copy;
    memcpy(&copy, original, sizeof(struct zmk_behavior_binding));
    copy.param1 = target_lang;
    return copy;
}

static int kp_on_lang_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                             struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_kp_on_lang_config *config = dev->config;
    struct behavior_kp_on_lang_data *data = dev->data;

    const uint8_t current_lang = zmk_language_state();
    if (current_lang != binding->param2) {
        LOG_DBG("KP_LANG switch from %d to %d", current_lang, config->switch_behavior.param1);
        data->switch_back_behavior =
            get_switch_back_binding(current_lang, &config->switch_behavior);
        data->switch_back = true;
        LOG_DBG("KP_LANG switchback %d", data->switch_back_behavior.param1);
        zmk_behavior_queue_add(event.position, config->switch_behavior, true, 0);
        zmk_behavior_queue_add(event.position, config->switch_behavior, false, 0);
    }
    return raise_zmk_keycode_state_changed_from_encoded(binding->param1, true, event.timestamp);
}

static int kp_on_lang_keymap_binding_released(struct zmk_behavior_binding *binding,
                                              struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_kp_on_lang_data *data = dev->data;

    if (data->switch_back) {
        zmk_behavior_queue_add(event.position, data->switch_back_behavior, true, 0);
        zmk_behavior_queue_add(event.position, data->switch_back_behavior, false, 0);
        data->switch_back = false;
    }
    return raise_zmk_keycode_state_changed_from_encoded(binding->param1, false, event.timestamp);
}

static const struct behavior_driver_api behavior_kp_on_lang_driver_api = {
    .binding_pressed = kp_on_lang_keymap_binding_pressed,
    .binding_released = kp_on_lang_keymap_binding_released};

#define KP_ON_LANG_INST(n)                                                                         \
    static struct behavior_kp_on_lang_data behavior_kp_on_lang_data_##n = {.switch_back = false};  \
    static struct behavior_kp_on_lang_config behavior_kp_on_lang_config_##n = {                    \
        .switch_behavior = ZMK_KEYMAP_EXTRACT_BINDING(0, DT_DRV_INST(n)),                          \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_kp_on_lang_init, NULL, &behavior_kp_on_lang_data_##n,      \
                            &behavior_kp_on_lang_config_##n, APPLICATION,                          \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_kp_on_lang_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KP_ON_LANG_INST)
