// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <SDL3/SDL.h>
#include "common/logging/log.h"
#include "core/libraries/kernel/time_management.h"
#include "core/libraries/pad/pad.h"
#include "input/controller.h"

namespace Input {

GameController::GameController() {
    m_states_num = 0;
    m_last_state = State();
}

void GameController::ReadState(State* state, bool* isConnected, int* connectedCount) {
    std::scoped_lock lock{m_mutex};

    *isConnected = m_connected;
    *connectedCount = m_connected_count;
    *state = GetLastState();
}

int GameController::ReadStates(State* states, int states_num, bool* isConnected,
                               int* connectedCount) {
    std::scoped_lock lock{m_mutex};

    *isConnected = m_connected;
    *connectedCount = m_connected_count;

    int ret_num = 0;

    if (m_connected) {
        if (m_states_num == 0) {
            ret_num = 1;
            states[0] = m_last_state;
        } else {
            for (uint32_t i = 0; i < m_states_num; i++) {
                if (ret_num >= states_num) {
                    break;
                }
                auto index = (m_first_state + i) % MAX_STATES;
                if (!m_private[index].obtained) {
                    m_private[index].obtained = true;

                    states[ret_num++] = m_states[index];
                }
            }
        }
    }

    return ret_num;
}

State GameController::GetLastState() const {
    if (m_states_num == 0) {
        return m_last_state;
    }

    auto last = (m_first_state + m_states_num - 1) % MAX_STATES;

    return m_states[last];
}

void GameController::AddState(const State& state) {
    if (m_states_num >= MAX_STATES) {
        m_states_num = MAX_STATES - 1;
        m_first_state = (m_first_state + 1) % MAX_STATES;
    }

    auto index = (m_first_state + m_states_num) % MAX_STATES;

    m_states[index] = state;
    m_last_state = state;
    m_private[index].obtained = false;
    m_states_num++;
}

void GameController::CheckButton(int id, u32 button, bool isPressed) {
    std::scoped_lock lock{m_mutex};
    auto state = GetLastState();
    state.time = Libraries::Kernel::sceKernelGetProcessTime();
    if (isPressed) {
        state.buttonsState |= button;
    } else {
        state.buttonsState &= ~button;
    }

    AddState(state);
}

void GameController::Axis(int id, Input::Axis axis, int value) {
    std::scoped_lock lock{m_mutex};
    auto state = GetLastState();

    state.time = Libraries::Kernel::sceKernelGetProcessTime();

    int axis_id = static_cast<int>(axis);

    state.axes[axis_id] = value;

    // Derive digital buttons from the analog trigger
    // Scaled value is 0 .. 255
    // Rest point for L2/R2 is ideally 0 but may drift
    // Use some hysteresis to avoid glitches

    const int ON_THRESHOLD = 31;  // 255 / 8
    const int OFF_THRESHOLD = 16; // 255 / 16 + 1

    if (axis == Input::Axis::TriggerLeft) {
        LOG_TRACE(Input, "TriggerLeft {}", value);
        if (value > ON_THRESHOLD) {
            LOG_TRACE(Input, "L2 ON");
            state.buttonsState |= Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_L2;
        } else if (value < OFF_THRESHOLD) {
            LOG_TRACE(Input, "L2 OFF");
            state.buttonsState &= ~Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_L2;
        }
    }

    if (axis == Input::Axis::TriggerRight) {
        LOG_TRACE(Input, "TriggerRight {}", value);
        if (value > ON_THRESHOLD) {
            LOG_TRACE(Input, "R2 ON");
            state.buttonsState |= Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_R2;
        } else if (value < OFF_THRESHOLD) {
            LOG_TRACE(Input, "R2 OFF");
            state.buttonsState &= ~Libraries::Pad::OrbisPadButtonDataOffset::ORBIS_PAD_BUTTON_R2;
        }
    }

    AddState(state);
}

void GameController::SetLightBarRGB(u8 r, u8 g, u8 b) {
    if (m_sdl_gamepad != nullptr) {
        SDL_SetGamepadLED(m_sdl_gamepad, r, g, b);
    }
}

bool GameController::SetVibration(u8 smallMotor, u8 largeMotor) {
    if (m_sdl_gamepad != nullptr) {
        return SDL_RumbleGamepad(m_sdl_gamepad, (smallMotor / 255.0f) * 0xFFFF,
                                 (largeMotor / 255.0f) * 0xFFFF, -1) == 0;
    }
    return true;
}

void GameController::TryOpenSDLController() {
    if (m_sdl_gamepad == nullptr || !SDL_GamepadConnected(m_sdl_gamepad)) {
        int gamepad_count;
        SDL_JoystickID* gamepads = SDL_GetGamepads(&gamepad_count);
        m_sdl_gamepad = gamepad_count > 0 ? SDL_OpenGamepad(gamepads[0]) : nullptr;
        SDL_free(gamepads);
    }

    SetLightBarRGB(0, 0, 255);
}

} // namespace Input
