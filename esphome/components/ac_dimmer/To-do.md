We need to make some changes to this ESP Home ac_dimmer component to add some debug funcionality to troubleshoot visible flickering when using this component at any dimming value other than 100% or 0%.

The dimmer being tested always operates in trailing edge mode so we only need to add debug logging in the areas that are relevant to trailing edge control.

The goal is to add a ring buffer of debug records (256 records) each of which will store the timing information for an AC half-cycle (so 100 per second for 50Hz AC). Every second we will write/output these to the logger interface from a ESP Home loop method. Therefore, we will add and then clear 100 entries to the ring buffer each second. We will be writing to the debug records in the ring buffer from the ISRs that service the zero crossing pin change interrupt and the timer interrupt that then turns the MOSFET gate on and off so this code must not be blocking or time consuming. Writing to the logger interface can then happen in the loop method for this component (needs to be added).

For each debug record we want to record the zero crossing timestap, the zero crossing period, the requested gate on and off time, and the actual gate on and off time. We want to comma seperate the variables so the data can be later analysed in Excel. The format should be "ACDIM_DEBUG,zc_timestamp,zc_period_us,requested_on_us,actual_on_us,requested_off_us,actual_off_us". 

ac_dimmer.h
    Add a DebugEvent structure.
    Include:
        zc_timestamp
        zc_period_us
        requested_on_us
        actual_on_us
        requested_off_us
        actual_off_us
    Add a fixed-size ring buffer of 256 records.
    Add ISR-safe producer/consumer indexes.
    Add ESPHome loop() to AcDimmer.

gpio_intr()
    When the ZC occurs, capture the ZC timestamp.
    Create the next debug record and fill in the:
        ZC timestamp
        ZC period
        requested enable and disable times
    Do nothing involving logging or string formatting.

timer_intr()
    At the existing enable and disable condition:
        Associate time_since_zc with the current debug record.
        Perform the existing digital_writes.
    Don't log anything.

loop()
    Run in normal ESPHome task context.
    Every second, drain completed debug records.
    Output them as CSV-style log lines as described above