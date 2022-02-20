// Copyright (c) 2022, Korbinian Schreiber
// Copyright (c) 2018, Jason Justian
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
#include "tiny_dsp.h"

#define BNC_MAX_PARAM 63
#define CH_KICK 0
#define CH_SNARE 1
#define CH_PUNCH_DECAY 2

#define CV_MODE_ATTEN 0
#define CV_MODE_TONE  1
#define CV_MODE_DECAY 2
#define CV_MODE_FM    3
#define CV_MODE_DROP  4

class BugCrack : public HemisphereApplet {
public:

    const char* applet_name() {
        return "BugCrack";
    }

    void Start() {
        tone_kick = 32;
        decay_kick = 60;
        punch = 50;
        decay_punch = 32;

        tone_snare = 6;
        decay_snare = 50; // Snare decay
        snap = 55;
        decay_snap = 26;

        noise = random(0, (1<<12));

        kick = WaveformManager::VectorOscillatorFromWaveform(HS::Sine);
        kick.SetFrequency(Proportion(tone_kick, BNC_MAX_PARAM, 3000) + 3000);
        kick.SetScale((12 << 7) * 3);

        ForEachChannel(ch) levels[ch] = 0;

        env_kick = WaveformManager::VectorOscillatorFromWaveform(HS::Exponential);
        env_kick.SetScale(HEMISPHERE_MAX_CV);
        env_kick.Offset(HEMISPHERE_MAX_CV);
        env_kick.Cycle(0);
        SetEnvDecayKick(decay_kick);

        env_punch = WaveformManager::VectorOscillatorFromWaveform(HS::Exponential);
        env_punch.SetScale(HEMISPHERE_3V_CV);
        env_punch.Offset(HEMISPHERE_3V_CV);
        env_punch.Cycle(0);
        SetEnvDecayPunch(decay_punch);

        env_snare = WaveformManager::VectorOscillatorFromWaveform(HS::Exponential);
        env_snare.SetScale(HEMISPHERE_3V_CV);
        env_snare.Offset(HEMISPHERE_3V_CV);
        env_snare.Cycle(0);
        SetEnvDecaySnare(decay_snare);

        env_snap = WaveformManager::VectorOscillatorFromWaveform(HS::Exponential);
        env_snap.SetScale(HEMISPHERE_3V_CV);
        env_snap.Offset(HEMISPHERE_3V_CV);
        env_snap.Cycle(0);
        SetEnvDecaySnap(decay_snap);
    }

