
#include "math.h"
#include "test.h"


#define PI 3.1415926
#define c3_frequency 130.81
#define c4_frequency 261.63

typedef struct _Channels {
    uint8_t channel1;
    uint8_t channel2;
} Channels;

// The supported audio codec in ESP32 A2DP is SBC. SBC audio stream is encoded
// from PCM data normally formatted as 44.1kHz sampling rate, two-channel 16-bit
// sample data
int32_t get_data(uint8_t* data, int32_t len) {
    if (len < 0 || data == NULL) {
        return 0;
    }
    Channels* ch_data = (Channels*)data;
    static double m_time = 0.0;
    double m_amplitude = 10000;  // max -32,768 to 32,767
    double m_deltaTime = 1.0 / 44100;
    double m_phase = 0.0;
    double double_Pi = PI * 2.0;
    for (int sample = 0; sample < len / 4; ++sample) {
        ch_data[sample].channel1 =
            m_amplitude * sin(double_Pi * c3_frequency * m_time + m_phase);
        ch_data[sample].channel2 =
            m_amplitude * sin(double_Pi * c4_frequency * m_time + m_phase);
        m_time += m_deltaTime;
    }
    return len;
}