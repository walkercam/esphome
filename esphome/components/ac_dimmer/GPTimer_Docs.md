This document introduces the features of the General Purpose Timer (GPTimer) driver in ESP-IDF. The table of contents is as follows:

Overview

Quick Start

Creating and Starting a Timer

Setting and Getting the Count Value

Triggering Periodic Alarm Events

Triggering One-Shot Alarm Events

Resource Recycling

Advanced Features

Dynamic Alarm Value Update

Power Management

Thread Safety

Cache Safety

Performance

Other Kconfig Options

Resource Consumption

Application Examples

API Reference

GPTimer Driver APIs

GPTimer Driver Types

GPTimer HAL Types

GPTimer ETM APIs

Overview
GPTimer is a dedicated driver for the ESP32 [Timer Group peripheral]. This timer can select different clock sources and prescalers to meet the requirements of nanosecond-level resolution. Additionally, it has flexible timeout alarm functions and allows automatic updating of the count value at the alarm moment, achieving very precise timing cycles.

Based on the high resolution, high count range, and high response capabilities of the hardware timer, the main application scenarios of this driver include:

Running freely as a calendar clock to provide timestamp services for other modules

Generating periodic alarms to complete periodic tasks

Generating one-shot alarms, which can be used to implement a monotonic software timer list with asynchronous updates of alarm values

Working with the GPIO module to achieve PWM signal output and input capture

etc.

Quick Start
This section provides a concise overview of how to use the GPTimer driver. Through practical examples, it demonstrates how to initialize and start a timer, configure alarm events, and register callback functions. The typical usage flow is as follows:


GPTimer driver's general usage flow (click to enlarge)

Creating and Starting a Timer
First, we need to create a timer instance. The following code shows how to create a timer with a resolution of 1 MHz:

gptimer_handle_t gptimer = NULL;
gptimer_config_t timer_config = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT, // Select the default clock source
    .direction = GPTIMER_COUNT_UP,      // Counting direction is up
    .resolution_hz = 1 * 1000 * 1000,   // Resolution is 1 MHz, i.e., 1 tick equals 1 microsecond
};
// Create a timer instance
ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));
// Enable the timer
ESP_ERROR_CHECK(gptimer_enable(gptimer));
// Start the timer
ESP_ERROR_CHECK(gptimer_start(gptimer));
When creating a timer instance, we need to configure parameters such as the clock source, counting direction, and resolution through gptimer_config_t. These parameters determine how the timer works. Then, call the gptimer_new_timer() function to create a new timer instance, which returns a handle pointing to the new instance. The timer handle is essentially a pointer to the timer memory object, of type gptimer_handle_t.

Here are the other configuration parameters of the gptimer_config_t structure and their explanations:

gptimer_config_t::clk_src selects the clock source for the timer. Available clock sources are listed in gptimer_clock_source_t, and only one can be selected. Different clock sources vary in resolution, accuracy, and power consumption.

gptimer_config_t::direction sets the counting direction of the timer. Supported directions are listed in gptimer_count_direction_t, and only one can be selected.

gptimer_config_t::resolution_hz sets the resolution of the internal counter. Each tick is equivalent to 1 / resolution_hz seconds.

gptimer_config_t::intr_priority sets the interrupt priority. If set to 0, a default priority interrupt will be allocated; otherwise, the specified priority will be used.

gptimer_config_t::flags is used to fine-tune some behaviors of the driver, including the following options:

gptimer_config_t::flags::allow_pd configures whether the driver allows the system to power down the peripheral in sleep mode. Before entering sleep, the system will back up the GPTimer register context, which will be restored when the system wakes up. Note that powering down the peripheral can save power but will consume more memory to save the register context. You need to balance power consumption and memory usage. This configuration option depends on specific hardware features. If enabled on an unsupported chip, you will see an error message like not able to power down in light sleep.

Note

Note that if all hardware timers in the current chip have been allocated, gptimer_new_timer() will return the ESP_ERR_NOT_FOUND error.

