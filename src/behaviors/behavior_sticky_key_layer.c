/*
 * Layer-Switching Sticky Key – Phase 2
 * -----------------------------------
 * One-shot modifier that temporarily activates a target layer while the
 * modifier is active. Usage: &skl LCTRL ENG - applies LCTRL modifier
 * while temporarily switching to ENG layer for the duration.
 */

#define DT_DRV_COMPAT zmk_behavior_sticky_key_layer

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>

#include <zmk/matrix.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/modifiers_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define KEY_PRESS DEVICE_DT_NAME(DT_INST(0, zmk_behavior_key_press))

#define ZMK_BHV_STICKY_KEY_MAX_HELD CONFIG_ZMK_BEHAVIOR_STICKY_KEY_MAX_HELD
#define ZMK_BHV_STICKY_KEY_POSITION_FREE UINT32_MAX

struct behavior_sticky_key_config {
    uint32_t release_after_ms;
    bool quick_release;
    bool lazy;
    bool ignore_modifiers;
    zmk_keymap_layer_id_t target_layer;
    struct zmk_behavior_binding behavior;
};

struct active_sticky_key {
    uint32_t position;
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    uint8_t source;
#endif
    uint32_t param1;
    uint32_t param2;
    const struct behavior_sticky_key_config *config;
    bool timer_started;
    bool timer_cancelled;
    int64_t release_at;
    struct k_work_delayable release_timer;
    uint8_t modified_key_usage_page;
    uint32_t modified_key_keycode;
    // NEW: target layer for switching
    zmk_keymap_layer_id_t target_layer;
    bool layer_was_active; // track if layer was already active before we activated it
    // NEW: save original layer state for restoration
    zmk_keymap_layers_state_t saved_layer_state;
};

static struct active_sticky_key active_sticky_keys[ZMK_BHV_STICKY_KEY_MAX_HELD] = {};

static struct active_sticky_key *store_sticky_key(struct zmk_behavior_binding_event *event,
                                                  uint32_t param1,
                                                  const struct behavior_sticky_key_config *config) {
    for (int i = 0; i < ZMK_BHV_STICKY_KEY_MAX_HELD; i++) {
        struct active_sticky_key *const sticky_key = &active_sticky_keys[i];
        if (sticky_key->position != ZMK_BHV_STICKY_KEY_POSITION_FREE ||
            sticky_key->timer_cancelled) {
            continue;
        }
        sticky_key->position = event->position;
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        sticky_key->source = event->source;
#endif
        sticky_key->param1 = param1;
        sticky_key->param2 = 0; // Not used anymore
        sticky_key->config = config;
        sticky_key->release_at = 0;
        sticky_key->timer_cancelled = false;
        sticky_key->timer_started = false;
        sticky_key->modified_key_usage_page = 0;
        sticky_key->modified_key_keycode = 0;
        // NEW: store target layer from config
        sticky_key->target_layer = config->target_layer;
        sticky_key->layer_was_active = zmk_keymap_layer_active(sticky_key->target_layer);
        sticky_key->saved_layer_state = 0; // Will be set when needed

        LOG_DBG("SKL: stored sticky key pos=%d, mod=%d, target_layer=%d, layer_was_active=%d",
                event->position, param1, sticky_key->target_layer, sticky_key->layer_was_active);
        return sticky_key;
    }
    return NULL;
}

static void clear_sticky_key(struct active_sticky_key *sticky_key) {
    LOG_DBG("SKL: clearing sticky key pos=%d", sticky_key->position);
    sticky_key->position = ZMK_BHV_STICKY_KEY_POSITION_FREE;
}

static struct active_sticky_key *find_sticky_key(uint32_t position) {
    for (int i = 0; i < ZMK_BHV_STICKY_KEY_MAX_HELD; i++) {
        if (active_sticky_keys[i].position == position && !active_sticky_keys[i].timer_cancelled) {
            return &active_sticky_keys[i];
        }
    }
    return NULL;
}

static inline int activate_target_layer(struct active_sticky_key *sticky_key) {
    if (!zmk_keymap_layer_active(sticky_key->target_layer)) {
        LOG_DBG("SKL: activating target layer %d", sticky_key->target_layer);
        return zmk_keymap_layer_activate(sticky_key->target_layer);
    }
    LOG_DBG("SKL: target layer %d already active", sticky_key->target_layer);
    return 0;
}

