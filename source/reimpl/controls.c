/*
 * Copyright (C) 2025 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/controls.h"

#include <math.h>
#include <psp2/ctrl.h>
#include <psp2/motion.h>
#include <psp2/touch.h>
#include <psp2/kernel/clib.h>

#define LEFT_ANALOG_DEADZONE  0.16f
#define RIGHT_ANALOG_DEADZONE 0.16f
#ifndef GUNBROS_INPUT_TRACE
#define GUNBROS_INPUT_TRACE 0
#endif


void coord_normalize(float * x, float * y, float deadzone) {
    float magnitude = sqrtf((*x * *x) + (*y * *y));
    if (magnitude <= deadzone || magnitude == 0.0f) {
        *x = 0;
        *y = 0;
        return;
    }

    // normalize
    *x = *x / magnitude;
    *y = *y / magnitude;

    float multiplier = ((fminf(magnitude, 1.0f) - deadzone) / (1 - deadzone));
    *x = *x * multiplier;
    *y = *y * multiplier;
}

void controls_init() {
    // Enable analog sticks and touchscreen
    sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, 1);

    // Enable accelerometer
    sceMotionStartSampling();
}

void poll_touch();
void poll_pad();
void poll_accel();

void poll_stick(ControlsStickId which, float raw_x, float raw_y, float * readings_x, float * readings_y, float deadzone);

void controls_poll() {
    poll_touch();
    poll_pad();
    //poll_accel();
}

SceTouchData touch;
SceTouchData touch_old;

void poll_touch() {
    static int touch_poll_log_count = 0;

    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) <= 0) {
        return;
    }

    if (GUNBROS_INPUT_TRACE &&
        (touch.reportNum > 0 || touch_old.reportNum > 0) &&
        touch_poll_log_count < 48) {
        sceClibPrintf("[INPUT-RAW] touch reports=%u old=%u\n",
                      (unsigned int)touch.reportNum,
                      (unsigned int)touch_old.reportNum);
        touch_poll_log_count++;
    }

    for (int i = 0; i < touch.reportNum; i++) {
        float x = (float) touch.report[i].x * 960.f / 1920.0f;
        float y = (float) touch.report[i].y * 544.f / 1088.0f;

        // Check if the finger was down before to distinguish between the Move and Down events
        int finger_down = 0;
        int previous_index = -1;

        if (touch_old.reportNum > 0) {
            for (int j = 0; j < touch_old.reportNum; j++) {
                if (touch.report[i].id == touch_old.report[j].id) {
                    finger_down = 1;
                    previous_index = j;
                    break;
                }
            }
        }

        if (!finger_down) {
            controls_handler_touch(touch.report[i].id, x, y, CONTROLS_ACTION_DOWN);
        } else {
            int dx = (int)touch.report[i].x -
                     (int)touch_old.report[previous_index].x;
            int dy = (int)touch.report[i].y -
                     (int)touch_old.report[previous_index].y;

            /* sceTouchPeek reports every held finger on every poll. Feeding
             * an Android MOVE for an unchanged contact can put dozens of
             * redundant events in front of the UP that actually activates a
             * menu button. Ignore only raw-coordinate jitter; real drags and
             * every DOWN/UP remain lossless. */
            if (dx >= 2 || dx <= -2 || dy >= 2 || dy <= -2) {
                controls_handler_touch(touch.report[i].id, x, y,
                                       CONTROLS_ACTION_MOVE);
            }
        }
    }

    for (int i = 0; i < touch_old.reportNum; i++) {
        int finger_up = 1;

        for (int j = 0; j < touch.reportNum; j++) {
            if (touch.report[j].id == touch_old.report[i].id ) {
                finger_up = 0;
                break;
            }
        }

        if (finger_up == 1) {
            float x = (float) touch_old.report[i].x * 960.f / 1920.0f;
            float y = (float) touch_old.report[i].y * 544.f / 1088.0f;

            controls_handler_touch(touch_old.report[i].id, x, y, CONTROLS_ACTION_UP);
        }
    }

    sceClibMemcpy(&touch_old, &touch, sizeof(touch));
}