Before starting the timer, it must be enabled. The enable function gptimer_enable() can switch the internal state machine of the driver to the active state, which includes some system service requests/registrations, such as applying for a power management lock. The corresponding disable function is gptimer_disable(), which releases all system services.

Note

When calling the gptimer_enable() and gptimer_disable() functions, they need to be used in pairs. This means you cannot call gptimer_enable() or gptimer_disable() twice in a row. This pairing principle ensures the correct management and release of resources.

The gptimer_start() function is used to start the timer. After starting, the timer will begin counting and will automatically overflow and restart from 0 when it reaches the maximum or minimum value (depending on the counting direction). The gptimer_stop() function is used to stop the timer. Note that stopping a timer does not clear the current value of the counter. To clear the counter, use the gptimer_set_raw_count() function introduced later. The gptimer_start() and gptimer_stop() functions follow the idempotent principle. This means that if the timer is already started, calling the gptimer_start() function again will have no effect. Similarly, if the timer is already stopped, calling the gptimer_stop() function again will have no effect.

Note

However, note that when the timer is in the intermediate state of starting (the start has begun but not yet completed), if another thread calls the gptimer_start() or gptimer_stop() function, it will return the ESP_ERR_INVALID_STATE error to avoid triggering uncertain behavior.

Setting and Getting the Count Value
When a timer is newly created, its internal counter value defaults to zero. You can set other count values using the gptimer_set_raw_count() function. The maximum count value depends on the bit width of the hardware timer (usually no less than 54 bits).

Note

If the timer is already running, gptimer_set_raw_count() will make the timer immediately jump to the new value and start counting from the newly set value.

The gptimer_get_raw_count() function is used to get the current count value of the timer. This count value is the accumulated count since the timer started (assuming it started from 0). Note that the returned value has not been converted to any unit; it is a pure count value. You need to convert the count value to time units based on the actual resolution of the timer. The timer's resolution can be obtained using the gptimer_get_resolution() function.

// Check the timer's resolution
uint32_t resolution_hz;
ESP_ERROR_CHECK(gptimer_get_resolution(gptimer, &resolution_hz));
// Read the current count value
uint64_t count;
ESP_ERROR_CHECK(gptimer_get_raw_count(gptimer, &count));
// (Optional) Convert the count value to time units (seconds)
double time = (double)count / resolution_hz;
Triggering Periodic Alarm Events
In addition to the timestamp function, the general-purpose timer also supports alarm functions. The following code shows how to set a periodic alarm that triggers once per second:

gptimer_handle_t gptimer = NULL;
gptimer_config_t timer_config = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT, // Select the default clock source
    .direction = GPTIMER_COUNT_UP,      // Counting direction is up
    .resolution_hz = 1 * 1000 * 1000,   // Resolution is 1 MHz, i.e., 1 tick equals 1 microsecond
};
// Create a timer instance
ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

static bool example_timer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    // General process for handling event callbacks:
    // 1. Retrieve user context data from user_ctx (passed in from gptimer_register_event_callbacks)
    // 2. Get alarm event data from edata, such as edata->count_value
    // 3. Perform user-defined operations
    // 4. Return whether a high-priority task was awakened during the above operations to notify the scheduler to switch tasks
    return false;
}

gptimer_alarm_config_t alarm_config = {
    .reload_count = 0,      // When the alarm event occurs, the timer will automatically reload to 0
    .alarm_count = 1000000, // Set the actual alarm period, since the resolution is 1us, 1000000 represents 1s
    .flags.auto_reload_on_alarm = true, // Enable auto-reload function
};
// Set the timer's alarm action
ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