static inline int deactivate_target_layer(struct active_sticky_key *sticky_key) {
    if (!sticky_key->layer_was_active && zmk_keymap_layer_active(sticky_key->target_layer)) {
        LOG_DBG("SKL: deactivating target layer %d", sticky_key->target_layer);
        return zmk_keymap_layer_deactivate(sticky_key->target_layer);
    }
    LOG_DBG("SKL: not deactivating layer %d (was_active_before=%d)", sticky_key->target_layer,
            sticky_key->layer_was_active);
    return 0;
}

static inline int press_sticky_key_behavior(struct active_sticky_key *sticky_key,
                                            int64_t timestamp) {
    LOG_DBG("SKL: pressing sticky key behavior, mod=%d", sticky_key->param1);

    // Set target layer as highest priority BEFORE pressing the modifier
    activate_target_layer(sticky_key);

    struct zmk_behavior_binding binding = {
        .behavior_dev = sticky_key->config->behavior.behavior_dev,
        .param1 = sticky_key->param1,
        .param2 = 0, // Only pass the modifier, not the layer
    };
    struct zmk_behavior_binding_event event = {
        .position = sticky_key->position,
        .timestamp = timestamp,
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = sticky_key->source,
#endif
    };
    return zmk_behavior_invoke_binding(&binding, event, true);
}

static inline int release_sticky_key_behavior(struct active_sticky_key *sticky_key,
                                              int64_t timestamp) {
    LOG_DBG("SKL: releasing sticky key behavior, mod=%d, restoring layers", sticky_key->param1);

    struct zmk_behavior_binding binding = {
        .behavior_dev = sticky_key->config->behavior.behavior_dev,
        .param1 = sticky_key->param1,
        .param2 = 0, // Only pass the modifier, not the layer
    };
    struct zmk_behavior_binding_event event = {
        .position = sticky_key->position,
        .timestamp = timestamp,
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = sticky_key->source,
#endif
    };

    // Restore layer state BEFORE releasing modifier
    deactivate_target_layer(sticky_key);

    clear_sticky_key(sticky_key);
    return zmk_behavior_invoke_binding(&binding, event, false);
}

static inline void on_sticky_key_timeout(struct active_sticky_key *sticky_key) {
    LOG_DBG("SKL: sticky key timeout, pos=%d", sticky_key->position);
    if (sticky_key->config->lazy) {
        // Restore layer state on timeout for lazy keys
        deactivate_target_layer(sticky_key);
        clear_sticky_key(sticky_key);
    } else {
        release_sticky_key_behavior(sticky_key, sticky_key->release_at);
    }
}

static int stop_timer(struct active_sticky_key *sticky_key) {
    int timer_cancel_result = k_work_cancel_delayable(&sticky_key->release_timer);
    if (timer_cancel_result == -EINPROGRESS) {
        sticky_key->timer_cancelled = true;
    }
    return timer_cancel_result;
}

