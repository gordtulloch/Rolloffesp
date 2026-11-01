#include "rolloffesp_roof_control.h"

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include "rolloffesp_hardware.h"

typedef enum {
    ROOF_MOTION_UNKNOWN,
    ROOF_MOTION_OPENING,
    ROOF_MOTION_CLOSING,
    ROOF_MOTION_STOPPED_TO_OPEN,
    ROOF_MOTION_STOPPED_TO_CLOSED,
} roof_motion_t;

static portMUX_TYPE s_roof_mux = portMUX_INITIALIZER_UNLOCKED;
static roof_motion_t s_motion = ROOF_MOTION_UNKNOWN;
static bool s_position_initialized;

rolloffesp_roof_status_t rolloffesp_roof_status(void)
{
    const rolloffesp_position_t position = rolloffesp_read_position();
    portENTER_CRITICAL(&s_roof_mux);
    if (position == ROLLOFFESP_POSITION_OPEN ||
        position == ROLLOFFESP_POSITION_CLOSED) {
        s_motion = ROOF_MOTION_UNKNOWN;
        s_position_initialized = true;
    } else if (position == ROLLOFFESP_POSITION_INVALID) {
        s_motion = ROOF_MOTION_UNKNOWN;
        s_position_initialized = false;
    }

    rolloffesp_roof_status_t status = ROLLOFFESP_ROOF_UNKNOWN;
    if (position == ROLLOFFESP_POSITION_OPEN) {
        status = ROLLOFFESP_ROOF_OPEN;
    } else if (position == ROLLOFFESP_POSITION_CLOSED) {
        status = ROLLOFFESP_ROOF_CLOSED;
    } else if (position == ROLLOFFESP_POSITION_INVALID) {
        status = ROLLOFFESP_ROOF_INVALID;
    } else if (s_motion == ROOF_MOTION_OPENING) {
        status = ROLLOFFESP_ROOF_OPENING;
    } else if (s_motion == ROOF_MOTION_CLOSING) {
        status = ROLLOFFESP_ROOF_CLOSING;
    }
    portEXIT_CRITICAL(&s_roof_mux);
    return status;
}

static esp_err_t start_motion(bool open)
{
    const rolloffesp_roof_status_t status = rolloffesp_roof_status();
    if (status == ROLLOFFESP_ROOF_INVALID) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((open && status == ROLLOFFESP_ROOF_OPEN) ||
        (!open && status == ROLLOFFESP_ROOF_CLOSED)) {
        return ESP_OK;
    }

    portENTER_CRITICAL(&s_roof_mux);
    const roof_motion_t current_motion = s_motion;
    const bool initialized = s_position_initialized;
    portEXIT_CRITICAL(&s_roof_mux);

    if (status == ROLLOFFESP_ROOF_UNKNOWN && !initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (current_motion == ROOF_MOTION_OPENING ||
        current_motion == ROOF_MOTION_CLOSING) {
        return (current_motion == ROOF_MOTION_OPENING) == open
            ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if (current_motion == ROOF_MOTION_STOPPED_TO_OPEN ||
        current_motion == ROOF_MOTION_STOPPED_TO_CLOSED) {
        const bool next_open = current_motion == ROOF_MOTION_STOPPED_TO_OPEN;
        if (next_open != open) {
            return ESP_ERR_INVALID_STATE;
        }
    } else if ((open && status != ROLLOFFESP_ROOF_CLOSED) ||
               (!open && status != ROLLOFFESP_ROOF_OPEN)) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t err = rolloffesp_relay_press_default();
    if (err != ESP_OK) {
        return err;
    }
    portENTER_CRITICAL(&s_roof_mux);
    s_motion = open ? ROOF_MOTION_OPENING : ROOF_MOTION_CLOSING;
    s_position_initialized = true;
    portEXIT_CRITICAL(&s_roof_mux);
    return ESP_OK;
}

esp_err_t rolloffesp_roof_open(void)
{
    return start_motion(true);
}

esp_err_t rolloffesp_roof_close(void)
{
    return start_motion(false);
}

esp_err_t rolloffesp_roof_abort(void)
{
    const rolloffesp_roof_status_t status = rolloffesp_roof_status();
    if (status == ROLLOFFESP_ROOF_OPEN || status == ROLLOFFESP_ROOF_CLOSED) {
        return ESP_OK;
    }

    portENTER_CRITICAL(&s_roof_mux);
    const roof_motion_t current_motion = s_motion;
    portEXIT_CRITICAL(&s_roof_mux);
    if (current_motion != ROOF_MOTION_OPENING &&
        current_motion != ROOF_MOTION_CLOSING) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t err = rolloffesp_relay_press_default();
    if (err != ESP_OK) {
        return err;
    }
    portENTER_CRITICAL(&s_roof_mux);
    s_motion = current_motion == ROOF_MOTION_OPENING
        ? ROOF_MOTION_STOPPED_TO_CLOSED
        : ROOF_MOTION_STOPPED_TO_OPEN;
    portEXIT_CRITICAL(&s_roof_mux);
    return ESP_OK;
}