gptimer_event_callbacks_t cbs = {
    .on_alarm = example_timer_on_alarm_cb, // Call the user callback function when the alarm event occurs
};
// Register timer event callback functions, allowing user context to be carried
ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));
// Enable the timer
ESP_ERROR_CHECK(gptimer_enable(gptimer));
// Start the timer
ESP_ERROR_CHECK(gptimer_start(gptimer));
The gptimer_set_alarm_action() function is used to configure the timer's alarm action. When the timer count value reaches the specified alarm value, an alarm event will be triggered. Users can choose to automatically reload the preset count value when the alarm event occurs, thereby achieving periodic alarms.

Here are the necessary members of the gptimer_alarm_config_t structure and their functions. By configuring these parameters, users can flexibly control the timer's alarm behavior to meet different application needs.

gptimer_alarm_config_t::alarm_count sets the target count value that triggers the alarm event. When the timer count value reaches this value, an alarm event will be triggered. When setting the alarm value, consider the counting direction of the timer. If the current count value has exceeded the alarm value, the alarm event will be triggered immediately.

gptimer_alarm_config_t::reload_count sets the count value to be reloaded when the alarm event occurs. This configuration only takes effect when the gptimer_alarm_config_t::flags::auto_reload_on_alarm flag is true. The actual alarm period will be determined by |alarm_count - reload_count|. From a practical application perspective, it is not recommended to set the alarm period to less than 5us.

Note

Specifically, gptimer_set_alarm_action(gptimer, NULL); means disabling the timer's alarm function.

The gptimer_register_event_callbacks() function is used to register the timer event callback functions. When the timer triggers a specific event (such as an alarm event), the user-defined callback function will be called. Users can perform custom operations in the callback function, such as sending signals, to achieve more flexible event handling mechanisms. Since the callback function is executed in the interrupt context, avoid performing complex operations (including any operations that may cause blocking) in the callback function to avoid affecting the system's real-time performance. The gptimer_register_event_callbacks() function also allows users to pass a context pointer to access user-defined data in the callback function.

The supported event callback functions for GPTimer are as follows:

gptimer_alarm_cb_t alarm event callback function, which has a corresponding data structure gptimer_alarm_event_data_t for passing alarm event-related data: - gptimer_alarm_event_data_t::alarm_value stores the alarm value, which is the target count value that triggers the alarm event. - gptimer_alarm_event_data_t::count_value stores the count value when entering the interrupt handler after the alarm occurs. This value may differ from the alarm value due to interrupt handler delays, and the count value may have been automatically reloaded when the alarm occurred.

Note

Be sure to register the callback function before calling gptimer_enable(), otherwise the timer event will not correctly trigger the interrupt service.

Triggering One-Shot Alarm Events
Some application scenarios only require triggering a one-shot alarm interrupt. The following code shows how to set a one-shot alarm that triggers after 1 second:

gptimer_handle_t gptimer = NULL;
gptimer_config_t timer_config = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT, // Select the default clock source
    .direction = GPTIMER_COUNT_UP,      // Counting direction is up
    .resolution_hz = 1 * 1000 * 1000,   // Resolution is 1 MHz, i.e., 1 tick equals 1 microsecond
};
// Create a timer instance
ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

static bool example_timer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    // This is just a demonstration of how to stop the timer when the alarm occurs for the first time
    gptimer_stop(timer);
    // General process for handling event callbacks:
    // 1. Retrieve user context data from user_ctx (passed in from gptimer_register_event_callbacks)
    // 2. Get alarm event data from edata, such as edata->count_value
    // 3. Perform user-defined operations
    // 4. Return whether a high-priority task was awakened during the above operations to notify the scheduler to switch tasks
    return false;
}

gptimer_alarm_config_t alarm_config = {
    .alarm_count = 1000000, // Set the actual alarm period, since the resolution is 1us, 1000000 represents 1s
    .flags.auto_reload_on_alarm = false; // Disable auto-reload function
};
// Set the timer's alarm action
ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

