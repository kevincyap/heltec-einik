#pragma once

// Estimated single-cell Li-ion charge; -1 means no plausible voltage reading.
int battery_percent();

// Filtered ADC millivolts scaled by the divider ratio, without plausibility
// filtering for calibration. -1 means battery measurement is unsupported.
float battery_voltage_mv();
