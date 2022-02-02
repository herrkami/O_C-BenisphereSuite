// Copyright (c) 2018, Jason Justian
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

#include "vector_osc/HSVectorOscillator.h"
#include "vector_osc/WaveformManager.h"

#define BNC_MAX_PARAM 63
#define CH_BASS 0
#define CH_SNARE 1
#define CH_PUNCH_DECAY 2

class BootsNCat : public HemisphereApplet {
public:

    const char* applet_name() {
        return "BootsNCat";
    }

    void Start() {
        tone[0] = 32; // Bass drum freq
        decay[0] = 32; // Bass drum decay
        punch = BNC_MAX_PARAM/2; // Bass drum punch
        decay[2] = 32; // Bass drum punch decay

        tone[1] = 55; // Snare low limit
        decay[1] = 16; // Snare decay
        noise_tone_countdown = 1;
        blend = 0;

        bass = WaveformManager::VectorOscillatorFromWaveform(HS::Sine);
        SetBDFreq();
        bass.SetScale((12 << 7) * 3); // Audio signal is -3V to +3V due to DAC asymmetry

        ForEachChannel(ch)
        {
            levels[ch] = 0;
            eg[ch] = WaveformManager::VectorOscillatorFromWaveform(HS::Exponential);
            eg[ch].SetFrequency(decay[ch]);
            eg[ch].SetScale(ch ? HEMISPHERE_3V_CV : HEMISPHERE_MAX_CV);
            eg[ch].Offset(ch ? HEMISPHERE_3V_CV : HEMISPHERE_MAX_CV);
            eg[ch].Cycle(0);
            SetEGFreq(ch);
        }
        eg[CH_PUNCH_DECAY] = WaveformManager::VectorOscillatorFromWaveform(HS::Exponential);
        // eg[CH_PUNCH_DECAY].SetFrequency(decay_punch);
        eg[CH_PUNCH_DECAY].SetScale(HEMISPHERE_3V_CV);
        eg[CH_PUNCH_DECAY].Offset(HEMISPHERE_3V_CV);
        eg[CH_PUNCH_DECAY].Cycle(0);
        SetPunchDecayFreq();
    }

    void Controller() {
        // Bass and snare signals are calculated independently
        int32_t signal = 0;
        int32_t bd_signal = 0;
        int32_t sd_signal = 0;

        if (Changed(CH_BASS)) eg[CH_BASS].SetScale(HEMISPHERE_MAX_CV - In(CH_BASS));
        if (Clock(CH_BASS, 1)) {  // Use physical-only clocking
            eg[CH_BASS].Start();
            eg[CH_PUNCH_DECAY].Start();
            bass.Start();  // Set phase to zero
        }
        if (Changed(CH_SNARE)) eg[CH_SNARE].SetScale(HEMISPHERE_3V_CV - In(CH_SNARE));
        if (Clock(CH_SNARE, 1)) {  // Use physical-only clocking
            eg[CH_SNARE].Start();
        }

        // Calculate bass drum signal
        if (!eg[0].GetEOC()) {
            // base frequency
            int freq = Proportion(tone[0], BNC_MAX_PARAM, 3000) + 3000;

            if (!eg[CH_PUNCH_DECAY].GetEOC()) {
                // punchy FM drop
                int df = Proportion(eg[CH_PUNCH_DECAY].Next(), HEMISPHERE_3V_CV, freq);
                df = Proportion(punch, BNC_MAX_PARAM/4, df);
                freq = freq + df;
            }

            bass.SetFrequency(freq);
            levels[0] = eg[0].Next()/2; // Divide by 2 to account for offset
            bd_signal = Proportion(levels[0], HEMISPHERE_MAX_CV, bass.Next());
        }

        // Calculate snare drum signal
        if (--noise_tone_countdown == 0) {
            noise = random(0, (12 << 7) * 6) - ((12 << 7) * 3);
            noise_tone_countdown = BNC_MAX_PARAM - tone[1] + 1;
        }

        if (!eg[1].GetEOC()) {
            levels[1] = eg[1].Next();
            sd_signal = Proportion(levels[1], HEMISPHERE_MAX_CV, noise);
        }

        // Bass Drum Output
        signal = Proportion((BNC_MAX_PARAM - blend) + BNC_MAX_PARAM, BNC_MAX_PARAM * 2, bd_signal);
        signal += Proportion(blend, BNC_MAX_PARAM * 2, sd_signal); // Blend in snare drum
        Out(0, signal);

        // Snare Drum Output
        signal = Proportion((BNC_MAX_PARAM - blend) + BNC_MAX_PARAM, BNC_MAX_PARAM * 2, sd_signal);
        signal += Proportion(blend, BNC_MAX_PARAM * 2, bd_signal); // Blend in bass drum
        Out(1, signal);
    }

