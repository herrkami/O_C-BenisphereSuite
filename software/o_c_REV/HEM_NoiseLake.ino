// Copyright (c) 2022, Korbinian Schreiber
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "tiny_dsp.h"

#define BNC_MAX_PARAM 63

#define NOISE_MODE_WHITE    0
#define NOISE_MODE_BITFLIP  1
#define NOISE_MODE_CRACKLE  2
// #define NOISE_VALUE_MAX     4095
#define NOISE_VALUE_MAX     2047

#define FILTER_MODE_OFF     0
#define FILTER_MODE_LP      1
#define FILTER_MODE_BP      2
#define FILTER_MODE_HP      3
#define FILTER_MODE_NOTCH   4

struct FilterState {
    uint8_t f;
    uint8_t q;
    uint8_t mode;
};

class NoiseLake : public HemisphereApplet {
public:

    const char* applet_name() {
        return "NoiseLake";
    }

    void Start() {
        noise = random(0, NOISE_VALUE_MAX);
        noise_mode = NOISE_MODE_BITFLIP;
        CLKSetCFreq(FSAMPLE_CHZ);

        ForEachChannel(ch) {
            state_filter[ch].f = 32;
            state_filter[ch].q = 32;
        }
        state_filter[0].mode = FILTER_MODE_LP;
        state_filter[1].mode = FILTER_MODE_BP;
    }

    void Controller() {
        int16_t signal[2];

        if (noise_mode == NOISE_MODE_CRACKLE) {
            // Stochastic rate
            uint16_t eta = random(0, CLK_PHI_MAX);
            if (eta < clk_dphi) {
                int16_t a = clk_dphi / eta;
                a = a > 8 ? 8 : a;
                a *= random(2) > 0 ? 1 : -1;
                noise = a*NOISE_VALUE_MAX/8;
            } else {
                // Exponential decay
                noise = (noise*3)/4;
            }
        } else {
            // Noise clocking
            clk_phi += clk_dphi;
            if (clk_phi >= CLK_PHI_MAX) {
                // Wrap phase
                clk_phi -= CLK_PHI_MAX;
                if (noise_mode == NOISE_MODE_BITFLIP) {
                    noise += NOISE_VALUE_MAX;
                    noise ^= (1 << random(0, 12));
                    noise -= NOISE_VALUE_MAX;
                } else {
                    noise = random(-NOISE_VALUE_MAX, NOISE_VALUE_MAX);
                }
            }
        }

        ForEachChannel(ch) {
            // Freeze on postive gate
            if (!Gate(ch)) _noise[ch] = noise;

            int32_t freq = Parameter2Frequency(state_filter[ch].f);
            freq *= 100;
            int32_t q = Proportion(state_filter[ch].q, BNC_MAX_PARAM, 2020);

            filter[ch].feed(_noise[ch], freq, q);
            switch (state_filter[ch].mode) {
                case FILTER_MODE_OFF:
                    signal[ch] = _noise[ch];
                break;
                case FILTER_MODE_LP:
                    signal[ch] = filter[ch].get_lp();
                break;
                case FILTER_MODE_BP:
                    signal[ch] = filter[ch].get_bp();
                break;
                case FILTER_MODE_HP:
                    signal[ch] = filter[ch].get_hp();
                break;
                case FILTER_MODE_NOTCH:
                    signal[ch] = filter[ch].get_no();
                break;
            }
            Out(ch, signal[ch]/2);
        }
    }

    void View() {
        gfxHeader(applet_name());
        DrawInterface();
    }

    void OnButtonPress() {
        if (++cursor > 8) cursor = 0;
    }

    void OnEncoderMove(int direction) {
        switch (cursor) {
            case 0:
                state_clock = constrain(state_clock + direction, 0, BNC_MAX_PARAM);
                CLKSetCFreq(Parameter2Clk(state_clock)*100);
            break;
            case 1:
                noise_mode = constrain(noise_mode + direction, 0, 2);
            break;
            case 2:
                state_filter[0].mode = constrain(state_filter[0].mode + direction, 0, 4);
            break;
            case 3:
                state_filter[1].mode = constrain(state_filter[1].mode + direction, 0, 4);
            break;
            case 4:
                state_filter[0].f = constrain(state_filter[0].f + direction, 0, BNC_MAX_PARAM);
            break;
            case 5:
                state_filter[1].f = constrain(state_filter[1].f + direction, 0, BNC_MAX_PARAM);
            break;
            case 6:
                state_filter[0].q = constrain(state_filter[0].q + direction, 0, BNC_MAX_PARAM);
            break;
            case 7:
                state_filter[1].q = constrain(state_filter[1].q + direction, 0, BNC_MAX_PARAM);
            break;
        }
    }

