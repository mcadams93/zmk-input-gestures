/*
 * Copyright (c) 2025 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

 #include <drivers/input_processor.h>
 #include <zephyr/logging/log.h>
 #include <zmk/endpoints.h>
 #include <zmk/hid.h>
 #include "input_processor_gestures.h"
 
 LOG_MODULE_DECLARE(gestures, CONFIG_ZMK_LOG_LEVEL);

 // Max distance (squared) finger can move from start and still count as a tap.
 // With 1024x1024 coordinate space, 40 units ~= 4% of pad width.
 #define TAP_DISTANCE_THRESHOLD_SQ 1600

 int tap_detection_handle_start(const struct device *dev, struct gesture_event_t *event) {
     struct gesture_config *config = (struct gesture_config *)dev->config;
     struct gesture_data *data = (struct gesture_data *)dev->data;

     if (!config->tap_detection.enabled) {
         return -1;
     }

     data->tap_detection.touch_start_time = event->last_touch_timestamp;
     data->tap_detection.touch_start_x = event->x;
     data->tap_detection.touch_start_y = event->y;
     data->tap_detection.is_waiting_for_tap = true;

     // Safety timeout: if finger stays down longer than this, it's not a tap
     k_work_reschedule(&data->tap_detection.tap_timeout_work,
                       K_MSEC(config->tap_detection.tap_timout_ms));

     // Never block movement — let it pass through
     return 0;
 }

 int tap_detection_handle_touch(const struct device *dev, struct gesture_event_t *event) {
     struct gesture_config *config = (struct gesture_config *)dev->config;
     struct gesture_data *data = (struct gesture_data *)dev->data;

     if (!config->tap_detection.enabled) {
         return -1;
     }

     // If finger moves too far from start position, cancel tap candidacy
     if (data->tap_detection.is_waiting_for_tap) {
         int dx = (int)event->x - (int)data->tap_detection.touch_start_x;
         int dy = (int)event->y - (int)data->tap_detection.touch_start_y;
         int dist_sq = dx * dx + dy * dy;
         if (dist_sq > TAP_DISTANCE_THRESHOLD_SQ) {
             data->tap_detection.is_waiting_for_tap = false;
             k_work_cancel_delayable(&data->tap_detection.tap_timeout_work);
             LOG_DBG("tap cancelled - finger moved too far");
         }
     }

     // Never block movement
     return 0;
 }

 int tap_detection_handle_end(const struct device *dev) {
     struct gesture_config *config = (struct gesture_config *)dev->config;
     struct gesture_data *data = (struct gesture_data *)dev->data;

     if (!config->tap_detection.enabled) {
         return -1;
     }

     if (data->tap_detection.is_waiting_for_tap) {
         // Finger lifted within timeout and distance threshold — it's a tap
         data->tap_detection.is_waiting_for_tap = false;
         k_work_cancel_delayable(&data->tap_detection.tap_timeout_work);

         LOG_DBG("tap detected on lift-off - sending click");
         zmk_hid_mouse_button_press(0);
         zmk_endpoints_send_mouse_report();
         zmk_hid_mouse_button_release(0);
         zmk_endpoints_send_mouse_report();
     }

     return 0;
 }


 /* Timeout: finger held too long, cancel tap detection */
 static void tap_timeout_callback(struct k_work *work) {
     struct k_work_delayable *d_work = k_work_delayable_from_work(work);
     struct tap_detection_data *data = CONTAINER_OF(d_work, struct tap_detection_data, tap_timeout_work);
     data->is_waiting_for_tap = false;
     LOG_DBG("tap timeout expired - not a tap");
 }

 int tap_detection_init(const struct device *dev) {
     struct gesture_config *config = (struct gesture_config *)dev->config;
     struct gesture_data *data = (struct gesture_data *)dev->data;

     LOG_DBG("tap_detection: %s, timeout in ms: %d, prevent_movement_during_tap: %s", 
         config->tap_detection.enabled ? "yes" : "no", 
         config->tap_detection.tap_timout_ms,
         config->tap_detection.prevent_movement_during_tap ? "yes" : "no");

     if (!config->tap_detection.enabled) {
         return -1;
     }
     k_work_init_delayable(&data->tap_detection.tap_timeout_work, tap_timeout_callback);

     return 0;
 }