gptimer_event_callbacks_t cbs = {
    .on_alarm = example_timer_on_alarm_cb, // Call the user callback function when the alarm event occurs
};
// Register timer event callback functions, allowing user context to be carried
ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));
// Enable the timer
ESP_ERROR_CHECK(gptimer_enable(gptimer));
// Start the timer
ESP_ERROR_CHECK(gptimer_start(gptimer));
Unlike periodic alarms, the above code disables the auto-reload function when configuring the alarm behavior. This means that after the alarm event occurs, the timer will not automatically reload to the preset count value but will continue counting until it overflows. If you want the timer to stop immediately after the alarm, you can call gptimer_stop() in the callback function.

Resource Recycling
When the timer is no longer needed, you should call the gptimer_delete_timer() function to release software and hardware resources. Before deleting, ensure that the timer is already stopped.

Advanced Features
After understanding the basic usage, we can further explore more features of the GPTimer driver.

Dynamic Alarm Value Update
The GPTimer driver supports dynamically updating the alarm value in the interrupt callback function by calling the gptimer_set_alarm_action() function, thereby implementing a monotonic software timer list. The following code shows how to reset the next alarm trigger time when the alarm event occurs:

gptimer_handle_t gptimer = NULL;
gptimer_config_t timer_config = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT, // Select the default clock source
    .direction = GPTIMER_COUNT_UP,      // Counting direction is up
    .resolution_hz = 1 * 1000 * 1000,   // Resolution is 1 MHz, i.e., 1 tick equals 1 microsecond
};
// Create a timer instance
ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

static bool example_timer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = edata->alarm_value + 1000000, // Next alarm in 1s from the current alarm
    };
    // Update the alarm value
    gptimer_set_alarm_action(timer, &alarm_config);
    return false;
}

gptimer_alarm_config_t alarm_config = {
    .alarm_count = 1000000, // Set the actual alarm period, since the resolution is 1us, 1000000 represents 1s
    .flags.auto_reload_on_alarm = false, // Disable auto-reload function
};
// Set the timer's alarm action
ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

gptimer_event_callbacks_t cbs = {
    .on_alarm = example_timer_on_alarm_cb, // Call the user callback function when the alarm event occurs
};
// Register timer event callback functions, allowing user context to be carried
ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));
// Enable the timer
ESP_ERROR_CHECK(gptimer_enable(gptimer));
// Start the timer
ESP_ERROR_CHECK(gptimer_start(gptimer));
Power Management
When power management CONFIG_PM_ENABLE is enabled, the system may adjust or disable the clock source before entering sleep mode, causing the GPTimer to lose accuracy.

To prevent this, the GPTimer driver creates a power management lock internally. When the gptimer_enable() function is called, the lock is activated to ensure the system does not enter sleep mode, thus maintaining the timer's accuracy. To reduce power consumption, you can call the gptimer_disable() function to release the power management lock, allowing the system to enter sleep mode. However, this will stop the timer, so you need to restart the timer after waking up.

Thread Safety
The driver uses critical sections to ensure atomic operations on registers. Key members in the driver handle are also protected by critical sections. The driver's internal state machine uses atomic instructions to ensure thread safety, with state checks preventing certain invalid concurrent operations (e.g., conflicts between start and stop). Therefore, GPTimer driver APIs can be used in a multi-threaded environment without extra locking.

The following functions can also be used in an interrupt context:

gptimer_start()

gptimer_stop()

gptimer_get_raw_count()

gptimer_set_raw_count()

gptimer_get_captured_count()

gptimer_set_alarm_action()

Cache Safety
When the file system performs Flash read/write operations, the system temporarily disables the Cache function to avoid errors when loading instructions and data from Flash. This causes the GPTimer interrupt handler to be unresponsive during this period, preventing the user callback function from executing in time. If you want the interrupt handler to run normally when the Cache is disabled, you can enable the CONFIG_GPTIMER_ISR_CACHE_SAFE option.

Note

Note that when this option is enabled, all interrupt callback functions and their context data must be placed in internal storage. This is because the system cannot load data and instructions from Flash when the Cache is disabled.