    uint32_t OnDataRequest() {
        uint32_t data = 0;
        // example: pack property_name at bit 0, with size of 8 bits
        // Pack(data, PackLocation {0,8}, property_name);
        return data;
    }

    void OnDataReceive(uint32_t data) {
        // example: unpack value at bit 0 with size of 8 bits to property_name
        // property_name = Unpack(data, PackLocation {0,8});
    }

protected:
    void SetHelp() {
        //                               "------------------" <-- Size Guide
        help[HEMISPHERE_HELP_DIGITALS] = "Digital in help";
        help[HEMISPHERE_HELP_CVS]      = "CV in help";
        help[HEMISPHERE_HELP_OUTS]     = "Out help";
        help[HEMISPHERE_HELP_ENCODER]  = "123456789012345678";
        //                               "------------------" <-- Size Guide
    }

private:
    int cursor;

    uint8_t noise_mode;
    const char *NOISE_MODE_NAMES[3] = {"  white",
                                       "bitflip",
                                       "crackle"};
    const char *FILTER_MODE_NAMES[5] = {"off", " lp", " bp", " hp", "ntc"};

    const uint16_t FREQ_SCALE[64] = {
        50,    55,    60,    66,    72,    79,    87,    95,   105,
       115,   126,   138,   151,   166,   182,   199,   219,   240,
       263,   288,   316,   347,   380,   417,   457,   501,   550,
       603,   661,   725,   795,   872,   956,  1048,  1150,  1261,
      1382,  1516,  1662,  1823,  1999,  2192,  2404,  2636,  2891,
      3170,  3476,  3812,  4180,  4584,  5026,  5512,  6044,  6628,
      7269,  7971,  8741,  9585, 10511, 11526, 12639, 13860, 15199,
     16667};

    TDSP::FilterStateVariable filter[2];

    // States and CVs
    FilterState state_filter[2];
    uint8_t state_clock = 63;

    const uint32_t FSAMPLE_CHZ = 1666667;
    const uint16_t FSAMPLE_HZ = 16667;
    const uint32_t CLK_PHI_MAX = 0xffff;
    uint64_t clk_phi = 0;
    uint64_t clk_dphi;

    // Signals
    int16_t noise;
    int16_t _noise[2];

    // Functions
    void DrawInterface() {
        // Clock
        if (cursor == 0) {
            gfxPrint(1, 15, ">");
        } else {
            gfxPrint(1, 15, "//");
        }
        uint16_t clk = Parameter2Clk(state_clock);
        if (clk/10000) {
            gfxPrint(23, 15, clk);
        } else if (clk/1000) {
            gfxPrint(29, 15, clk);
        } else {
            gfxPrint(35, 15, clk);
        }
        gfxIcon(54, 14, HERTZ_ICON);

        // Noise type
        if (cursor == 1) gfxPrint(1, 25, ">");
        gfxPrint(20, 25, NOISE_MODE_NAMES[noise_mode]);

        // Filter
        ForEachChannel(ch) {
            // Filter mode
            if (cursor == 2 + ch) gfxPrint(1 + 32*ch, 35, ">");

            // Q
            if (cursor == 6 + ch) gfxPrint(1 + 32*ch, 35, "Q");

            // Filter icon
            if (state_filter[ch].mode == FILTER_MODE_LP) {
                DrawFilterLP(14 + 32*ch, 35, state_filter[ch].q);
            } else if (state_filter[ch].mode == FILTER_MODE_HP) {
                DrawFilterHP(14 + 32*ch, 35, state_filter[ch].q);
            } else if (state_filter[ch].mode == FILTER_MODE_BP) {
                DrawFilterBP(14 + 32*ch, 35, state_filter[ch].q);
            } else if (state_filter[ch].mode == FILTER_MODE_NOTCH) {
                DrawFilterNotch(14 + 32*ch, 35, state_filter[ch].q);
            } else {
                gfxPrint(13 + 32*ch, 35, FILTER_MODE_NAMES[state_filter[ch].mode]);
            }

            // Filter frequency
            if (cursor == 4 + ch) gfxLine(1 + 32*ch, 46, 1 + 32*ch, 50);
            uint16_t f = Parameter2Frequency(state_filter[ch].f);
            if (f/100) {
                gfxPrint(3 + 32*ch, 45, f);
            } else {
                gfxPrint(9 + 32*ch, 45, f);
            }
            gfxIcon(22 + 32*ch, 44, HERTZ_ICON);
        }

        // CV type
    }

