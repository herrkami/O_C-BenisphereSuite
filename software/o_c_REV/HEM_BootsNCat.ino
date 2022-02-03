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
#define CH_KICK 0
#define CH_SNARE 1
#define CH_PUNCH_DECAY 2

class BootsNCat : public HemisphereApplet {
public:

    const char* applet_name() {
        return "BootsNCat";
    }

    void Start() {
        tone_kick = 32; // Kick drum freq
        decay_kick = 32; // Kick drum decay
        punch = BNC_MAX_PARAM/2; // Kick drum punch
        decay_punch = 32; // Kick drum punch decay

        tone_snare = 55; // Snare low limit
        decay_snare = 16; // Snare decay
        noise_tone_countdown = 1;

        blend = 0;

        kick = WaveformManager::VectorOscillatorFromWaveform(HS::Sine);
        SetBDFreq();
        // Audio signal is -3V to +3V due to DAC asymmetry
        kick.SetScale((12 << 7) * 3);

        ForEachChannel(ch) levels[ch] = 0;

        env_kick = WaveformManager::VectorOscillatorFromWaveform(HS::Exponential);
        env_kick.SetScale(HEMISPHERE_MAX_CV);
        env_kick.Offset(HEMISPHERE_MAX_CV);
        env_kick.Cycle(0);
        SetEnvDecayKick();

        env_punch = WaveformManager::VectorOscillatorFromWaveform(HS::Exponential);
        env_punch.SetScale(HEMISPHERE_3V_CV);
        env_punch.Offset(HEMISPHERE_3V_CV);
        env_punch.Cycle(0);
        SetEnvDecayPunch();

        env_snare = WaveformManager::VectorOscillatorFromWaveform(HS::Exponential);
        env_snare.SetScale(HEMISPHERE_3V_CV);
        env_snare.Offset(HEMISPHERE_3V_CV);
        env_snare.Cycle(0);
        SetEnvDecaySnare();
    }

    void Controller() {
        int32_t signal = 0;
        int32_t bd_signal = 0;
        int32_t sd_signal = 0;

        if (Changed(CH_KICK))
            env_kick.SetScale(HEMISPHERE_MAX_CV - In(CH_KICK));
        if (Clock(CH_KICK, 1)) {  // Use physical-only clocking
            env_kick.Start();
            env_punch.Start();
            kick.Start();  // Set phase to zero
        }
        if (Changed(CH_SNARE))
            env_snare.SetScale(HEMISPHERE_3V_CV - In(CH_SNARE));
        if (Clock(CH_SNARE, 1)) {  // Use physical-only clocking
            env_snare.Start();
        }

        // Calculate kick drum signal
        if (!env_kick.GetEOC()) {
            // base frequency
            int freq = Proportion(tone_kick, BNC_MAX_PARAM, 3000) + 3000;

            // punchy FM drop
            if (!env_punch.GetEOC()) {
                int df = Proportion(env_punch.Next(), HEMISPHERE_3V_CV, freq);
                df = Proportion(punch, BNC_MAX_PARAM/4, df);
                freq = freq + df;
            }

            kick.SetFrequency(freq);
            levels[0] = env_kick.Next()/2; // Divide by 2 to account for offset
            bd_signal = Proportion(levels[0], HEMISPHERE_MAX_CV, kick.Next());
        }

        // Calculate snare drum signal
        if (--noise_tone_countdown == 0) {
            noise = random(0, (12 << 7) * 6) - ((12 << 7) * 3);
            noise_tone_countdown = BNC_MAX_PARAM - tone_snare + 1;
        }

        if (!env_snare.GetEOC()) {
            levels[1] = env_snare.Next();
            sd_signal = Proportion(levels[1], HEMISPHERE_MAX_CV, noise);
        }

        // Kick Drum Output
        signal = Proportion((BNC_MAX_PARAM - blend) + BNC_MAX_PARAM, BNC_MAX_PARAM * 2, bd_signal);
        signal += Proportion(blend, BNC_MAX_PARAM * 2, sd_signal); // Blend in snare drum
        Out(0, signal);

        // Snare Drum Output
        signal = Proportion((BNC_MAX_PARAM - blend) + BNC_MAX_PARAM, BNC_MAX_PARAM * 2, sd_signal);
        signal += Proportion(blend, BNC_MAX_PARAM * 2, bd_signal); // Blend in kick drum
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

            // Kick drum
            if (cursor == 0) {
                tone_kick = constrain(tone_kick + direction, 0, BNC_MAX_PARAM);
            }
            if (cursor == 1) {
                decay_kick = constrain(decay_kick + direction, 0, BNC_MAX_PARAM);
                SetEnvDecayKick();
            }
            if (cursor == 2) {
                punch = constrain(punch + direction, 0, BNC_MAX_PARAM);
            }
            if (cursor == 3) {
                decay_punch = constrain(decay_punch + direction, 0, BNC_MAX_PARAM);
                SetEnvDecayPunch();
            }

            // Snare drum
            if (cursor == 4) {
                tone_snare = constrain(tone_snare + direction, 0, BNC_MAX_PARAM);
            }
            if (cursor == 5) {
                decay_snare = constrain(decay_snare + direction, 0, BNC_MAX_PARAM);
                SetEnvDecaySnare();
            }
        }
        ResetCursor();
    }

    uint32_t OnDataRequest() {
        uint32_t data = 0;
        Pack(data, PackLocation {0,6}, tone_kick);
        Pack(data, PackLocation {6,6}, decay_kick);
        Pack(data, PackLocation {12,6}, tone_snare);
        Pack(data, PackLocation {18,6}, decay_snare);
        Pack(data, PackLocation {24,6}, blend);
        return data;
    }

    void OnDataReceive(uint32_t data) {
        tone_kick = Unpack(data, PackLocation {0,6});
        decay_kick = Unpack(data, PackLocation {6,6});
        tone_snare = Unpack(data, PackLocation {12,6});
        decay_snare = Unpack(data, PackLocation {18,6});
        blend = Unpack(data, PackLocation {24,6});
    }