Performance
To improve the real-time responsiveness of interrupt handling, the GPTimer driver provides the CONFIG_GPTIMER_ISR_HANDLER_IN_IRAM option. Once enabled, the interrupt handler is placed in internal RAM, reducing delays caused by potential cache misses when loading instructions from Flash.

Note

However, the user callback function and its context data called by the interrupt handler may still reside in Flash. Cache misses are still possible, so users must manually place the callback function and data in internal RAM, for example by using IRAM_ATTR and DRAM_ATTR.

As mentioned above, the GPTimer driver allows some functions to be called in an interrupt context. By enabling the CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM option, these functions can also be placed in IRAM, which helps avoid performance loss caused by cache misses and allows them to be used when the Cache is disabled.

Other Kconfig Options
The CONFIG_GPTIMER_ENABLE_DEBUG_LOG option forces the GPTimer driver to enable all debug logs, regardless of the global log level settings. Enabling this option helps developers obtain more detailed log information during debugging, making it easier to locate and solve problems.

Resource Consumption
Use the IDF Size tool to check the code and data consumption of the GPTimer driver. The following are the test conditions (using ESP32-C2 as an example):

Compiler optimization level set to -Os to ensure minimal code size.

Default log level set to ESP_LOG_INFO to balance debug information and performance.

Disable the following driver optimization options:
CONFIG_GPTIMER_ISR_HANDLER_IN_IRAM - Do not place the interrupt handler in IRAM.

CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM - Do not place control functions in IRAM.

CONFIG_GPTIMER_ISR_CACHE_SAFE - Do not enable Cache safety options.

Note that the following data are not exact values and are for reference only; they may differ on different chip models.

Component Layer

Total Size

DIRAM

.bss

.data

.text

Flash Code

.text

Flash Data

.rodata

soc

8

0

0

0

0

0

0

8

8

hal

206

0

0

0

0

206

206

0

0

driver

4251

12

12

0

0

4046

4046

193

193

Additionally, each GPTimer handle dynamically allocates about 100 bytes of memory from the heap. If the gptimer_config_t::flags::allow_pd option is enabled, each timer will also consume approximately 30 extra bytes of memory during sleep to store the register context.

Application Examples
peripherals/timer_group/gptimer demonstrates how to use the general-purpose timer APIs on ESP SOC chips to generate periodic alarm events and trigger different alarm actions.

peripherals/timer_group/wiegand_interface uses two timers (one in one-shot alarm mode and the other in periodic alarm mode) to trigger interrupts and change the GPIO output state in the alarm event callback function, simulating the output waveform of the Wiegand protocol.

API Reference
GPTimer Driver APIs
Header File
components/esp_driver_gptimer/include/driver/gptimer.h

This header file can be included with:

#include "driver/gptimer.h"
This header file is a part of the API provided by the esp_driver_gptimer component. To declare that your component depends on esp_driver_gptimer, add the following to your CMakeLists.txt:

REQUIRES esp_driver_gptimer
or

PRIV_REQUIRES esp_driver_gptimer
Functions
esp_err_t gptimer_new_timer(const gptimer_config_t *config, gptimer_handle_t *ret_timer)
Create a new General Purpose Timer, and return the handle.

Note

The newly created timer is put in the "init" state.

Parameters
:
config -- [in] GPTimer configuration

ret_timer -- [out] Returned timer handle

Returns
:
ESP_OK: Create GPTimer successfully

ESP_ERR_INVALID_ARG: Create GPTimer failed because of invalid argument

ESP_ERR_NO_MEM: Create GPTimer failed because out of memory

ESP_ERR_NOT_FOUND: Create GPTimer failed because all hardware timers are used up and no more free one

ESP_FAIL: Create GPTimer failed because of other error

esp_err_t gptimer_del_timer(gptimer_handle_t timer)
Delete the GPTimer handle.

Note

A timer must be in the "init" state before it can be deleted.

Parameters
:
timer -- [in] Timer handle created by gptimer_new_timer