static int on_sticky_key_binding_pressed(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    LOG_DBG("SKL: binding pressed, pos=%d, param1=%d", event.position, binding->param1);
    LOG_DBG("SKL: param1 hex=0x%X", binding->param1);

    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_sticky_key_config *cfg = dev->config;
    struct active_sticky_key *sticky_key;

    LOG_DBG("SKL: config target_layer=%d", cfg->target_layer);

    sticky_key = find_sticky_key(event.position);
    if (sticky_key != NULL) {
        LOG_DBG("SKL: found existing sticky key, releasing it first");
        stop_timer(sticky_key);
        release_sticky_key_behavior(sticky_key, event.timestamp);
    }

    sticky_key = store_sticky_key(&event, binding->param1, cfg);
    if (sticky_key == NULL) {
        LOG_ERR("SKL: unable to store sticky key, did you press more than %d sticky_key?",
                ZMK_BHV_STICKY_KEY_MAX_HELD);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    LOG_DBG("SKL: new sticky_key at pos %d, lazy=%d, target_layer=%d", event.position,
            sticky_key->config->lazy, sticky_key->target_layer);

    if (!sticky_key->config->lazy) {
        // Press the key now if it's not lazy
        press_sticky_key_behavior(sticky_key, event.timestamp);
    } else {
        // For lazy keys, set layer priority immediately even if modifier waits
        activate_target_layer(sticky_key);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_sticky_key_binding_released(struct zmk_behavior_binding *binding,
                                          struct zmk_behavior_binding_event event) {
    LOG_DBG("SKL: binding released, pos=%d", event.position);

    struct active_sticky_key *sticky_key = find_sticky_key(event.position);
    if (sticky_key == NULL) {
        LOG_ERR("SKL: ACTIVE STICKY KEY CLEARED TOO EARLY");
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if (sticky_key->modified_key_usage_page != 0 && sticky_key->modified_key_keycode != 0) {
        LOG_DBG("SKL: Another key was pressed while sticky key was active, acting like normal key");
        return release_sticky_key_behavior(sticky_key, event.timestamp);
    }

    // No other key was pressed. Start the timer.
    LOG_DBG("SKL: starting timer for sticky key");
    sticky_key->timer_started = true;
    sticky_key->release_at = event.timestamp + sticky_key->config->release_after_ms;
    int32_t ms_left = sticky_key->release_at - k_uptime_get();
    if (ms_left > 0) {
        k_work_schedule(&sticky_key->release_timer, K_MSEC(ms_left));
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static int sticky_key_parameter_domains(const struct device *sk,
                                        struct behavior_parameter_metadata *param_metadata) {
    const struct behavior_sticky_key_config *cfg = sk->config;
    struct behavior_parameter_metadata child_metadata;
    int err = behavior_get_parameter_metadata(zmk_behavior_get_binding(cfg->behavior.behavior_dev),
                                              &child_metadata);
    if (err < 0) {
        LOG_WRN("Failed to get the sticky key bound behavior parameter: %d", err);
        return err;
    }
    for (int s = 0; s < child_metadata.sets_len; s++) {
        const struct behavior_parameter_metadata_set *set = &child_metadata.sets[s];
        if (set->param2_values_len > 0) {
            return -ENOTSUP;
        }
    }
    *param_metadata = child_metadata;
    return 0;
}
#endif

static const struct behavior_driver_api behavior_sticky_key_layer_driver_api = {
    .binding_pressed = on_sticky_key_binding_pressed,
    .binding_released = on_sticky_key_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = sticky_key_parameter_domains,
#endif
};

static int sticky_key_keycode_state_changed_listener(const zmk_event_t *eh);

ZMK_LISTENER(behavior_sticky_key_layer, sticky_key_keycode_state_changed_listener);
ZMK_SUBSCRIPTION(behavior_sticky_key_layer, zmk_keycode_state_changed);

static int sticky_key_keycode_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    struct active_sticky_key *press_before[ZMK_BHV_STICKY_KEY_MAX_HELD] = {};
    struct active_sticky_key *release_after[ZMK_BHV_STICKY_KEY_MAX_HELD] = {};

    const struct zmk_keycode_state_changed ev_copy = *ev;

    LOG_DBG("SKL: keycode event: usage_page=%d, keycode=%d, state=%d", ev_copy.usage_page,
            ev_copy.keycode, ev_copy.state);

    // Find active sticky keys for layer restoration
    struct active_sticky_key *target_layer_key = NULL;
    zmk_keymap_layers_state_t saved_layer_state_to_restore = 0;

    for (int i = 0; i < ZMK_BHV_STICKY_KEY_MAX_HELD; i++) {
        struct active_sticky_key *sticky_key = &active_sticky_keys[i];
        if (sticky_key->position == ZMK_BHV_STICKY_KEY_POSITION_FREE) {
            continue;
        }

        LOG_DBG("SKL: checking sticky key pos=%d, mod=%d, layer=%d", sticky_key->position,
                sticky_key->param1, sticky_key->target_layer);

        if (strcmp(sticky_key->config->behavior.behavior_dev, KEY_PRESS) == 0 &&
            ZMK_HID_USAGE_ID(sticky_key->param1) == ev_copy.keycode &&
            ZMK_HID_USAGE_PAGE(sticky_key->param1) == ev_copy.usage_page &&
            SELECT_MODS(sticky_key->param1) == ev_copy.implicit_modifiers) {
            LOG_DBG("SKL: ignoring self-generated event");
            continue;
        }

        if (ev_copy.state) { // key down
            if (sticky_key->config->ignore_modifiers &&
                is_mod(ev_copy.usage_page, ev_copy.keycode)) {
                LOG_DBG("SKL: ignoring modifier key due to ignore_modifiers");
                continue;
            }
            if (sticky_key->modified_key_usage_page != 0 || sticky_key->modified_key_keycode != 0) {
                LOG_DBG("SKL: sticky key already in use for another key");
                continue;
            }

            LOG_DBG("SKL: activating sticky key for new key press");
            stop_timer(sticky_key);
            if (sticky_key->release_at != 0 && ev_copy.timestamp > sticky_key->release_at) {
                LOG_DBG("SKL: sticky key timed out, cleaning up");
                on_sticky_key_timeout(sticky_key);
                continue;
            }

            // This sticky key will be used for layer restoration
            if (target_layer_key == NULL) {
                target_layer_key = sticky_key;
                // Save the layer state before any sticky keys get cleared
                saved_layer_state_to_restore = sticky_key->saved_layer_state;
            }

            if (sticky_key->config->lazy) {
                LOG_DBG("SKL: marking lazy sticky key for press");
                press_before[i] = sticky_key;
            }
            if (sticky_key->timer_started && sticky_key->config->quick_release) {
                LOG_DBG("SKL: marking sticky key for quick release");
                release_after[i] = sticky_key;
            }
            sticky_key->modified_key_usage_page = ev_copy.usage_page;
            sticky_key->modified_key_keycode = ev_copy.keycode;
        } else {
            if (sticky_key->timer_started &&
                sticky_key->modified_key_usage_page == ev_copy.usage_page &&
                sticky_key->modified_key_keycode == ev_copy.keycode) {
                LOG_DBG("SKL: key released, marking sticky key for release");
                stop_timer(sticky_key);
                release_after[i] = sticky_key;
            }
        }
    }

    for (int i = 0; i < ZMK_BHV_STICKY_KEY_MAX_HELD; i++) {
        if (press_before[i]) {
            LOG_DBG("SKL: pressing lazy sticky key before reraise");
            press_sticky_key_behavior(press_before[i], ev_copy.timestamp);
        }
    }

    bool event_reraised = false;
    for (int i = 0; i < ZMK_BHV_STICKY_KEY_MAX_HELD; i++) {
        if (!release_after[i]) {
            continue;
        }
        if (!event_reraised) {
            LOG_DBG("SKL: reraising event");
            struct zmk_keycode_state_changed_event dupe_ev =
                copy_raised_zmk_keycode_state_changed(ev);
            ZMK_EVENT_RAISE_AFTER(dupe_ev, behavior_sticky_key_layer);
            event_reraised = true;
        }
        LOG_DBG("SKL: releasing sticky key after reraise");
        release_sticky_key_behavior(release_after[i], ev_copy.timestamp);
    }

    // Restore layer state after processing if we changed it (only on key press)
    if (target_layer_key && ev_copy.state && saved_layer_state_to_restore != 0) {
        LOG_DBG("SKL: restoring layer state after key processing");
        // Restore all layers
        for (zmk_keymap_layer_id_t layer = 0; layer < ZMK_KEYMAP_LAYERS_LEN; layer++) {
            bool should_be_active = (saved_layer_state_to_restore & BIT(layer)) != 0;
            bool currently_active = zmk_keymap_layer_active(layer);

            if (should_be_active && !currently_active) {
                zmk_keymap_layer_activate(layer);
            } else if (!should_be_active && currently_active &&
                       layer != zmk_keymap_layer_default()) {
                zmk_keymap_layer_deactivate(layer);
            }
        }
    }

    return event_reraised ? ZMK_EV_EVENT_CAPTURED : ZMK_EV_EVENT_BUBBLE;
}

static int sticky_key_position_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    // Only care about key presses, not releases
    if (!ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    // Check if we have any active sticky keys that need layer switching
    struct active_sticky_key *target_layer_key = NULL;
    bool found_any_sticky_key = false;

    for (int i = 0; i < ZMK_BHV_STICKY_KEY_MAX_HELD; i++) {
        struct active_sticky_key *sticky_key = &active_sticky_keys[i];
        if (sticky_key->position == ZMK_BHV_STICKY_KEY_POSITION_FREE) {
            continue;
        }

        // Skip if sticky key already has a modified key (unless it's the same position - multi-tap
        // case)
        if ((sticky_key->modified_key_usage_page != 0 || sticky_key->modified_key_keycode != 0) &&
            sticky_key->position != ev->position) {
            continue;
        }

        // We found a sticky key that should handle layer switching
        found_any_sticky_key = true;

        // For multiple sticky keys, use the first one with a target layer, but prefer any that
        // don't already have a modified key
        if (target_layer_key == NULL || (target_layer_key->modified_key_usage_page != 0 &&
                                         sticky_key->modified_key_usage_page == 0)) {
            target_layer_key = sticky_key;
        }
    }

    // If we have a target layer key, check if we should switch layers for this position
    if (target_layer_key && found_any_sticky_key) {
        // Find the highest active layer for this position (same logic as ZMK's keymap resolution)
        zmk_keymap_layer_id_t current_layer = ZMK_KEYMAP_LAYER_ID_INVAL;

        // Iterate from highest to lowest layer to find which layer would handle this position
        for (zmk_keymap_layer_id_t layer_id = ZMK_KEYMAP_LAYERS_LEN - 1;
             layer_id >= zmk_keymap_layer_default(); layer_id--) {

            if (zmk_keymap_layer_active(layer_id)) {
                current_layer = layer_id;
                break;
            }
        }

        if (current_layer != ZMK_KEYMAP_LAYER_ID_INVAL) {
            // Get the current binding for this position to see what type it is
            const struct zmk_behavior_binding *binding =
                zmk_keymap_get_layer_binding_at_idx(current_layer, ev->position);

            if (binding && binding->behavior_dev) {
                const char *behavior_name = binding->behavior_dev;
                LOG_DBG("SKL: position %d binding: %s", ev->position, behavior_name);

                // Only switch layers for regular key presses, not for other behaviors
                if (strcmp(behavior_name, "key_press") == 0) {
                    // Only switch to English if we're currently on a language layer (0 or 1)
                    // Don't switch if we're on function layers (7, etc.) - keep those keys on the
                    // function layer
                    if (current_layer <= 1) {
                        LOG_DBG("SKL: intercepting position %d, switching to layer %d before "
                                "keymap lookup (current layer: %d)",
                                ev->position, target_layer_key->target_layer, current_layer);
                        target_layer_key->saved_layer_state = zmk_keymap_layer_state();
                        zmk_keymap_layer_to(target_layer_key->target_layer);
                        LOG_DBG("SKL: position %d will use sticky key at pos %d for layer %d",
                                ev->position, target_layer_key->position,
                                target_layer_key->target_layer);
                    } else {
                        LOG_DBG("SKL: skipping layer switch for position %d (on function layer %d)",
                                ev->position, current_layer);
                    }
                } else {
                    LOG_DBG("SKL: skipping layer switch for position %d (behavior: %s)",
                            ev->position, behavior_name);
                }
            } else {
                LOG_DBG("SKL: no binding found for position %d on layer %d", ev->position,
                        current_layer);
            }
        } else {
            LOG_DBG("SKL: no active layer found for position %d", ev->position);
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(behavior_sticky_key_layer_position, sticky_key_position_state_changed_listener);
ZMK_SUBSCRIPTION(behavior_sticky_key_layer_position, zmk_position_state_changed);

void behavior_sticky_key_layer_timer_handler(struct k_work *item) {
    struct k_work_delayable *d_work = k_work_delayable_from_work(item);
    struct active_sticky_key *sticky_key =
        CONTAINER_OF(d_work, struct active_sticky_key, release_timer);
    if (sticky_key->position == ZMK_BHV_STICKY_KEY_POSITION_FREE) {
        return;
    }
    if (sticky_key->timer_cancelled) {
        sticky_key->timer_cancelled = false;
    } else {
        on_sticky_key_timeout(sticky_key);
    }
}

static int behavior_sticky_key_layer_init(const struct device *dev) {
    static bool init_first_run = true;
    if (init_first_run) {
        for (int i = 0; i < ZMK_BHV_STICKY_KEY_MAX_HELD; i++) {
            k_work_init_delayable(&active_sticky_keys[i].release_timer,
                                  behavior_sticky_key_layer_timer_handler);
            active_sticky_keys[i].position = ZMK_BHV_STICKY_KEY_POSITION_FREE;
        }
    }
    init_first_run = false;
    return 0;
}

struct behavior_sticky_key_data {};
static struct behavior_sticky_key_data behavior_sticky_key_layer_data;

#define SKL_INST(n)                                                                                \
    static const struct behavior_sticky_key_config behavior_sticky_key_layer_config_##n = {        \
        .behavior = ZMK_KEYMAP_EXTRACT_BINDING(0, DT_DRV_INST(n)),                                 \
        .target_layer = DT_INST_PROP(n, target_layer),                                             \
        .release_after_ms = DT_INST_PROP(n, release_after_ms),                                     \
        .quick_release = DT_INST_PROP(n, quick_release),                                           \
        .lazy = DT_INST_PROP(n, lazy),                                                             \
        .ignore_modifiers = DT_INST_PROP(n, ignore_modifiers),                                     \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(                                                                       \
        n, behavior_sticky_key_layer_init, NULL, &behavior_sticky_key_layer_data,                  \
        &behavior_sticky_key_layer_config_##n, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,   \
        &behavior_sticky_key_layer_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SKL_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY */ 