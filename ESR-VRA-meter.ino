#include <Arduino.h>
#include "config.h"
#include "ads1115.h"
#include "vra.h"

ADS1115 adc;
VRA_Analyzer vra;

// Measurement count
unsigned long measurement_count = 0;

void printSeparator() {
    Serial.println(F("========================================"));
}

void printBanner() {
    Serial.println();
    printSeparator();
    Serial.println(F("  ESR-VRA Meter v1.0"));
    Serial.println(F("  Voltage Relaxation Analysis"));
    Serial.println(F("  Single-pulse battery diagnostics"));
    printSeparator();
    Serial.println();
    Serial.print(F("  Shunt: "));     Serial.print(SHUNT_RESISTANCE, 2); Serial.println(F(" Ohm"));
    Serial.print(F("  Load:  "));     Serial.print(LOAD_RESISTANCE, 1); Serial.println(F(" Ohm"));
    Serial.print(F("  Pulse: "));     Serial.print(PULSE_DURATION_MS); Serial.println(F(" ms"));
    Serial.print(F("  Samples: "));   Serial.print(RELAX_SAMPLES); Serial.print(F(" x ")); Serial.print(RELAX_SAMPLE_STEP_MS); Serial.println(F(" ms"));
    Serial.println();
    printSeparator();
    Serial.println();
}

void printVoltageScale() {
    Serial.println(F("  Voltage Scale:"));
    Serial.println(F("  4.20V - 3.90V  = Fully charged"));
    Serial.println(F("  3.90V - 3.70V  = Nominal"));
    Serial.println(F("  3.70V - 3.40V  = Discharged"));
    Serial.println(F("  3.40V - 2.50V  = Deep discharge"));
    Serial.println();
}

void printResult(const VRA_Result &r, unsigned long num) {
    Serial.print(F("--- Measurement #")); Serial.print(num); Serial.println(F(" ---"));
    Serial.println();
    
    Serial.println(F("  [Voltage Data]"));
    Serial.print(F("  V_load     = ")); Serial.print(r.V_before, 4); Serial.println(F(" V  (under load)"));
    Serial.print(F("  V_instant  = ")); Serial.print(r.V_after, 4);  Serial.println(F(" V  (instant after)"));
    Serial.print(F("  V_final    = ")); Serial.print(r.V_final, 4);  Serial.println(F(" V  (relaxed)"));
    Serial.print(F("  ΔV_ohmic   = ")); Serial.print(r.V_after - r.V_before, 4); Serial.println(F(" V"));
    Serial.print(F("  ΔV_relax   = ")); Serial.print(r.V_relaxation, 4); Serial.println(F(" V"));
    Serial.println();
    
    Serial.println(F("  [Current]"));
    Serial.print(F("  I_load     = ")); Serial.print(r.I_load * 1000.0f, 1); Serial.println(F(" mA"));
    Serial.print(F("  R_load     = ")); 
    if (r.I_load > 0.001f) {
        Serial.print(r.V_before / r.I_load, 2); Serial.println(F(" Ohm"));
    } else {
        Serial.println(F("N/A"));
    }
    Serial.println();
    
    Serial.println(F("  [Resistance Analysis]"));
    Serial.print(F("  R_ohm      = ")); Serial.print(r.R_ohm * 1000.0f, 2); Serial.println(F(" mOhm  (AC-IR equivalent)"));
    Serial.print(F("  R_pol      = ")); Serial.print(r.R_pol * 1000.0f, 2); Serial.println(F(" mOhm  (polarization)"));
    Serial.print(F("  R_total    = ")); Serial.print((r.R_ohm + r.R_pol) * 1000.0f, 2); Serial.println(F(" mOhm  (total ESR)"));
    Serial.println();
    
    Serial.println(F("  [Relaxation Curve Quality]"));
    Serial.print(F("  R-squared  = ")); Serial.print(r.R_squared, 6);
    Serial.print(F("  → ")); Serial.println(VRA_Analyzer::getGradeString(r.soh_grade));
    Serial.println();
    
    char assessment[80];
    VRA_Analyzer::getAssessment(r, assessment, sizeof(assessment));
    Serial.print(F("  Assessment: ")); Serial.println(assessment);
    Serial.println();
    
    // Print relaxation data points for graphing
    Serial.println(F("  [Relaxation Data]"));
    Serial.println(F("  t(ms)   V(V)"));
    for (int i = 0; i < RELAX_SAMPLES; i++) {
        float t_ms = (float)((i + 1) * RELAX_SAMPLE_STEP_MS);
        Serial.print(F("  ")); 
        Serial.print(t_ms, 0);
        Serial.print(F("    "));
        Serial.println(vra.getVoltageSample(i), 4);
    }
    Serial.println();
    printSeparator();
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    while (!Serial) { ; }
    
    pinMode(MOSFET_PIN, OUTPUT);
    digitalWrite(MOSFET_PIN, HIGH); // Load OFF initially
    
    adc.begin();
    vra.begin();
    
    printBanner();
    printVoltageScale();
    
    Serial.println(F("  Ready. Send any character to start measurement."));
    Serial.println(F("  Send 'a' for auto-mode (continuous measurements)."));
    Serial.println();
}

bool auto_mode = false;

void loop() {
    // Check for serial commands
    if (Serial.available()) {
        char cmd = Serial.read();
        if (cmd == 'a' || cmd == 'A') {
            auto_mode = !auto_mode;
            Serial.print(F("Auto mode: ")); Serial.println(auto_mode ? F("ON") : F("OFF"));
        }
    }
    
    // Quick voltage check before measurement
    float v_check = adc.readVoltage();
    Serial.print(F("Battery: ")); Serial.print(v_check, 3); Serial.print(F("V ... "));
    
    if (v_check < BATTERY_MIN_V) {
        Serial.println(F("UNDERVOLTAGE - Connect battery!"));
        delay(2000);
        return;
    }
    if (v_check > BATTERY_MAX_V) {
        Serial.println(F("OVERVOLTAGE - Disconnect battery!"));
        delay(2000);
        return;
    }
    
    Serial.println(F("Measuring..."));
    
    VRA_Result result;
    if (vra.measure(result)) {
        measurement_count++;
        printResult(result, measurement_count);
    } else {
        Serial.println(F("ERROR: Measurement failed (voltage out of range during test)."));
    }
    
    if (auto_mode) {
        delay(1000); // 1 second between measurements in auto mode
    } else {
        Serial.println(F("Send any character for next measurement, 'a' for auto-mode."));
    }
}