Returns
:
ESP_OK: Delete GPTimer successfully

ESP_ERR_INVALID_ARG: Delete GPTimer failed because of invalid argument

ESP_ERR_INVALID_STATE: Delete GPTimer failed because the timer is not in init state

ESP_FAIL: Delete GPTimer failed because of other error

esp_err_t gptimer_set_raw_count(gptimer_handle_t timer, uint64_t value)
Set GPTimer raw count value.

Note

When updating the raw count of an active timer, the timer will immediately start counting from the new value.

Note

This function is allowed to run within ISR context

Note

If CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM is enabled, this function will be placed in the IRAM by linker, makes it possible to execute even when the Flash Cache is disabled.

Parameters
:
timer -- [in] Timer handle created by gptimer_new_timer

value -- [in] Count value to be set

Returns
:
ESP_OK: Set GPTimer raw count value successfully

ESP_ERR_INVALID_ARG: Set GPTimer raw count value failed because of invalid argument

ESP_FAIL: Set GPTimer raw count value failed because of other error

esp_err_t gptimer_get_raw_count(gptimer_handle_t timer, uint64_t *value)
Get GPTimer raw count value.

Note

This function will trigger a software capture event and then return the captured count value.

Note

With the raw count value and the resolution returned from gptimer_get_resolution, you can convert the count value into seconds.

Note

This function is allowed to run within ISR context

Note

If CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM is enabled, this function will be placed in the IRAM by linker, makes it possible to execute even when the Flash Cache is disabled.

Parameters
:
timer -- [in] Timer handle created by gptimer_new_timer

value -- [out] Returned GPTimer count value

Returns
:
ESP_OK: Get GPTimer raw count value successfully

ESP_ERR_INVALID_ARG: Get GPTimer raw count value failed because of invalid argument

ESP_FAIL: Get GPTimer raw count value failed because of other error

esp_err_t gptimer_get_resolution(gptimer_handle_t timer, uint32_t *out_resolution)
Return the real resolution of the timer.

Note

usually the timer resolution is same as what you configured in the gptimer_config_t::resolution_hz, but some unstable clock source (e.g. RC_FAST) will do a calibration, the real resolution can be different from the configured one.

Parameters
:
timer -- [in] Timer handle created by gptimer_new_timer

out_resolution -- [out] Returned timer resolution, in Hz

Returns
:
ESP_OK: Get GPTimer resolution successfully

ESP_ERR_INVALID_ARG: Get GPTimer resolution failed because of invalid argument

ESP_FAIL: Get GPTimer resolution failed because of other error

esp_err_t gptimer_get_captured_count(gptimer_handle_t timer, uint64_t *value)
Get GPTimer captured count value.

Note

Different from gptimer_get_raw_count, this function won't trigger a software capture event. It just returns the last captured count value. It's especially useful when the capture has already been triggered by an external event and you want to read the captured value.

Note

This function is allowed to run within ISR context

Note

If CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM is enabled, this function will be placed in the IRAM by linker, makes it possible to execute even when the Flash Cache is disabled.

Parameters
:
timer -- [in] Timer handle created by gptimer_new_timer

value -- [out] Returned captured count value

Returns
:
ESP_OK: Get GPTimer captured count value successfully

ESP_ERR_INVALID_ARG: Get GPTimer captured count value failed because of invalid argument

ESP_FAIL: Get GPTimer captured count value failed because of other error

esp_err_t gptimer_register_event_callbacks(gptimer_handle_t timer, const gptimer_event_callbacks_t *cbs, void *user_data)
Set callbacks for GPTimer.

Note

User registered callbacks are expected to be runnable within ISR context

Note

The first call to this function needs to be before the call to gptimer_enable

Note

User can deregister a previously registered callback by calling this function and setting the callback member in the cbs structure to NULL.

Parameters
:
timer -- [in] Timer handle created by gptimer_new_timer

