#ifndef __ARDUIN_TSUNAMI_H
#define __ARDUINO_TSUNAMI_H

#include <Arduino.h>
#include <EEPROM.h>
#include <SPI.h>
extern "C" {
  #include <ad983x/ad983x.h>
  #include <mcp49xx/mcp49xx.h>
}

#define TSUNAMI_DDS_CS        8
#define TSUNAMI_DDS_FSEL      5
#define TSUNAMI_DDS_PSEL      6
#define TSUNAMI_DDS_SLEEP     9
#define TSUNAMI_DDS_RESET     11
#define TSUNAMI_AUX           10
#define TSUNAMI_PHASE         A1
#define TSUNAMI_PEAK          A5
#define TSUNAMI_VIN           A4
#define TSUNAMI_AUX_FILTER    A0
#define TSUNAMI_SIGN_EN       A2
#define TSUNAMI_VAVG          A3
#define TSUNAMI_FREQIN_1      4
#define TSUNAMI_FREQIN_2      12
#define TSUNAMI_DAC_CS        7
#define TSUNAMI_FDIV_SEL_0    31
#define TSUNAMI_FDIV_SEL_1    30
#define TSUNAMI_OFFSET_ID     1
#define TSUNAMI_AMPLITUDE_ID  0
#define TSUNAMI_FREQUENCY     16 // MHz

#define TSUNAMI_DAC_BITS     12
#define TSUNAMI_DAC_RANGE    (1 << TSUNAMI_DAC_BITS)
#define TSUNAMI_OFFSET_FS    4074 // Fullscale voltage offset in millivolts
#define TSUNAMI_AMPLITUDE_FS 6606 // Fullscale amplitude in millivolts

#define TSUNAMI_ADC_BITS     10
#define TSUNAMI_ADC_RANGE    (1 << TSUNAMI_ADC_BITS)
#define TSUNAMI_VIN_RANGE    3037 // Fullscale voltage offset in millivolts
#define TSUNAMI_VIN_SCALING  ((((int32_t)TSUNAMI_VIN_RANGE) << 17) / TSUNAMI_ADC_RANGE)

#define TSUNAMI_CALIBRATION  0xDa7aDa7a

enum OutputMode {
  OUTPUT_MODE_SINE        = AD983X_OUTPUT_MODE_SINE,
  OUTPUT_MODE_TRIANGLE    = AD983X_OUTPUT_MODE_TRIANGLE,
};

enum CalibratedValue {
  CAL_DATA_OFFSET         = 0,
  CAL_DATA_AMPLITUDE      = 1,
  CAL_DATA_MEAN_VALUE     = 2,
  CAL_DATA_PEAK_VALUE     = 3,
  CAL_DATA_CURRENT_VALUE  = 4,
  CAL_DATA_ALL            = 5,
  CAL_DATA_NONE           = 6,
};

typedef struct {
  float     scale;
  float     shift;
} calibration_data_t ;

typedef struct {
  uint32_t  magic;
  float     scale;
  float     shift;
} calibration_record_t ;

extern calibration_data_t cal_data[CAL_DATA_ALL];

class Tsunami_Class {
public:
  Tsunami_Class();
  void begin();

  /* Applies and saves calibration data for a single value; "scale" does not
   * have a unit and "shift" is in whatever unit the value has (millivolts).
   */
  uint8_t setCalibrationData(CalibratedValue value, float scale, float shift);

  /* Returns the saved calibration data for a single value; "scale" does not
   * have a unit and "shift" is in whatever unit the value has (millivolts);
   * Note: this is the stored calibration data, not the current data in use!
   */
  uint8_t getCalibrationData(CalibratedValue value, float *scale, float *shift);

  /* Applies saved calibration data for either a specific value, all values,
   * or none of them: restores scale 1.0 and shift 0.0 but keeps saved data.
   */
  uint8_t useCalibrationData(CalibratedValue value);

  /* Set the output waveform to sine or triangle
   */
  inline void setOutputMode(OutputMode out) {
    ad983x_set_output_mode(&dds, (ad983x_output_mode_t)out);
  }

  // These overloads automatically set the unused frequency register, then
  // switch to it.

  /* Set the output frequency, in Hz.
   */
  inline void setFrequency(long int frequency) {
    current_frequency_reg = 1 - current_frequency_reg;
    setFrequency(current_frequency_reg, frequency);
    selectFrequency(current_frequency_reg);
  }

  /* Set the output frequency, in Hz.
   */
  inline void setFrequency(double frequency) {
    current_frequency_reg = 1 - current_frequency_reg;
    setFrequency(current_frequency_reg, (float)frequency);
    selectFrequency(current_frequency_reg);
  }

  /* Set the output frequency, in Hz.
   */
  inline void setFrequency(float frequency) {
    current_frequency_reg = 1 - current_frequency_reg;
    setFrequency(current_frequency_reg, frequency);
    selectFrequency(current_frequency_reg);
  }