protected:
    void SetHelp() {
        //                               "------------------" <-- Size Guide
        help[HEMISPHERE_HELP_DIGITALS] = "Trigger 1=BD 2=SD";
        help[HEMISPHERE_HELP_CVS]      = "Atten.  1=BD 2=SD";
        help[HEMISPHERE_HELP_OUTS]     = "Output  1=BD 2=SD";
        help[HEMISPHERE_HELP_ENCODER]  = "Preset/Pan";
        //                               "------------------" <-- Size Guide
    }

private:
    int cursor = 0;
    VectorOscillator kick;
    VectorOscillator env_kick;
    VectorOscillator env_snare;
    VectorOscillator env_punch;
    int noise_tone_countdown = 0;
    uint32_t noise;
    int levels[2]; // For display

    // Settings
    int tone_kick;
    int tone_snare;
    int decay_kick;
    int decay_snare;
    int decay_punch;
    int8_t blend;
    int punch;

    void DrawInterface() {
        gfxBitmap(1, 15, 8, NOTE_ICON);
        DrawKnobAt(10, 15, 18, tone_kick, cursor == 0);

        gfxBitmap(1, 25, 8, DECAY_ICON);
        DrawKnobAt(10, 25, 18, decay_kick, cursor == 1);

        gfxBitmap(1, 35, 8, FM_ICON);
        DrawKnobAt(10, 35, 18, punch, cursor == 2);

        gfxBitmap(1, 45, 8, BANG_ICON);
        DrawKnobAt(10, 45, 18, decay_punch, cursor == 3);

        gfxBitmap(32, 15, 8, NOTE_ICON);
        DrawKnobAt(41, 15, 18, tone_snare, cursor == 4);

        gfxBitmap(32, 25, 8, DECAY_ICON);
        DrawKnobAt(41, 25, 18, decay_snare, cursor == 5);

        gfxBitmap(32, 35, 8, FM_ICON);
        DrawKnobAt(41, 35, 18, tone_snare, cursor == 6);

        gfxBitmap(32, 45, 8, BANG_ICON);
        DrawKnobAt(41, 45, 18, decay_snare, cursor == 7);

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
        kick.SetFrequency(Proportion(tone_kick, BNC_MAX_PARAM, 3000) + 3000);
    }

    void SetEnvDecayKick() {
        env_kick.SetFrequency(1000 - Proportion(decay_kick, BNC_MAX_PARAM, 900));
    }
    void SetEnvDecaySnare() {
        env_snare.SetFrequency(1000 - Proportion(decay_snare, BNC_MAX_PARAM, 900));
    }

    void SetEnvDecayPunch() {
        // 10 ms - 200 ms -> 10000 cHz - 500 cHz
        env_punch.SetFrequency(
            10000 - Proportion(decay_punch, BNC_MAX_PARAM, 9500));
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