    void Controller() {
        int32_t bd_signal = 0;
        int32_t sd_signal = 0;
        cv_kick = Proportion(DetentedIn(CH_KICK), HEMISPHERE_MAX_CV, BNC_MAX_PARAM);
        cv_snare = Proportion(DetentedIn(CH_SNARE), HEMISPHERE_MAX_CV, BNC_MAX_PARAM);

        // Kick drum
        if (cv_mode_kick == CV_MODE_TONE) {
            _tone_kick = constrain(tone_kick + cv_kick, 0, BNC_MAX_PARAM);
        } else {
            _tone_kick = tone_kick;
        }
        if (cv_mode_kick == CV_MODE_DECAY) {
            _decay_kick = constrain(decay_kick + cv_kick, 0, BNC_MAX_PARAM);
        } else {
            _decay_kick = decay_kick;
        }
        if (cv_mode_kick == CV_MODE_FM) {
            _punch = constrain(punch + cv_kick, 0, BNC_MAX_PARAM);
        } else {
            _punch = punch;
        }
        if (cv_mode_kick == CV_MODE_DROP) {
            _decay_punch = constrain(decay_punch + cv_kick, 0, BNC_MAX_PARAM);
        } else {
            _decay_punch = decay_punch;
        }
        if (Clock(CH_KICK, 1)) {
            SetEnvDecayKick(_decay_kick);
            SetEnvDecayPunch(_decay_punch);
            env_kick.Start();
            env_punch.Start();
            kick.Start();
        }
        if (!env_kick.GetEOC()) {
            // base frequency
            int freq_kick = Proportion(_tone_kick, BNC_MAX_PARAM, 3000) + 3000;
            // punchy FM drop
            if (!env_punch.GetEOC()) {
                int df = Proportion(env_punch.Next(), HEMISPHERE_3V_CV, freq_kick);
                df = Proportion(_punch, BNC_MAX_PARAM/4, df);
                freq_kick += df;
            }
            kick.SetFrequency(freq_kick);
            levels[0] = env_kick.Next()/2; // Divide by 2 to account for offset
            if (cv_mode_kick == CV_MODE_ATTEN) {
                levels[0] = Proportion(BNC_MAX_PARAM - cv_kick, BNC_MAX_PARAM, levels[0]);
            }
            bd_signal = Proportion(levels[0], HEMISPHERE_MAX_CV, kick.Next());
            // Because of overtones induced by the linear interpolation of the
            // sine wave vector oscilator, we have to low-pass filter the signal
            bd_signal = filter_lp.filter(bd_signal, freq_kick);
        }

        // Snare drum
        // Bitflip noise
        noise ^= (1 << random(0, 12));
        if (cv_mode_snare == CV_MODE_TONE) {
            _tone_snare = constrain(tone_snare + cv_snare, 0, BNC_MAX_PARAM);
        } else {
            _tone_snare = tone_snare;
        }
        if (cv_mode_snare == CV_MODE_DECAY) {
            _decay_snare = constrain(decay_snare + cv_snare, 0, BNC_MAX_PARAM);
        } else {
            _decay_snare = decay_snare;
        }
        if (cv_mode_snare == CV_MODE_FM) {
            _snap = constrain(snap + cv_snare, 0, BNC_MAX_PARAM);
        } else {
            _snap = snap;
        }
        if (cv_mode_snare == CV_MODE_DROP) {
            _decay_snap = constrain(decay_snap + cv_snare, 0, BNC_MAX_PARAM);
        } else {
            _decay_snap = decay_snap;
        }
        if (Clock(CH_SNARE, 1)) {
            SetEnvDecaySnare(_decay_snare);
            SetEnvDecaySnap(_decay_snap);
            env_snare.Start();
            env_snap.Start();
        }
        if (!env_snare.GetEOC()) {
            int64_t freq_snare = Proportion(_tone_snare, BNC_MAX_PARAM, 500) + 100;
            freq_snare *= 100;
            if (!env_snap.GetEOC()) {
                int64_t df = Proportion(env_snap.Next(), HEMISPHERE_3V_CV, freq_snare/1024);
                df = Proportion(_snap, BNC_MAX_PARAM/4, df);
                df *= 1024;
                freq_snare += df;
            }

            levels[1] = env_snare.Next()/2;
            if (cv_mode_snare == CV_MODE_ATTEN) {
                levels[1] = Proportion(BNC_MAX_PARAM - cv_snare, BNC_MAX_PARAM, levels[1]);
            }
            sd_signal = Proportion(levels[1], HEMISPHERE_3V_CV, noise);
            filter_sv.feed(sd_signal, freq_snare, TDSP::QMAX/4);
            sd_signal = filter_sv.get_lp();
        }

        // Kick Drum Output
        Out(0, bd_signal);

        // Snare Drum Output
        Out(1, sd_signal);
    }

    void View() {
        gfxHeader(applet_name());
        DrawInterface();
    }

    void OnButtonPress() {
        if (++cursor > 8) cursor = 0;
    }

    void OnEncoderMove(int direction) {
        // Kick drum
        if (cursor == 0) {
            tone_kick = constrain(tone_kick + direction, 0, BNC_MAX_PARAM);
        }
        if (cursor == 1) {
            decay_kick = constrain(decay_kick + direction, 0, BNC_MAX_PARAM);
        }
        if (cursor == 2) {
            punch = constrain(punch + direction, 0, BNC_MAX_PARAM);
        }
        if (cursor == 3) {
            decay_punch = constrain(decay_punch + direction, 0, BNC_MAX_PARAM);
        }

        // Snare drum
        if (cursor == 4) {
            tone_snare = constrain(tone_snare + direction, 0, BNC_MAX_PARAM);
        }
        if (cursor == 5) {
            decay_snare = constrain(decay_snare + direction, 0, BNC_MAX_PARAM);
        }
        if (cursor == 6) {
            snap = constrain(snap + direction, 0, BNC_MAX_PARAM);
        }
        if (cursor == 7) {
            decay_snap = constrain(decay_snap + direction, 0, BNC_MAX_PARAM);
        }

        // CV mode
        if (cursor == 8) {
            cv_mode = constrain(cv_mode + direction, 0, 24);
            cv_mode_kick = cv_mode/5;
            cv_mode_snare = cv_mode%5;
        }
        ResetCursor();
    }