    void View() {
        gfxHeader(applet_name());
        DrawInterface();
    }

    void OnButtonPress() {
        if (++cursor > 8) cursor = 0;
    }

    void OnEncoderMove(int direction) {
        if (cursor == 8) { // Blend
            blend = constrain(blend + direction, 0, BNC_MAX_PARAM);
        } else {
            byte ch = cursor > 3 ? 1 : 0;
            byte c = cursor;
            if (ch) c -= 4;

            if (c == 0) { // Tone
                tone[ch] = constrain(tone[ch] + direction, 0, BNC_MAX_PARAM);
                // if (ch == 0) SetBDFreq();
            }

            if (c == 1) { // Decay
                decay[ch] = constrain(decay[ch] + direction, 0, BNC_MAX_PARAM);
                SetEGFreq(ch);
            }

            if (ch == 0) {
                if (c == 2) { // FM (punch)
                    punch = constrain(punch + direction, 0, BNC_MAX_PARAM);
                }

                if (c == 3) { // punch decay
                    decay[CH_PUNCH_DECAY] = constrain(decay[CH_PUNCH_DECAY] + direction, 0, BNC_MAX_PARAM);
                    SetPunchDecayFreq();
                }
            }

        }
        ResetCursor();
    }

    uint32_t OnDataRequest() {
        uint32_t data = 0;
        Pack(data, PackLocation {0,6}, tone[0]);
        Pack(data, PackLocation {6,6}, decay[0]);
        Pack(data, PackLocation {12,6}, tone[1]);
        Pack(data, PackLocation {18,6}, decay[1]);
        Pack(data, PackLocation {24,6}, blend);
        return data;
    }

    void OnDataReceive(uint32_t data) {
        tone[0] = Unpack(data, PackLocation {0,6});
        decay[0] = Unpack(data, PackLocation {6,6});
        tone[1] = Unpack(data, PackLocation {12,6});
        decay[1] = Unpack(data, PackLocation {18,6});
        blend = Unpack(data, PackLocation {24,6});
    }

protected:
    void SetHelp() {
        //                               "------------------" <-- Size Guide
        help[HEMISPHERE_HELP_DIGITALS] = "1,2 Play";
        help[HEMISPHERE_HELP_CVS]      = "Atten. 1=BD 2=SD";
        help[HEMISPHERE_HELP_OUTS]     = "A=Left B=Right";
        help[HEMISPHERE_HELP_ENCODER]  = "Preset/Pan";
        //                               "------------------" <-- Size Guide
    }

private:
    int cursor = 0;
    VectorOscillator bass;
    VectorOscillator eg[3];
    int noise_tone_countdown = 0;
    uint32_t noise;
    int levels[2]; // For display

    // Settings
    int tone[2];
    int decay[3];
    int8_t blend;
    int punch;