cbs -- [in] Group of callback functions

user_data -- [in] User data, which will be passed to callback functions directly

Returns
:
ESP_OK: Set event callbacks successfully

ESP_ERR_INVALID_ARG: Set event callbacks failed because of invalid argument

ESP_ERR_INVALID_STATE: Set event callbacks failed because the timer is not in init state

ESP_FAIL: Set event callbacks failed because of other error

esp_err_t gptimer_set_alarm_action(gptimer_handle_t timer, const gptimer_alarm_config_t *config)
Set alarm event actions for GPTimer.

Note

This function is allowed to run within ISR context, so you can update new alarm action immediately in any ISR callback.

Note

If CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM is enabled, this function will be placed in the IRAM by linker, makes it possible to execute even when the Flash Cache is disabled. In this case, please also ensure the gptimer_alarm_config_t instance is placed in the static data section instead of in the read-only data section. e.g.: static gptimer_alarm_config_t alarm_config = { ... };

Parameters
:
timer -- [in] Timer handle created by gptimer_new_timer

config -- [in] Alarm configuration, especially, set config to NULL means disabling the alarm function

Returns
:
ESP_OK: Set alarm action for GPTimer successfully

ESP_ERR_INVALID_ARG: Set alarm action for GPTimer failed because of invalid argument

ESP_FAIL: Set alarm action for GPTimer failed because of other error

esp_err_t gptimer_enable(gptimer_handle_t timer)
Enable GPTimer.

Note

This function will transit the timer state from "init" to "enable".

Note

This function will enable the interrupt service, if it's lazy installed in gptimer_register_event_callbacks.

Note

This function will acquire a PM lock, if a specific source clock (e.g. APB) is selected in the gptimer_config_t, while CONFIG_PM_ENABLE is enabled.

Note

Enable a timer doesn't mean to start it. See also gptimer_start for how to make the timer start counting.

Parameters
:
timer -- [in] Timer handle created by gptimer_new_timer

Returns
:
ESP_OK: Enable GPTimer successfully

ESP_ERR_INVALID_ARG: Enable GPTimer failed because of invalid argument

ESP_ERR_INVALID_STATE: Enable GPTimer failed because the timer is already enabled

ESP_FAIL: Enable GPTimer failed because of other error

esp_err_t gptimer_disable(gptimer_handle_t timer)
Disable GPTimer.

Note

This function will transit the timer state from "enable" to "init".

Note

This function will disable the interrupt service if it's installed.

Note

This function will release the PM lock if it's acquired in the gptimer_enable.

Note

Disable a timer doesn't mean to stop it. See also gptimer_stop for how to make the timer stop counting.

Parameters
:
timer -- [in] Timer handle created by gptimer_new_timer

Returns
:
ESP_OK: Disable GPTimer successfully

ESP_ERR_INVALID_ARG: Disable GPTimer failed because of invalid argument

ESP_ERR_INVALID_STATE: Disable GPTimer failed because the timer is not enabled yet

ESP_FAIL: Disable GPTimer failed because of other error

esp_err_t gptimer_start(gptimer_handle_t timer)
Start GPTimer (internal counter starts counting)

Note

This function will transit the timer state from "enable" to "run".

Note

This function is allowed to run within ISR context

Note

If CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM is enabled, this function will be placed in the IRAM by linker, makes it possible to execute even when the Flash Cache is disabled.

Parameters
:
timer -- [in] Timer handle created by gptimer_new_timer

Returns
:
ESP_OK: Start GPTimer successfully

ESP_ERR_INVALID_ARG: Start GPTimer failed because of invalid argument

ESP_ERR_INVALID_STATE: Start GPTimer failed because the timer is not enabled or is already in running

ESP_FAIL: Start GPTimer failed because of other error

esp_err_t gptimer_stop(gptimer_handle_t timer)
Stop GPTimer (internal counter stops counting)

Note

This function will transit the timer state from "run" to "enable".

Note