    uint32_t OnDataRequest() {
        // 16 bit per drum -> 4 bit per parameter
        uint32_t data = 0;
        Pack(data, PackLocation {0,4}, (tone_kick >> 2));
        Pack(data, PackLocation {4,4}, (decay_kick >> 2));
        Pack(data, PackLocation {8,4}, (punch >> 2));
        Pack(data, PackLocation {12,4}, (decay_punch >> 2));

        Pack(data, PackLocation {16,4}, (tone_snare >> 2));
        Pack(data, PackLocation {20,4}, (decay_snare >> 2));
        Pack(data, PackLocation {24,4}, (snap >> 2));
        Pack(data, PackLocation {28,4}, (decay_snap >> 2));
        return data;
    }

    void OnDataReceive(uint32_t data) {
        tone_kick = (Unpack(data, PackLocation {0,4}) << 2);
        decay_kick = (Unpack(data, PackLocation {4,4}) << 2);
        punch = (Unpack(data, PackLocation {8,4}) << 2);
        decay_punch = (Unpack(data, PackLocation {12,4}) << 2);

        tone_snare = (Unpack(data, PackLocation {16,4}) << 2);
        decay_snare = (Unpack(data, PackLocation {20,4}) << 2);
        snap = (Unpack(data, PackLocation {24,4}) << 2);
        decay_snap = (Unpack(data, PackLocation {28,4}) << 2);
    }

protected:
    void SetHelp() {
        //                               "------------------" <-- Size Guide
        help[HEMISPHERE_HELP_DIGITALS] = "trigger 1=ki 2=sn";
        help[HEMISPHERE_HELP_CVS]      = "cv in   1=ki 2=sn";
        help[HEMISPHERE_HELP_OUTS]     = "output  1=ki 2=sn";
        help[HEMISPHERE_HELP_ENCODER]  = "preset/pan";
        //                               "------------------" <-- Size Guide
    }

private:
    int cursor = 0;
    VectorOscillator kick;
    VectorOscillator env_kick;
    VectorOscillator env_punch;

    VectorOscillator env_snare;
    VectorOscillator env_snap;

    uint32_t noise;

    TDSP::FilterLP filter_lp;
    TDSP::FilterStateVariable filter_sv;

    int cv_kick;
    int cv_snare;
    int levels[2]; // For display

    // Settings
    int tone_kick;
    int _tone_kick;
    int decay_kick;
    int _decay_kick;
    int punch;
    int _punch;
    int decay_punch;
    int _decay_punch;

    int tone_snare;
    int _tone_snare;
    int decay_snare;
    int _decay_snare;
    int snap;
    int _snap;
    int decay_snap;
    int _decay_snap;

    const char *CV_MODE_NAMES[5] = {"atn", "ton", "dec", "FM", "dro"};

    uint8_t cv_mode;
    uint8_t cv_mode_kick;
    uint8_t cv_mode_snare;

    void DrawInterface() {
        DrawDrumBody(1, _tone_kick, _decay_kick, _punch, _decay_punch, 0);
        DrawDrumBody(32, _tone_snare, _decay_snare, _snap, _decay_snap, 1);

        // CV modes
        gfxIcon(1, 57, CV_ICON);
        gfxPrint(10, 55, CV_MODE_NAMES[cv_mode_kick]);
        gfxPrint(41, 55, CV_MODE_NAMES[cv_mode_snare]);

        switch (cursor) {
            // Kick drum
            case 0:
                gfxPrint(9, 45, Proportion(_tone_kick, BNC_MAX_PARAM, 30) + 30);
                gfxIcon(22, 44, HERTZ_ICON);
                break;
            case 1:
                gfxPrint(1, 45, "decay"); break;
            case 2:
                gfxPrint(1, 45, "punch"); break;
            case 3:
                gfxPrint(1, 45, "drop"); break;

            // Snare drum
            case 4:
                gfxPrint(35, 45, Proportion(_tone_snare, BNC_MAX_PARAM, 500) + 100);
                gfxIcon(54, 44, HERTZ_ICON);
                break;
            case 5:
                gfxPrint(32, 45, "decay"); break;
            case 6:
                gfxPrint(32, 45, "snap"); break;
            case 7:
                gfxPrint(32, 45, "drop"); break;
            case 8:
                gfxInvert(1, 54, 61, 9);
        }

        // Level indicators
        ForEachChannel(ch)
            gfxInvert(1 + (31*ch), 53 - ProportionCV(levels[ch], 34),
                      30, ProportionCV(levels[ch], 34));
    }