static ButtonMapping mapping[] = {
        { SCE_CTRL_UP,        AKEYCODE_DPAD_UP },
        { SCE_CTRL_DOWN,      AKEYCODE_DPAD_DOWN },
        { SCE_CTRL_LEFT,      AKEYCODE_DPAD_LEFT },
        { SCE_CTRL_RIGHT,     AKEYCODE_DPAD_RIGHT },
        { SCE_CTRL_CROSS,     AKEYCODE_DPAD_CENTER },
        { SCE_CTRL_CIRCLE,    AKEYCODE_BACK },
        { SCE_CTRL_SQUARE,    AKEYCODE_BUTTON_X },
        { SCE_CTRL_TRIANGLE,  AKEYCODE_BUTTON_Y },
        { SCE_CTRL_L1,        AKEYCODE_BUTTON_L1 },
        { SCE_CTRL_R1,        AKEYCODE_BUTTON_R1 },
        { SCE_CTRL_START,     AKEYCODE_BUTTON_START },
        { SCE_CTRL_SELECT,    AKEYCODE_BUTTON_SELECT },
};

uint32_t old_buttons = 0, current_buttons = 0, pressed_buttons = 0, released_buttons = 0;

float analog_lx[3] = { 0 };
float analog_ly[3] = { 0 };
float analog_rx[3] = { 0 };
float analog_ry[3] = { 0 };

void poll_pad() {
    SceCtrlData pad;
    int sample_count = sceCtrlPeekBufferPositiveExt2(0, &pad, 1);

    /* Peek returning no sample is not a neutral controller state. Preserve
     * the last valid stick lifecycle and wait for the next sample; injecting
     * two releases here caused held move+fire input to pulse and stutter. */
    if (sample_count <= 0) {
        return;
    }

    // Gamepad buttons
    old_buttons = current_buttons;
    current_buttons = pad.buttons;
    pressed_buttons = current_buttons & ~old_buttons;
    released_buttons = ~current_buttons & old_buttons;

    for (int i = 0; i < sizeof(mapping) / sizeof(ButtonMapping); i++) {
        if (pressed_buttons & mapping[i].sce_button) {
            controls_handler_key(mapping[i].android_button, CONTROLS_ACTION_DOWN);
        }
        if (released_buttons & mapping[i].sce_button) {
            controls_handler_key(mapping[i].android_button, CONTROLS_ACTION_UP);
        }
    }

    // Analog sticks
    poll_stick(CONTROLS_STICK_LEFT, (float)pad.lx, (float)pad.ly, analog_lx, analog_ly, LEFT_ANALOG_DEADZONE);
    poll_stick(CONTROLS_STICK_RIGHT, (float)pad.rx, (float)pad.ry, analog_rx, analog_ry, RIGHT_ANALOG_DEADZONE);
}

void poll_stick(ControlsStickId which, float raw_x, float raw_y, float * readings_x, float * readings_y, float deadzone) {
    int was_active;
    int is_active;

    readings_x[0] = (raw_x - 128.0f) / 128.0f;
    readings_y[0] = (raw_y - 128.0f) / 128.0f;

    coord_normalize(&readings_x[0], &readings_y[0], deadzone);

    /* A native ControlStick is a touch-state machine: every active run must
     * have exactly one DOWN and one UP.  The old three-sample debounce kept a
     * synthetic touch alive for two neutral frames.  A quick release/reverse
     * could therefore reuse the old floating origin and leave the player
     * walking in one direction, or send MOVE to a stick that had already
     * reset during a menu/level transition.  The Vita analog dead zone is
     * already the debounce boundary, so emit lifecycle edges immediately. */
    was_active = readings_x[1] != 0.0f || readings_y[1] != 0.0f;
    is_active = readings_x[0] != 0.0f || readings_y[0] != 0.0f;

    if (is_active) {
        controls_handler_analog(which, readings_x[0], readings_y[0],
                                was_active ? CONTROLS_ACTION_MOVE :
                                             CONTROLS_ACTION_DOWN);
    } else if (was_active) {
        /* Emit one lifecycle edge. CInputPad's owner-side watchdog verifies
         * that this exact synthetic ID was cleared after native UpdateInput. */
        controls_handler_analog(which, 0.0f, 0.0f, CONTROLS_ACTION_UP);
    }

    readings_x[2] = readings_x[1];
    readings_y[2] = readings_y[1];
    readings_x[1] = readings_x[0];
    readings_y[1] = readings_y[0];
}