  /* Set the output frequency on a given register, in Hz.
   */
  inline void setFrequency(byte reg, long int frequency)  {
    setFrequencyWord(reg, computeFrequencyWord(frequency));
  }

  /* Set the output frequency on a given register, in Hz.
   */
  inline void setFrequency(byte reg, double frequency) {
    setFrequency(reg, (float)frequency);
  }

  /* Set the output frequency on a given register, in Hz.
   */
  inline void setFrequency(byte reg, float frequency) {
    setFrequencyWord(reg, (frequency * (1l << 28)) / (TSUNAMI_FREQUENCY * 1000000));
  }

  // TODO: Provide setPhase methods.

  /* Enables or disables the DDS's reset mode.
   * Reset sets phase accumulator registers to 0, setting the output to
   * midscale and resetting the starting phase.
   */
  inline void reset(boolean in_reset) {
    digitalWrite(TSUNAMI_DDS_RESET, in_reset);
  }

  /* Enables or disables the DDS's sleep mode.
   * Sleep disables the DDS's DAC. The DDS keeps counting, and a square wave is
   * still output to the AUX port if enabled with auxSignOutput().
   */
  inline void sleep(boolean sleeping) {
    digitalWrite(TSUNAMI_DDS_SLEEP, sleeping);
  }

  /* Selects which frequency register (0 or 1) is used to control the DDS.
   */
  inline void selectFrequency(byte reg) {
    digitalWrite(TSUNAMI_DDS_FSEL, reg);
  }

  /* Selects which phase register (0 or 1) is used to control the DDS.
   */
  inline void selectPhase(byte reg) {
    digitalWrite(TSUNAMI_DDS_PSEL, reg);
  }

  /* Sets signal offset in millivolts.
   * When the DDS is disabled (sleep and reset are true), this function can be
   * used to generate an output waveform directly, albeit at a very low sample
   * rate.
   */
  inline void setOffset(int millivolts) {
    float scale = cal_data[CAL_DATA_OFFSET].scale;
    float shift = cal_data[CAL_DATA_OFFSET].shift;
    int32_t value = (float)(millivolts * scale) + shift;
    value += TSUNAMI_OFFSET_FS;
    value *= TSUNAMI_DAC_RANGE;
    value /= TSUNAMI_OFFSET_FS * 2;
    if(value < 0)
      value = 0;
    if(value >= TSUNAMI_DAC_RANGE)
      value = TSUNAMI_DAC_RANGE - 1;

    mcp49xx_write(&dac, TSUNAMI_OFFSET_ID, value);
  }

  /*
   * Sets signal amplitude in millivolts.
   */
  inline void setAmplitude(int millivolts) {
    float scale = cal_data[CAL_DATA_AMPLITUDE].scale;
    float shift = cal_data[CAL_DATA_AMPLITUDE].shift;
    int32_t value = (float)(millivolts * scale) + shift;
    value *= TSUNAMI_DAC_RANGE;
    value /= TSUNAMI_AMPLITUDE_FS;
    if(value < 0)
      value = 0;
    if(value >= TSUNAMI_DAC_RANGE)
      value = TSUNAMI_DAC_RANGE - 1;

    mcp49xx_write(&dac, TSUNAMI_AMPLITUDE_ID, TSUNAMI_DAC_RANGE - value - 1);
  }

  /* Measures peak to peak amplitude, returning a value in millivolts. Note that a
   * decrease in the amplitude will take some time to show up on the output, as
   * charge leaks from the storage cap. For a more accurate instantaneous
   * reading, set the TSUNAMI_PEAK pin to output, bring it low briefly, then
   * return it to input and wait a while for the capacitor to charge.
   */
  inline int16_t measurePeakVoltage() {
    // TODO: Discharge cap, delay, read.
    float scale = cal_data[CAL_DATA_PEAK_VALUE].scale;
    float shift = cal_data[CAL_DATA_PEAK_VALUE].shift;
    int32_t value = analogRead(TSUNAMI_PEAK);
    value *= TSUNAMI_VIN_SCALING;
    value >>= 16;
    value -= TSUNAMI_VIN_RANGE;
    value = (float)(value * scale) + shift;
    return (int16_t)value;
  }

  /* Measures mean voltage, returning a value in millivolts.
   */
  inline int16_t measureMeanVoltage() {
    float scale = cal_data[CAL_DATA_MEAN_VALUE].scale;
    float shift = cal_data[CAL_DATA_MEAN_VALUE].shift;
    int32_t value = analogRead(TSUNAMI_VAVG);
    value *= TSUNAMI_VIN_SCALING;
    value >>= 16;
    value -= TSUNAMI_VIN_RANGE;
    value = (float)(value * scale) + shift;
    return (int16_t)value;
  }