    void DrawInterface() {
        gfxBitmap(1, 15, 8, NOTE_ICON);
        DrawKnobAt(10, 15, 18, tone[0], cursor == 0);

        gfxBitmap(1, 25, 8, DECAY_ICON);
        DrawKnobAt(10, 25, 18, decay[0], cursor == 1);

        gfxBitmap(1, 35, 8, FM_ICON);
        DrawKnobAt(10, 35, 18, punch, cursor == 2);

        gfxBitmap(1, 45, 8, BANG_ICON);
        DrawKnobAt(10, 45, 18, decay[CH_PUNCH_DECAY], cursor == 3);

        gfxBitmap(32, 15, 8, NOTE_ICON);
        DrawKnobAt(41, 15, 18, tone[1], cursor == 4);

        gfxBitmap(32, 25, 8, DECAY_ICON);
        DrawKnobAt(41, 25, 18, decay[1], cursor == 5);

        gfxBitmap(32, 35, 8, FM_ICON);
        DrawKnobAt(41, 35, 18, tone[1], cursor == 6);

        gfxBitmap(32, 45, 8, BANG_ICON);
        DrawKnobAt(41, 45, 18, decay[1], cursor == 7);

        gfxPrint(1, 55, "BD");
        gfxPrint(49, 55, "SN");
        DrawKnobAt(20, 55, 20, blend, cursor == 8);

        // Level indicators
        // x, y, w, h
        ForEachChannel(ch)
            gfxInvert(1 + (31*ch), 63 - ProportionCV(levels[ch], 42),
                      30, ProportionCV(levels[ch], 42));
    }

    void DrawKnobAt(byte x, byte y, byte len, byte value, bool is_cursor) {
        // byte x = 45;
        byte w = Proportion(value, BNC_MAX_PARAM, len);
        byte p = is_cursor ? 1 : 3;
        gfxDottedLine(x, y + 4, x + len, y + 4, p);
        gfxRect(x + w, y, 2, 7);
    }

    void SetBDFreq() {
        bass.SetFrequency(Proportion(tone[0], BNC_MAX_PARAM, 3000) + 3000);
    }

    void SetEGFreq(byte ch) {
        eg[ch].SetFrequency(1000 - Proportion(decay[ch], BNC_MAX_PARAM, 900));
    }

    void SetPunchDecayFreq() {
        // 10 ms - 200 ms -> 10000 cHz - 500 cHz
        eg[CH_PUNCH_DECAY].SetFrequency(
            10000 - Proportion(decay[CH_PUNCH_DECAY], BNC_MAX_PARAM, 9500));
    }
};


////////////////////////////////////////////////////////////////////////////////
//// Hemisphere Applet Functions
///
///  Once you run the find-and-replace to make these refer to BootsNCat,
///  it's usually not necessary to do anything with these functions. You
///  should prefer to handle things in the HemisphereApplet child class
///  above.
////////////////////////////////////////////////////////////////////////////////
BootsNCat BootsNCat_instance[2];

void BootsNCat_Start(bool hemisphere) {BootsNCat_instance[hemisphere].BaseStart(hemisphere);}
void BootsNCat_Controller(bool hemisphere, bool forwarding) {BootsNCat_instance[hemisphere].BaseController(forwarding);}
void BootsNCat_View(bool hemisphere) {BootsNCat_instance[hemisphere].BaseView();}
void BootsNCat_OnButtonPress(bool hemisphere) {BootsNCat_instance[hemisphere].OnButtonPress();}
void BootsNCat_OnEncoderMove(bool hemisphere, int direction) {BootsNCat_instance[hemisphere].OnEncoderMove(direction);}
void BootsNCat_ToggleHelpScreen(bool hemisphere) {BootsNCat_instance[hemisphere].HelpScreen();}
uint32_t BootsNCat_OnDataRequest(bool hemisphere) {return BootsNCat_instance[hemisphere].OnDataRequest();}
void BootsNCat_OnDataReceive(bool hemisphere, uint32_t data) {BootsNCat_instance[hemisphere].OnDataReceive(data);}