This function is allowed to run within ISR context

Note

If CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM is enabled, this function will be placed in the IRAM by linker, makes it possible to execute even when the Flash Cache is disabled.

Parameters
:
timer -- [in] Timer handle created by gptimer_new_timer

Returns
:
ESP_OK: Stop GPTimer successfully

ESP_ERR_INVALID_ARG: Stop GPTimer failed because of invalid argument

ESP_ERR_INVALID_STATE: Stop GPTimer failed because the timer is not in running.

ESP_FAIL: Stop GPTimer failed because of other error

Structures
struct gptimer_config_t
General Purpose Timer configuration.

Public Members

gptimer_clock_source_t clk_src
GPTimer clock source

gptimer_count_direction_t direction
Count direction

uint32_t resolution_hz
Counter resolution (working frequency) in Hz, hence, the step size of each count tick equals to (1 / resolution_hz) seconds

int intr_priority
GPTimer interrupt priority, if set to 0, the driver will try to allocate an interrupt with a relative low priority (1,2,3)

uint32_t intr_shared
Set true, the timer interrupt number can be shared with other peripherals

uint32_t allow_pd
If set, driver allows the power domain to be powered off when system enters sleep mode. This can save power, but at the expense of more RAM being consumed to save register context.

struct gptimer_config_t flags
GPTimer config flags

struct gptimer_event_callbacks_t
Group of supported GPTimer callbacks.

Note

The callbacks are all running under ISR environment

Note

When CONFIG_GPTIMER_ISR_CACHE_SAFE is enabled, the callback itself and functions called by it should be placed in IRAM.

Public Members

gptimer_alarm_cb_t on_alarm
Timer alarm callback

struct gptimer_alarm_config_t
General Purpose Timer alarm configuration.

Public Members

uint64_t alarm_count
Alarm target count value

uint64_t reload_count
Alarm reload count value, effect only when auto_reload_on_alarm is set to true

uint32_t auto_reload_on_alarm
Reload the count value by hardware, immediately at the alarm event

struct gptimer_alarm_config_t flags
Alarm config flags

GPTimer Driver Types
Header File
components/esp_driver_gptimer/include/driver/gptimer_types.h

This header file can be included with:

#include "driver/gptimer_types.h"
This header file is a part of the API provided by the esp_driver_gptimer component. To declare that your component depends on esp_driver_gptimer, add the following to your CMakeLists.txt:

REQUIRES esp_driver_gptimer
or

PRIV_REQUIRES esp_driver_gptimer
Structures
struct gptimer_alarm_event_data_t
GPTimer alarm event data.

Public Members

uint64_t count_value
Current count value

uint64_t alarm_value
Current alarm value

Type Definitions
typedef struct gptimer_t *gptimer_handle_t
Type of General Purpose Timer handle.

typedef bool (*gptimer_alarm_cb_t)(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
Timer alarm callback prototype.

Param timer
:
[in] Timer handle created by gptimer_new_timer

Param edata
:
[in] Alarm event data, fed by driver

Param user_ctx
:
[in] User data, passed from gptimer_register_event_callbacks

Return
:
Whether a high priority task has been waken up by this function

GPTimer HAL Types
Header File
components/esp_hal_timg/include/hal/timer_types.h

This header file can be included with:

#include "hal/timer_types.h"
This header file is a part of the API provided by the esp_hal_timg component. To declare that your component depends on esp_hal_timg, add the following to your CMakeLists.txt:

REQUIRES esp_hal_timg
or

PRIV_REQUIRES esp_hal_timg
Type Definitions
typedef soc_periph_gptimer_clk_src_t gptimer_clock_source_t
GPTimer clock source.

Note

User should select the clock source based on the power and resolution requirement

Enumerations
enum gptimer_count_direction_t
GPTimer count direction.

Values:

enumerator GPTIMER_COUNT_DOWN
Decrease count value

enumerator GPTIMER_COUNT_UP
Increase count value