    void DrawDrumBody(byte x, byte tone, byte decay, byte punch, byte pdecay, bool is_snare) {
        const int8_t wmin = 10;
        const int8_t wmax = 30;
        const int8_t hmin = 6;
        const int8_t hmax = 16;

        int8_t w = Proportion(decay, BNC_MAX_PARAM, wmax - wmin) + wmin;
        int8_t h = Proportion(punch, BNC_MAX_PARAM, hmax - hmin) + hmin;
        int8_t body_h = (2*h/hmin - 1) + hmin - 2;
        int8_t r = Proportion(pdecay, BNC_MAX_PARAM, body_h);
        int8_t y = 40 - Proportion(tone, BNC_MAX_PARAM, 18);

        int8_t cx = x + wmax/2;
        int8_t cy = y;

        // Body
        int dx = w/5;
        gfxLine(
            cx - dx + 1, cy - body_h/2,
            cx + dx - 1, cy - body_h/2);
        gfxLine(
            cx - dx + 2, cy - body_h/2-1,
            cx + dx - 2, cy - body_h/2-1);
        gfxLine(
            cx - dx + 1, cy + body_h/2,
            cx + dx - 1, cy + body_h/2);
        gfxRect(
            cx - dx + 1, cy - body_h/2 + 1,
            2*dx - 1, r - 1);

        // Legs
        for(int p=-1; p<=1; p+=2) { // parity for both sides
            int _dx = p*dx;
            // Front legs
            gfxLine(
                cx + _dx, cy - body_h/2,
                cx + p*w/3, cy - h/2);
            // Mid legs
            gfxLine(
                cx + _dx, cy - 1,
                cx + _dx + 2*p, cy-1);
            gfxLine(
                cx + _dx + 2*p, cy - 1,
                cx + p*w/2, cy - 2);
            // Rear legs
            gfxLine(
                cx + _dx, cy + 1,
                cx + p*w/3, cy + hmax/h);
            gfxLine(
                cx + p*w/3, cy + hmax/h,
                cx + p*w/2, cy + h/2);
            // Body flank
            gfxLine(
                cx+_dx, cy-body_h/2+1,
                cx+_dx, cy+body_h/2-1);
            if(is_snare) {
                // Some feelers for snare bug
                gfxLine(
                    cx + p, cy - body_h/2 - 2,
                    cx + 2*p, cy - body_h/2 - 2 - hmax/h);
            } else {
                // Some eyes on the kick bug
                gfxInvert(
                    cx + _dx - p, cy - body_h/2 + 1,
                    1, 1);
            }
        }
    }

    void SetEnvDecayKick(int decay) {
        env_kick.SetFrequency(1000 - Proportion(decay, BNC_MAX_PARAM, 900));
    }
    void SetEnvDecayPunch(int decay) {
        // 25 ms - 200 ms -> 4000 cHz - 500 cHz
        env_punch.SetFrequency(
            4000 - Proportion(decay, BNC_MAX_PARAM, 3500));
    }

    void SetEnvDecaySnare(int decay) {
        env_snare.SetFrequency(1000 - Proportion(decay, BNC_MAX_PARAM, 900));
    }
    void SetEnvDecaySnap(int decay) {
        env_snap.SetFrequency(
            8000 - Proportion(decay, BNC_MAX_PARAM, 7500));
    }
};


////////////////////////////////////////////////////////////////////////////////
//// Hemisphere Applet Functions
///
///  Once you run the find-and-replace to make these refer to BugCrack,
///  it's usually not necessary to do anything with these functions. You
///  should prefer to handle things in the HemisphereApplet child class
///  above.
////////////////////////////////////////////////////////////////////////////////
BugCrack BugCrack_instance[2];

void BugCrack_Start(bool hemisphere) {BugCrack_instance[hemisphere].BaseStart(hemisphere);}
void BugCrack_Controller(bool hemisphere, bool forwarding) {BugCrack_instance[hemisphere].BaseController(forwarding);}
void BugCrack_View(bool hemisphere) {BugCrack_instance[hemisphere].BaseView();}
void BugCrack_OnButtonPress(bool hemisphere) {BugCrack_instance[hemisphere].OnButtonPress();}
void BugCrack_OnEncoderMove(bool hemisphere, int direction) {BugCrack_instance[hemisphere].OnEncoderMove(direction);}
void BugCrack_ToggleHelpScreen(bool hemisphere) {BugCrack_instance[hemisphere].HelpScreen();}
uint32_t BugCrack_OnDataRequest(bool hemisphere) {return BugCrack_instance[hemisphere].OnDataRequest();}
void BugCrack_OnDataReceive(bool hemisphere, uint32_t data) {BugCrack_instance[hemisphere].OnDataReceive(data);}