    void DrawFilterLP(byte x, byte y, uint8_t q) {
        // expects q = 0 .. 63
        // width = 16
        q += 1;  // 1 .. 64
        q /= 16; // 0 .. 4
        y += 7;
        byte yc = y - 4;
        gfxLine(x,      yc,         x + 6,  yc);
        gfxLine(x + 6,  yc,         x + 9,  yc - q + 1);
        gfxLine(x + 9,  yc - q + 1, x + 12, yc - q + 2);
        gfxLine(x + 12, yc - q + 2, x + 15, y);
    }

    void DrawFilterHP(byte x, byte y, uint8_t q) {
        // expects q = 0 .. 63
        // width = 16
        q += 1;  // 1 .. 64
        q /= 16; // 0 .. 4
        y += 7;
        byte yc = y - 4;
        gfxLine(15 + x, yc,         9 + x,  yc);
        gfxLine(9 + x,  yc,         6 + x,  yc - q + 1);
        gfxLine(6 + x,  yc - q + 1, 3 + x,  yc - q + 2);
        gfxLine(3 + x,  yc - q + 2, 0 + x,  y);
    }

    void DrawFilterBP(byte x, byte y, uint8_t q) {
        // expects q = 0 .. 63
        // width = 16
        q += 1;  // 1 .. 64
        q /= 16; // 0 .. 4
        y += 7;
        byte yc = y - 4;
        gfxLine(x,      y,          x + 5,  y);
        gfxLine(x + 5,  y,          x + 7,  yc - q + 1);
        gfxLine(x + 7,  yc - q + 1, x + 9, yc - q + 1);
        gfxLine(x + 9,  yc - q + 1, x + 10, y);
        gfxLine(x + 10, y,          x + 15, y);
    }

    void DrawFilterNotch(byte x, byte y, uint8_t q) {
        // expects q = 0 .. 63
        // width = 16
        q += 1;  // 1 .. 64
        q /= 16; // 0 .. 4
        y += 7;
        byte yc = y - 4;
        gfxLine(x,      yc,         x + 5,  yc);
        gfxLine(x + 5,  yc,         x + 7,  yc + q);
        gfxLine(x + 7,  yc + q,     x + 9,  yc + q);
        gfxLine(x + 9,  yc + q,     x + 10, yc);
        gfxLine(x + 10, yc,         x + 15, yc);
    }

    int16_t WaveFolderSimple(int16_t signal, int16_t limit) {
        signal = signal > limit ? 2*limit - signal : signal;
        signal = signal < -limit ? -2*limit - signal : signal;
        return signal;
    }

    int16_t WaveFolder(int16_t signal, int16_t limit) {
        signal += limit;
        int16_t out = signal%(2*limit);
        out = out < 0 ? -out : out;
        if (!((signal/(2*limit))%2)) { // odd
            out = 2*limit - out;
        }
        out -= limit;
        return out;
    }

    void CLKSetCFreq(uint64_t cfreq) {
        // cfreq in cHz
        clk_dphi = (cfreq*CLK_PHI_MAX)/FSAMPLE_CHZ;
    }

    inline uint16_t Parameter2Frequency(uint8_t p) {
        return Proportion(p, BNC_MAX_PARAM, 969) + 30;
    }

    inline uint16_t Parameter2Clk(uint8_t p) {
        p = p < 64 ? p : 63;
        return FREQ_SCALE[p];
    }
};


////////////////////////////////////////////////////////////////////////////////
//// Hemisphere Applet Functions
///
///  Once you run the find-and-replace to make these refer to NoiseLake,
///  it's usually not necessary to do anything with these functions. You
///  should prefer to handle things in the HemisphereApplet child class
///  above.
////////////////////////////////////////////////////////////////////////////////
NoiseLake NoiseLake_instance[2];

void NoiseLake_Start(bool hemisphere) {NoiseLake_instance[hemisphere].BaseStart(hemisphere);}
void NoiseLake_Controller(bool hemisphere, bool forwarding) {NoiseLake_instance[hemisphere].BaseController(forwarding);}
void NoiseLake_View(bool hemisphere) {NoiseLake_instance[hemisphere].BaseView();}
void NoiseLake_OnButtonPress(bool hemisphere) {NoiseLake_instance[hemisphere].OnButtonPress();}
void NoiseLake_OnEncoderMove(bool hemisphere, int direction) {NoiseLake_instance[hemisphere].OnEncoderMove(direction);}
void NoiseLake_ToggleHelpScreen(bool hemisphere) {NoiseLake_instance[hemisphere].HelpScreen();}
uint32_t NoiseLake_OnDataRequest(bool hemisphere) {return NoiseLake_instance[hemisphere].OnDataRequest();}
void NoiseLake_OnDataReceive(bool hemisphere, uint32_t data) {NoiseLake_instance[hemisphere].OnDataReceive(data);}
