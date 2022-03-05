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

#define NL_MAX_PARAM 111

#define NOISE_MODE_WHITE    0
#define NOISE_MODE_BITFLIP  1
#define NOISE_MODE_CRACKLE  2
#define NOISE_MODE_LINEIN   3
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

        state_filter.f = 32;
        state_filter.q = 32;
        state_filter.mode = FILTER_MODE_LP;

        state_wavefolder = 1;
    }

    void Controller() {
        int16_t signal[2];

        // Noise and signal generation
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
                } else if (noise_mode == NOISE_MODE_LINEIN) {
                    noise = Proportion(In(0), HEMISPHERE_MAX_CV, NOISE_VALUE_MAX);
                } else {
                    noise = random(-NOISE_VALUE_MAX, NOISE_VALUE_MAX);
                }
            }
        }

        // Freeze on postive gate
        int32_t freq = Parameter2Frequency(state_filter.f);
        int32_t q = Proportion(state_filter.q, NL_MAX_PARAM, 2020);

        // Filter
        filter.feed(noise, freq, q);
        switch (state_filter.mode) {
            case FILTER_MODE_OFF:
                signal[0] = noise;
            break;
            case FILTER_MODE_LP:
                signal[0] = filter.get_lp();
            break;
            case FILTER_MODE_BP:
                signal[0] = filter.get_bp();
            break;
            case FILTER_MODE_HP:
                signal[0] = filter.get_hp();
            break;
            case FILTER_MODE_NOTCH:
                signal[0] = filter.get_no();
            break;
        }

        // WaveFolder
        signal[1] = 4*InterpolationFolder(
            Proportion(state_wavefolder, NL_MAX_PARAM/4, signal[0]));

        ForEachChannel(ch) Out(ch, signal[ch]/2);
    }

    void View() {
        gfxHeader(applet_name());
        DrawInterface();
    }

    void OnButtonPress() {
        if (++cursor > 5) cursor = 0;
    }

    void OnEncoderMove(int direction) {
        switch (cursor) {
            case 0:
                state_clock = constrain(state_clock + direction, 0, NL_MAX_PARAM);
                CLKSetCFreq(Parameter2Clk(state_clock));
            break;
            case 1:
                noise_mode = constrain(noise_mode + direction, 0, 3);
            break;
            case 2:
                state_filter.mode = constrain(state_filter.mode + direction, 0, 4);
            break;
            case 3:
                state_filter.f = constrain(state_filter.f + direction, 0, CFREQ_SCALE_IDX_C7);
            break;
            case 4:
                state_filter.q = constrain(state_filter.q + direction, 0, NL_MAX_PARAM);
            break;
            case 5:
                state_wavefolder = constrain(state_wavefolder + direction, 1, NL_MAX_PARAM);
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
    const char *NOISE_MODE_NAMES[4] = {"  white",
                                       "bitflip",
                                       "crackle",
                                       "line in"};
    const char *FILTER_MODE_NAMES[5] = {"off", " lp", " bp", " hp", "ntc"};

    const uint32_t CFREQ_SCALE[112] = {
           2750,    2914,    3087,    3270,    3465,    3671,    3889,
           4120,    4365,    4625,    4900,    5191,    5500,    5827,
           6174,    6541,    6930,    7342,    7778,    8241,    8731,
           9250,    9800,   10383,   11000,   11654,   12347,   13081,
          13859,   14683,   15556,   16481,   17461,   18500,   19600,
          20765,   22000,   23308,   24694,   26163,   27718,   29366,
          31113,   32963,   34923,   36999,   39200,   41530,   44000,
          46616,   49388,   52325,   55437,   58733,   62225,   65926,
          69846,   73999,   78399,   83061,   88000,   93233,   98777,
         104650,  110873,  117466,  124451,  131851,  139691,  147998,
         156798,  166122,  176000,  186466,  197553,  209300,  221746,
         234932,  248902,  263702,  279383,  295996,  313596,  332244,
         352000,  372931,  395107,  418601,  443492,  469864,  497803,
         527404,  558765,  591991,  627193,  664488,  704000,  745862,
         790213,  837202,  886984,  939727,  995606, 1054808, 1117530,
        1183982, 1254385, 1328975, 1408000, 1491724, 1580427, 1666667
    };
    const uint8_t CFREQ_SCALE_IDX_C7 = 75;

    const int16_t FOLDCURVE[64] = {
        0,   406,   796,  1154,  1466,  1720,  1906,  2017,  2047,
     1997,  1867,  1663,  1393,  1068,   700,   305,  -102,  -505,
     -889, -1237, -1536, -1774, -1941, -2032, -2042, -1971, -1822,
    -1601, -1316,  -979,  -604,  -204,   204,   604,   979,  1316,
     1601,  1822,  1971,  2042,  2032,  1941,  1774,  1536,  1237,
      889,   505,   102,  -305,  -610,  -802,  -892,  -894,  -830,
     -719,  -582,  -439,  -305,  -191,  -104,   -46,   -14,    0,
        0
    };
    const uint8_t FOLDCURVE_N = 64;

    TDSP::FilterStateVariable filter;

    // States and CVs
    uint8_t state_clock = 63;
    FilterState state_filter;
    int16_t state_wavefolder = 0;

    const uint32_t FSAMPLE_CHZ = 1666667;
    const uint16_t FSAMPLE_HZ = 16667;
    const uint32_t CLK_PHI_MAX = 0xffff;
    uint64_t clk_phi = 0;
    uint64_t clk_dphi;

    // Signals
    int16_t noise;

    // Functions
    void DrawInterface() {
        // Clock
        if (cursor == 0) {
            gfxPrint(1, 15, ">");
        } else {
            gfxPrint(1, 15, "//");
        }
        uint16_t clk = Parameter2Clk(state_clock)/100;
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
        // Filter mode
        if (cursor == 2) gfxPrint(1, 35, ">");

        // Frequency
        if (cursor == 3) gfxPrint(1, 35, "f");

        // Q
        if (cursor == 4) gfxPrint(1, 35, "Q");

        // Filter icon
        if (state_filter.mode == FILTER_MODE_LP) {
            DrawFilterLP(14, 35, state_filter.q);
        } else if (state_filter.mode == FILTER_MODE_HP) {
            DrawFilterHP(14, 35, state_filter.q);
        } else if (state_filter.mode == FILTER_MODE_BP) {
            DrawFilterBP(14, 35, state_filter.q);
        } else if (state_filter.mode == FILTER_MODE_NOTCH) {
            DrawFilterNotch(14, 35, state_filter.q);
        } else {
            gfxPrint(13, 35, FILTER_MODE_NAMES[state_filter.mode]);
        }

        // Filter frequency
        const uint8_t xo = 26;
        uint16_t f = Parameter2Frequency(state_filter.f)/100;
        if (f/1000) {
            gfxPrint(3 + xo, 35, f);
        } else if (f/100) {
            gfxPrint(9 + xo, 35, f);
        } else {
            gfxPrint(15 + xo, 35, f);
        }
        gfxIcon(28 + xo, 34, HERTZ_ICON);

        // Wavefolder
        gfxPrint(1, 45, "WF");
        gfxPrint(16, 45, state_wavefolder);

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

    int16_t InterpolationFolder(int16_t x) {
        // dx = x_max / len(foldcurve)
        const int16_t dx = 4*(NOISE_VALUE_MAX+1)/FOLDCURVE_N;
        uint8_t i;
        int32_t y;
        if (x >= 0) {
            i = x / dx;
            i = i < FOLDCURVE_N - 1 ? i : FOLDCURVE_N - 2;
            y =   (int32_t)(FOLDCURVE[i]*((i + 1)*dx - x))
                + (int32_t)(FOLDCURVE[i + 1]*(x - i*dx));
        } else {
            i = -1 * x / dx;
            i = i < FOLDCURVE_N - 1 ? i : FOLDCURVE_N - 2;
            y =   (int32_t)(-1*FOLDCURVE[i]*((i + 1)*dx + x))
                + (int32_t)(FOLDCURVE[i + 1]*(x + i*dx));
        }
        y /= dx;
        return y;
    }

    void CLKSetCFreq(uint64_t cfreq) {
        // cfreq in cHz
        clk_dphi = (cfreq*CLK_PHI_MAX)/FSAMPLE_CHZ;
    }

    // inline uint16_t Parameter2Frequency(uint8_t p) {
    //     return Proportion(p, NL_MAX_PARAM, 969) + 30;
    // }

    inline uint32_t Parameter2Frequency(uint8_t p) {
        p = p > CFREQ_SCALE_IDX_C7 ? CFREQ_SCALE_IDX_C7 : p;
        return CFREQ_SCALE[p];
    }

    inline uint32_t Parameter2Clk(uint8_t p) {
        p = p < 112 ? p : 111;
        return CFREQ_SCALE[p];
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