  /* Measures instantaneous voltage, returning a value in millivolts.
   */
  inline int16_t measureCurrentVoltage() {
    float scale = cal_data[CAL_DATA_CURRENT_VALUE].scale;
    float shift = cal_data[CAL_DATA_CURRENT_VALUE].shift;
    int32_t value = analogRead(TSUNAMI_VIN);
    value *= TSUNAMI_VIN_SCALING;
    value >>= 16;
    value -= TSUNAMI_VIN_RANGE;
    value = (float)(value * scale) + shift;
    return (int16_t)value;
  }

  /* Measures frequency, returning a value in Hz.
   * This works from approximately 1.0Hz upwards.
   *
   * Return values are accurate, but will suffer some jitter due to the analog
   * nature of the input signal. Measuring the square wave output will give a more
   * precise result than measuring the sine wave output.
   */
  float measureFrequency();
  
  /* Same as above, but with a moving average ratio of "1/8 new data" applied.
   * Ordinarily it tracks to the instant value rather quickly but whenever the
   * divided signal is a really low frequency the reaction time is observable.
   */
  float measureAverageFrequency();

  /* Measures phase offset, returning a figure between 0 and 1. 0 indicates the
   * signals are 180 degrees out of phase, while 1 indicates the signals are
   * exactly in phase. Note that the analog signal chain imposes some delay,
   * meaning that at higher frequencies there is a significant phase shift that
   * must be accounted for to get accurate measurements; this function does not
   * make any adjustment for that.
   */
  inline float measurePhase() {
    // TODO: Provide for frequency calibration

    int vphase;

    // Configure ADC to use Vcc power rail reference
    setAnalogRef(DEFAULT);

    vphase = analogRead(TSUNAMI_PHASE);

    // Configure ADC to use internal 2.56V reference
    setAnalogRef(INTERNAL);

    return (float)vphase / 1024;
  }

  /* Configures whether or not the DDS sign signal is output on the AUX port.
   */
  inline void enableSignOutput() {
    pinMode(TSUNAMI_AUX, INPUT);
    digitalWrite(TSUNAMI_AUX, LOW);
    digitalWrite(TSUNAMI_SIGN_EN, HIGH);
  }

  inline void disableSignOutput() {
    digitalWrite(TSUNAMI_SIGN_EN, LOW);
  }

  /* Enable the RC filter on the AUX output.
   * By disabling auxSignOutput, you can use analogWrite to output either a PWM
   * signal (with auxFiltering(false)) or a rectified voltage
   * (with auxFiltering(true)). This can be useful, for instance, to generate
   * parameter sweeps and graph them on an external tool like an oscilloscope.
   */
  inline void enableAuxiliaryFiltering() {
    pinMode(TSUNAMI_AUX_FILTER, OUTPUT);
    digitalWrite(TSUNAMI_AUX_FILTER, LOW);
  }

  inline void disableAuxiliaryFiltering() {
    pinMode(TSUNAMI_AUX_FILTER, INPUT);
    digitalWrite(TSUNAMI_AUX_FILTER, LOW);
  }

  // Handle to the underlying DAC.
  mcp49xx_t dac;

  // Handle to the underlying DDS.
  ad983x_t dds;

  // The frequency register currently being used
  uint8_t current_frequency_reg;

  // The phase register currently being used
  uint8_t current_phase_reg;

private:
  /* Set the raw frequency control word used by the DDS
   */
  inline void setFrequencyWord(byte reg, uint32_t frequency) {
    ad983x_set_frequency(&dds, reg, frequency);
  }

  /* Set the raw phase control word used by the DDS
   */
  inline void setPhaseWord(byte reg, uint32_t phase) {
    ad983x_set_phase(&dds, reg, phase);
  }

  /* Select a reference voltage compared to which analog reads get measured
   */
  inline void setAnalogRef(uint8_t source) {
    // Let the system know we changed the reference so it won't override us
    analogReference(source);

    // Switch to the selected Vref with the REFS bits in the ADMUX register
    if(source == INTERNAL) {
      ADMUX |= _BV(REFS0);
      ADMUX |= _BV(REFS1);
    } else {
      ADMUX |= _BV(REFS0);
      ADMUX &= ~_BV(REFS1);
    }

    // Apparently NOT waiting for the Vref capacitor to settle is NOT smart
    delayMicroseconds(3000);
  }

  inline uint32_t computeFrequencyWord(uint32_t frequency) {
    // This is a manual expansion of (frequency * 2^28) / m_frequency_mhz
    // Since it doesn't require 64 bit multiplies or divides, it results in
    // substantially smaller code sizes.
    uint32_t lval = ((frequency & 0xFF) << 22) / (15625l * TSUNAMI_FREQUENCY);
    uint32_t mval = ((frequency & 0xFF00) << 14) / (15625l * TSUNAMI_FREQUENCY);
    uint32_t hval = ((frequency & 0xFF0000) << 6) / (15625l * TSUNAMI_FREQUENCY);
    return (hval << 16) + (mval << 8) + lval;
  }
};

extern Tsunami_Class Tsunami;

#endif
