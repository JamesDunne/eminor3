/*
    Programmable e-minor MIDI foot controller v2.

    Currently designed to work with:
        Axe-FX II (MIDI channel 3)

    Written by
    James S. Dunne
    https://github.com/JamesDunne/
    2017-09-21

    Axe-FX:
    + Split stereo cab block into two mono blocks; CAB1 pan L, CAB2 pan R
    + Try to remove pre-cab PEQ; merge curve with post-cab PEQ
    + Post-cab PEQ handles stereo out of CABs
    + Amp X/Y for dirty/clean (disable for acoustic)
    + Cab X/Y for electric/acoustic

    Controller:
    Amp control row:
    * 1 clean/dirty toggle, hold for acoustic (switches XY for amp and cab, maybe PEQ)
    * 2 volume dec, hold for 0dB
    * 3 volume inc, hold for +6dB
    * 4 gain dec, hold for song default
    * 5 gain inc
    * 6 switch to fx controls for row
    * 7,8 unchanged = prev/next song or scene
    FX row
    * 1 pitch
    * 2 custom, store MIDI CC
    * 3 custom, store MIDI CC
    * 4 chorus
    * 5 delay
    * 6 switch to amp controls for row

TODO: adjust MIDI program # per song
TODO: adjust tempo per song

AMP controls:
|------------------------------------------------------------|
|     *      *      *      *      *      *      *      *     |          /--------------------\
|  CLN|DRV GAIN-- GAIN++ VOL--  VOL++   FX    PR_PRV PR_NXT  |          |Beautiful_Disaster_*|
|  ACOUSTC                             RESET                 |          |Sng 62/62  Scn  1/10|
|                                                            |    LCD:  |C g=58 v=-99.9 P12CD|
|     *      *      *      *      *      *      *      *     |          |D g=5E v=  0.0 -1---|
|  CLN|DRV GAIN-- GAIN++ VOL--  VOL++   FX     MODE  SC_NXT  |          \--------------------/
|  ACOUSTC                             RESET         SC_ONE  |
|------------------------------------------------------------|

Press CLN|DRV to toggle clean vs overdrive mode (clean:    AMP -> Y, CAB -> X, gain -> 0x5E; dirty: AMP -> X, CAB -> X, gain -> n)
Hold  ACOUSTC to switch to acoustic emulation   (acoustic: AMP -> bypass, CAB -> Y, gain -> 0x5E)

Hold  VOL--   to decrease volume slowly
Hold  VOL++   to increase volume slowly

Hold  GAIN--  to decrease gain slowly
Hold  GAIN++  to increase gain slowly

Press FX      to switch row to FX mode

Hold  RESET   to resend MIDI state for amp

Press PR_PRV  to select previous song or program depending on MODE
Press PR_NXT  to select next song or program depending on MODE

Press MODE    to switch between set-list order and program # order

Press SC_NXT  to advance to next scene, move to next song scene 1 if at end
Hold  SC_ONE  to reset scene to 1 on current song

FX controls:
|------------------------------------------------------------|    
|     *      *      *      *      *      *      *      *     |          /--------------------\
|    FX1    FX2    FX3   CHORUS DELAY   AMP   PR_PRV PR_NXT  |          |What_I_Got_________*|
|   SELECT SELECT SELECT                                     |          |Sng 62/62  Scn  2/ 3|
|                                                            |    LCD:  |PIT1ROT1FIL1CHO1DLY1|
|     *      *      *      *      *      *      *      *     |          |A g=5E v=  6.0 ---CD|
|  CLN|DRV GAIN-- GAIN++ VOL--  VOL++   FX     MODE  SC_NXT  |          \--------------------/
|  ACOUSTC                             RESET         SC_ONE  |
|------------------------------------------------------------|

Press AMP to switch row to AMP mode

*/

#include <string.h>
#include "util.h"

#include "program-v5.h"
#include "hardware.h"

// Hard-coded MIDI channel #s:
#define gmaj_midi_channel    0
#define rjm_midi_channel     1
#define axe_midi_channel     2
#define triaxis_midi_channel 3

// Axe-FX II CC messages:
#define axe_cc_taptempo     14
#define axe_cc_tuner        15

// Output mixer gain:
#define axe_cc_external1    16
#define axe_cc_external2    17
// Amp gain:
#define axe_cc_external3    18
#define axe_cc_external4    19

#define axe_cc_scene        34

#define axe_cc_byp_amp1         37
#define axe_cc_byp_amp2         38
#define axe_cc_byp_chorus1      41
#define axe_cc_byp_chorus2      42
#define axe_cc_byp_compressor1  43
#define axe_cc_byp_compressor2  44
#define axe_cc_byp_delay1       47
#define axe_cc_byp_delay2       48
#define axe_cc_byp_gate1        60
#define axe_cc_byp_gate2        61
#define axe_cc_byp_phaser1      75
#define axe_cc_byp_phaser2      76
#define axe_cc_byp_pitch1       77
#define axe_cc_byp_pitch2       78
#define axe_cc_byp_rotary1      86
#define axe_cc_byp_rotary2      87

#define axe_cc_xy_amp1     100
#define axe_cc_xy_amp2     101
#define axe_cc_xy_cab1     102
#define axe_cc_xy_cab2     103
#define axe_cc_xy_chorus1  104
#define axe_cc_xy_chorus2  105
#define axe_cc_xy_delay1   106
#define axe_cc_xy_delay2   107
#define axe_cc_xy_pitch1   114
#define axe_cc_xy_pitch2   115

#ifdef FEAT_LCD
// Pointers to LCD character rows:
char *lcd_rows[LCD_ROWS];
#define row_song 0
#define row_stat 1
#define row_amp1 2
#define row_amp2 3
#endif

enum {
    MODE_LIVE = 0,
    MODE_count
};

enum rowstate_mode {
    ROWMODE_AMP,
    ROWMODE_FX
};

// Tap tempo CC value (toggles between 0x00 and 0x7F):
u8 tap;

// Current mode:
u8 mode;

// Max program #:
u8 sl_max;
// Pointer to unmodified program:
rom struct program *origpr;

// Structure to represent state that should be compared from current to last to detect changes in program.
struct state {
    // Footswitch state:
    u16 fsw;

    // 0 for program mode, 1 for setlist mode:
    u8 setlist_mode;
    // Current setlist entry:
    u8 sl_idx;
    // Current program:
    u8 pr_idx;
    // Current scene:
    u8 sc_idx;
    // Current MIDI program #:
    u8 midi_program;
    // Current tempo (bpm):
    u8 tempo;

    // Amp definitions:
    struct amp amp[2];

    // Each row's current state:
    struct {
        enum rowstate_mode mode;
        u8 fx;
    } rowstate[2];

    // Whether current program is modified in any way:
    u8 modified;
};

// Current and last state:
struct state curr, last;
struct {
    u8 amp_byp, amp_xy, cab_xy, gain, clean_gain, gate;
} last_amp[2];

// Loaded setlist:
struct set_list sl;
// Loaded program:
struct program pr;

// BCD-encoded dB value table (from PIC/v4_lookup.h):
extern rom const u16 dB_bcd_lookup[128];

// Set Axe-FX CC value
#define midi_axe_cc(cc, val) midi_send_cmd2(0xB, axe_midi_channel, cc, val)
#define midi_axe_pc(program) midi_send_cmd1(0xC, axe_midi_channel, program)
#define midi_axe_sysex_start(fn) { \
  midi_send_sysex(0xF0); \
  midi_send_sysex(0x00); \
  midi_send_sysex(0x01); \
  midi_send_sysex(0x74); \
  midi_send_sysex(0x03); \
  midi_send_sysex(fn); \
}
#define midi_axe_sysex_end(chksum) { \
  midi_send_sysex(chksum); \
  midi_send_sysex(0xF7); \
}

// ------------------------- Actual controller logic -------------------------

void prev_scene(void);

void next_scene(void);

void reset_scene(void);

void prev_song(void);

void next_song(void);

void gain_set(int amp, u8 new_gain);

void volume_set(int amp, u8 new_volume);

void midi_invalidate(void);

void toggle_setlist_mode(void);

void tap_tempo(void);

static void scene_default(void);

// (enable == 0 ? (u8)0 : (u8)0x7F)
#define calc_cc_toggle(enable) \
    ((u8) -((s8)(enable)) >> (u8)1)

#define or_default(a, b) (a == 0 ? b : a)

static void update_lcd(void);

#ifdef HWFEAT_REPORT

// Initialized in controller_init:
struct report *report = NULL;

// Fill in a report structure with latest controller data:
static void report_build(void) {
    // Safety:
    if (report == NULL) return;

    // Copy in program name:
    if (pr.name[0] == 0) {
        // Show unnamed song index:
        for (int i = 0; i < REPORT_PR_NAME_LEN; i++) {
            report->pr_name[i] = "__unnamed song #    "[i];
        }
        ritoa(report->pr_name, 18, curr.pr_idx + (u8) 1);
    } else {
        strncpy(report->pr_name, (const char *) pr.name, REPORT_PR_NAME_LEN);
    }

    report->tempo = curr.tempo;

    // program modified:
    report->is_modified = (curr.modified != 0);

    // setlist mode vs program mode:
    report->is_setlist_mode = (curr.setlist_mode != 0);

    report->pr_val = curr.pr_idx + 1u;
    report->pr_max = 128;
    report->sl_val = curr.sl_idx + 1u;
    report->sl_max = sl_max + 1u;
    report->sc_val = curr.sc_idx + 1u;
    report->sc_max = pr.scene_count;

    // Copy amp settings:
    for (int i = 0; i < 2; i++) {
        // Set amp tone:
        if ((curr.amp[i].fx & fxm_acoustc))
            report->amp[i].tone = AMP_TONE_ACOUSTIC;
        else if ((curr.amp[i].fx & fxm_dirty))
            report->amp[i].tone = AMP_TONE_DIRTY;
        else
            report->amp[i].tone = AMP_TONE_CLEAN;

        report->amp[i].gain_dirty = last_amp[i].gain;
        // TODO: gain_clean needs fixing controller code to be its own value

        report->amp[i].volume = curr.amp[i].volume;

        // Copy FX settings:
        u8 test_fx = 1;
        for (int f = 0; f < FX_COUNT; f++, test_fx <<= 1) {
            report->amp[i].fx_enabled[f] = (curr.amp[i].fx & test_fx) == test_fx;
            report->amp[i].fx_midi_cc[f] = pr.fx_midi_cc[i][f];
        }
    }

    // Notify host that report is updated:
    report_notify();
}

#endif

// MIDI is sent at a fixed 3,125 bytes/sec transfer rate; 56 bytes takes 175ms to complete.
// calculate the difference from last MIDI state to current MIDI state and send the difference as MIDI commands:
static void calc_midi(void) {
    u8 diff = 0;
    u8 dirty, last_dirty;
    u8 acoustc, last_acoustc;
    u8 test_fx = 1;
    u8 i, a;

    // Send MIDI program change:
    if (curr.midi_program != last.midi_program) {
        DEBUG_LOG1("MIDI change program %d", curr.midi_program);
        midi_axe_pc(curr.midi_program);
        // All bets are off as to what state when changing program:
        midi_invalidate();
    }

    if (curr.setlist_mode != last.setlist_mode) {
        diff = 1;
    }

    // Send controller changes per amp:
    for (a = 0; a < 2; a++) {
        dirty = curr.amp[a].fx & fxm_dirty;
        acoustc = curr.amp[a].fx & fxm_acoustc;
        last_dirty = last.amp[a].fx & fxm_dirty;
        last_acoustc = last.amp[a].fx & fxm_acoustc;

        if (acoustc != 0) {
            // acoustic:
            if (last_amp[a].amp_byp != 0x00) {
                last_amp[a].amp_byp = 0x00;
                DEBUG_LOG1("AMP%d off", a + 1);
                midi_axe_cc(axe_cc_byp_amp1 + a, last_amp[a].amp_byp);
                diff = 1;
            }
            if (last_amp[a].cab_xy != 0x00) {
                last_amp[a].cab_xy = 0x00;
                DEBUG_LOG1("CAB%d Y", a + 1);
                midi_axe_cc(axe_cc_xy_cab1 + a, last_amp[a].cab_xy);
                diff = 1;
            }
            if (last_amp[a].gain != last_amp[a].clean_gain) {
                last_amp[a].gain = last_amp[a].clean_gain;
                DEBUG_LOG2("Gain%d 0x%02x", a + 1, last_amp[a].gain);
                midi_axe_cc(axe_cc_external3 + a, last_amp[a].gain);
                diff = 1;
            }
            if (last_amp[a].gate != 0x00) {
                last_amp[a].gate = 0x00;
                DEBUG_LOG1("Gate%d off", a + 1);
                midi_axe_cc(axe_cc_byp_gate1 + a, last_amp[a].gate);
                diff = 1;
            }
        } else if (dirty != 0) {
            // dirty:
            u8 gain = or_default(curr.amp[a].gain, pr.default_gain[a]);
            if (last_amp[a].amp_byp != 0x7F) {
                last_amp[a].amp_byp = 0x7F;
                DEBUG_LOG1("AMP%d on", a + 1);
                midi_axe_cc(axe_cc_byp_amp1 + a, last_amp[a].amp_byp);
                diff = 1;
            }
            if (last_amp[a].amp_xy != 0x7F) {
                last_amp[a].amp_xy = 0x7F;
                DEBUG_LOG1("AMP%d X", a + 1);
                midi_axe_cc(axe_cc_xy_amp1 + a, last_amp[a].amp_xy);
                diff = 1;
            }
            if (last_amp[a].cab_xy != 0x7F) {
                last_amp[a].cab_xy = 0x7F;
                DEBUG_LOG1("CAB%d X", a + 1);
                midi_axe_cc(axe_cc_xy_cab1 + a, last_amp[a].cab_xy);
                diff = 1;
            }
            if (last_amp[a].gain != gain) {
                last_amp[a].gain = gain;
                DEBUG_LOG2("Gain%d 0x%02x", a + 1, last_amp[a].gain);
                midi_axe_cc(axe_cc_external3 + a, last_amp[a].gain);
                diff = 1;
            }
            if (last_amp[a].gate != 0x7F) {
                last_amp[a].gate = 0x7F;
                DEBUG_LOG1("Gate%d on", a + 1);
                midi_axe_cc(axe_cc_byp_gate1 + a, last_amp[a].gate);
                diff = 1;
            }
        } else {
            // clean:
            if (last_amp[a].amp_byp != 0x7F) {
                last_amp[a].amp_byp = 0x7F;
                DEBUG_LOG1("AMP%d on", a + 1);
                midi_axe_cc(axe_cc_byp_amp1 + a, last_amp[a].amp_byp);
                diff = 1;
            }
            if (last_amp[a].amp_xy != 0x00) {
                last_amp[a].amp_xy = 0x00;
                DEBUG_LOG1("AMP%d Y", a + 1);
                midi_axe_cc(axe_cc_xy_amp1 + a, last_amp[a].amp_xy);
                diff = 1;
            }
            if (last_amp[a].cab_xy != 0x7F) {
                last_amp[a].cab_xy = 0x7F;
                DEBUG_LOG1("CAB%d X", a + 1);
                midi_axe_cc(axe_cc_xy_cab1 + a, last_amp[a].cab_xy);
                diff = 1;
            }
            if (last_amp[a].gain != last_amp[a].clean_gain) {
                last_amp[a].gain = last_amp[a].clean_gain;
                DEBUG_LOG2("Gain%d 0x%02x", a + 1, last_amp[a].gain);
                midi_axe_cc(axe_cc_external3 + a, last_amp[a].gain);
                diff = 1;
            }
            if (last_amp[a].gate != 0x00) {
                last_amp[a].gate = 0x00;
                DEBUG_LOG1("Gate%d off", a + 1);
                midi_axe_cc(axe_cc_byp_gate1 + a, last_amp[a].gate);
                diff = 1;
            }
        }

        if ((last_acoustc | last_dirty) != (acoustc | dirty)) {
            // Always compressor on:
            DEBUG_LOG1("Comp%d on", a + 1);
            midi_axe_cc(axe_cc_byp_compressor1 + a, 0x7F);
        }

        // Update volumes:
        if (curr.amp[a].volume != last.amp[a].volume) {
            DEBUG_LOG2("MIDI set AMP%d volume = %s", a + 1, bcd(dB_bcd_lookup[curr.amp[a].volume]));
            midi_axe_cc(axe_cc_external1 + a, (curr.amp[a].volume));
            diff = 1;
        }
    }

    // Send FX state:
    for (i = 0; i < 5; i++, test_fx <<= 1) {
        if ((curr.amp[0].fx & test_fx) != (last.amp[0].fx & test_fx)) {
            DEBUG_LOG2("MIDI set AMP1 %.4s %s", fx_name(pr.fx_midi_cc[0][i]),
                       (curr.amp[0].fx & test_fx) == 0 ? "off" : "on");
            midi_axe_cc(pr.fx_midi_cc[0][i], calc_cc_toggle(curr.amp[0].fx & test_fx));
            diff = 1;
        }
        if ((curr.amp[1].fx & test_fx) != (last.amp[1].fx & test_fx)) {
            DEBUG_LOG2("MIDI set AMP2 %.4s %s", fx_name(pr.fx_midi_cc[1][i]),
                       (curr.amp[1].fx & test_fx) == 0 ? "off" : "on");
            midi_axe_cc(pr.fx_midi_cc[1][i], calc_cc_toggle(curr.amp[1].fx & test_fx));
            diff = 1;
        }
    }

    // Send MIDI tempo change:
    if ((curr.tempo != last.tempo) && (curr.tempo >= 30)) {
        // http://forum.fractalaudio.com/threads/is-it-possible-to-set-tempo-on-the-axe-fx-ii-via-sysex.101437/
        // Example SysEx runs for tempo change on Axe-FX II:
        // F0 00 01 74 03 02 0D 01 20 00 1E 00 00 01 37 F7   =  30 BPM
        // F0 00 01 74 03 02 0D 01 20 00 78 00 00 01 51 F7   = 120 BPM
        // F0 00 01 74 03 02 0D 01 20 00 0C 01 00 01 24 F7   = 140 BPM

        // Precompute a checksum that excludes only the 2 tempo bytes:
        u8 cs = 0xF0 ^0x00 ^0x01 ^0x74 ^0x03 ^0x02 ^0x0D ^0x01 ^0x20 ^0x00 ^0x00 ^0x01;
        u8 d;

        DEBUG_LOG1("MIDI set tempo = %d bpm", curr.tempo);
        // Start the sysex command targeted at the Axe-FX II to initiate tempo change:
        midi_axe_sysex_start(0x02);
        midi_send_sysex(0x0D);
        midi_send_sysex(0x01);
        midi_send_sysex(0x20);
        midi_send_sysex(0x00);
        // Tempo value split in 2x 7-bit values:
        d = (curr.tempo & (u8) 0x7F);
        cs ^= d;
        midi_send_sysex(d);
        d = (curr.tempo >> (u8) 7);
        cs ^= d;
        midi_send_sysex(d);
        //  Finish the tempo command and send the sysex checksum and terminator:
        midi_send_sysex(0x00);
        midi_send_sysex(0x01);
        midi_axe_sysex_end(cs & (u8) 0x7F);
    }

    if (curr.amp[0].fx != last.amp[0].fx) {
        diff = 1;
    }
    if (curr.amp[1].fx != last.amp[1].fx) {
        diff = 1;
    }

    if (curr.sl_idx != last.sl_idx) {
        diff = 1;
    } else if (curr.pr_idx != last.pr_idx) {
        diff = 1;
    } else if (curr.sc_idx != last.sc_idx) {
        diff = 1;
    }

    if (curr.rowstate[0].mode != last.rowstate[0].mode) {
        diff = 1;
    }
    if (curr.rowstate[1].mode != last.rowstate[1].mode) {
        diff = 1;
    }
    if (curr.modified != last.modified) {
        diff = 1;
    }

    // Update LCD if the state changed:
    if (diff) {
        update_lcd();

#ifdef HWFEAT_REPORT
        report_build();
#endif
    }
}

#ifdef HWFEAT_LABEL_UPDATES
char tmplabel[5][5];
#endif

// Update LCD display:
static void update_lcd(void) {
#ifdef HWFEAT_LABEL_UPDATES
    const char **labels;
    u8 n;
#endif
#ifdef FEAT_LCD
    s8 i;
    u8 test_fx;
    char *d;
#endif
    DEBUG_LOG0("update LCD");
#ifdef HWFEAT_LABEL_UPDATES
    // Top row:
    labels = label_row_get(1);
    switch (curr.rowstate[0].mode) {
        case ROWMODE_AMP:
            labels[0] = "CLN/DRV|AC";
            labels[1] = "GAIN--";
            labels[2] = "GAIN++";
            labels[3] = "VOL--";
            labels[4] = "VOL++";
            labels[5] = "FX|RESET";
            break;
        case ROWMODE_FX:
            labels[0] = fx_name(pr.fx_midi_cc[0][0]);
            labels[1] = fx_name(pr.fx_midi_cc[0][1]);
            labels[2] = fx_name(pr.fx_midi_cc[0][2]);
            labels[3] = fx_name(pr.fx_midi_cc[0][3]);
            labels[4] = fx_name(pr.fx_midi_cc[0][4]);
            for (n = 0; n < 5; n++) {
                tmplabel[n][0] = labels[n][0];
                tmplabel[n][1] = labels[n][1];
                tmplabel[n][2] = labels[n][2];
                tmplabel[n][3] = labels[n][3];
                tmplabel[n][4] = 0;
                labels[n] = tmplabel[n];
            }
            labels[5] = "FX|RESET";
            break;
    }
    labels[6] = "SONG--";
    labels[7] = "SONG++";
    label_row_update(1);

    // Bottom row:
    labels = label_row_get(0);
    switch (curr.rowstate[1].mode) {
        case ROWMODE_AMP:
            labels[0] = "CLN/DRV|AC";
            labels[1] = "GAIN--";
            labels[2] = "GAIN++";
            labels[3] = "VOL--";
            labels[4] = "VOL++";
            labels[5] = "FX|RESET";
            break;
        case ROWMODE_FX:
            labels[0] = fx_name(pr.fx_midi_cc[1][0]);
            labels[1] = fx_name(pr.fx_midi_cc[1][1]);
            labels[2] = fx_name(pr.fx_midi_cc[1][2]);
            labels[3] = fx_name(pr.fx_midi_cc[1][3]);
            labels[4] = fx_name(pr.fx_midi_cc[1][4]);
            for (n = 0; n < 5; n++) {
                tmplabel[n][0] = labels[n][0];
                tmplabel[n][1] = labels[n][1];
                tmplabel[n][2] = labels[n][2];
                tmplabel[n][3] = labels[n][3];
                tmplabel[n][4] = 0;
                labels[n] = tmplabel[n];
            }
            labels[5] = "FX|RESET";
            break;
    }
    labels[6] = "TAP|MODE";
    labels[7] = "SCENE++|1";
    label_row_update(0);
#endif
#ifdef FEAT_LCD
    for (i = 0; i < LCD_COLS; ++i) {
        lcd_rows[row_song][i] = "                    "[i];
        lcd_rows[row_stat][i] = "                    "[i];
    }

    // Print setlist date:
    if (curr.setlist_mode == 0) {
        for (i = 0; i < LCD_COLS; i++) {
            lcd_rows[row_stat][i] = "Prg  0/128 Scn  0/ 0"[i];
        }

        // Show program number:
        ritoa(lcd_rows[row_stat], 5, curr.pr_idx + (u8) 1);
    } else {
        for (i = 0; i < LCD_COLS; i++) {
            lcd_rows[row_stat][i] = "Sng  0/ 0  Scn  0/ 0"[i];
        }

        // Show setlist song index:
        ritoa(lcd_rows[row_stat], 5, curr.sl_idx + (u8) 1);
        // Show setlist song count:
        ritoa(lcd_rows[row_stat], 8, sl_max + (u8) 1);
    }
    // Scene number:
    ritoa(lcd_rows[row_stat], 16, curr.sc_idx + (u8) 1);
    // Scene count:
    ritoa(lcd_rows[row_stat], 19, pr.scene_count);

    // Song name:
    if (pr.name[0] == 0) {
        // Show unnamed song index:
        for (i = 0; i < LCD_COLS; i++) {
            lcd_rows[row_song][i] = "__unnamed song #    "[i];
        }
        ritoa(lcd_rows[row_song], 18, curr.pr_idx + (u8) 1);
    } else {
        copy_str_lcd(pr.name, lcd_rows[row_song]);
    }
    // Set modified bit:
    if (curr.modified) {
        lcd_rows[row_song][19] = '*';
    }

    // AMP1:
    switch (curr.rowstate[0].mode) {
        case ROWMODE_AMP:
            for (i = 0; i < LCD_COLS; i++) {
                lcd_rows[row_amp1][i] = "1C g 0  v  0.0 -----"[i];
            }

            if ((curr.amp[0].fx & fxm_acoustc) != 0) {
                // A for acoustic
                lcd_rows[row_amp1][1] = 'A';
            } else {
                // C/D for clean/dirty
                lcd_rows[row_amp1][1] = 'C' + ((curr.amp[0].fx & fxm_dirty) != 0);
            }
            hextoa(lcd_rows[row_amp1], 5, last_amp[0].gain);
            bcdtoa(lcd_rows[row_amp1], 13, dB_bcd_lookup[curr.amp[0].volume]);

            test_fx = 1;
            d = &lcd_rows[row_amp1][15];
            for (i = 0; i < 5; i++, test_fx <<= 1) {
                rom const char *name = fx_name(pr.fx_midi_cc[0][i]);
                const u8 lowercase_enable_mask = (~(curr.amp[0].fx & test_fx) << (5 - i));
                u8 is_alpha_mask, c;

                c = *name;
                is_alpha_mask = (c & 0x40) >> 1;
                *d++ = (c & ~is_alpha_mask) | (is_alpha_mask & lowercase_enable_mask);
            }
            break;
        case ROWMODE_FX:
            for (i = 0; i < LCD_COLS; i++) {
                lcd_rows[row_amp1][i] = "                    "[i];
            }
            test_fx = 1;
            d = &lcd_rows[row_amp1][0];
            for (i = 0; i < 5; i++, test_fx <<= 1) {
                rom const char *name = fx_name(pr.fx_midi_cc[0][i]);
                u8 is_alpha_mask, c, j;

                // Select 0x20 or 0x00 depending on if FX disabled or enabled, respectively:
                const u8 lowercase_enable_mask = (~(curr.amp[0].fx & test_fx) << (5 - i));

                // Uppercase enabled fx names; lowercase disabled.
                // 0x40 is used as an alpha test; this will fail for "@[\]^_`{|}~" but none of these are present in FX names.
                // 0x40 (or 0) is shifted right 1 bit to turn it into a mask for 0x20 to act as lowercase/uppercase switch.
                for (j = 0; j < 4; j++) {
                    c = *name++;
                    is_alpha_mask = (c & 0x40) >> 1;
                    *d++ = (c & ~is_alpha_mask) | (is_alpha_mask & lowercase_enable_mask);
                }
            }
            break;
    }

    // AMP2:
    switch (curr.rowstate[1].mode) {
        case ROWMODE_AMP:
            for (i = 0; i < LCD_COLS; i++) {
                lcd_rows[row_amp2][i] = "2C g 0  v  0.0 -----"[i];
            }

            if ((curr.amp[1].fx & fxm_acoustc) != 0) {
                // A for acoustic
                lcd_rows[row_amp2][1] = 'A';
            } else {
                // C/D for clean/dirty
                lcd_rows[row_amp2][1] = 'C' + ((curr.amp[1].fx & fxm_dirty) != 0);
            }
            hextoa(lcd_rows[row_amp2], 5, last_amp[1].gain);
            bcdtoa(lcd_rows[row_amp2], 13, dB_bcd_lookup[curr.amp[1].volume]);

            test_fx = 1;
            d = &lcd_rows[row_amp2][15];
            for (i = 0; i < 5; i++, test_fx <<= 1) {
                rom const char *name = fx_name(pr.fx_midi_cc[1][i]);
                const u8 lowercase_enable_mask = (~(curr.amp[1].fx & test_fx) << (5 - i));
                u8 is_alpha_mask, c;

                c = *name;
                is_alpha_mask = (c & 0x40) >> 1;
                *d++ = (c & ~is_alpha_mask) | (is_alpha_mask & lowercase_enable_mask);
            }
            break;
        case ROWMODE_FX:
            for (i = 0; i < LCD_COLS; i++) {
                lcd_rows[row_amp2][i] = "                    "[i];
            }
            test_fx = 1;
            d = &lcd_rows[row_amp2][0];
            for (i = 0; i < 5; i++, test_fx <<= 1) {
                rom const char *name = fx_name(pr.fx_midi_cc[1][i]);
                u8 is_alpha_mask, c, j;

                // Select 0x20 or 0x00 depending on if FX disabled or enabled, respectively:
                const u8 lowercase_enable_mask = (~(curr.amp[1].fx & test_fx) << (5 - i));

                // Uppercase enabled fx names; lowercase disabled.
                // 0x40 is used as an alpha test; this will fail for "@[\]^_`{|}~" but none of these are present in FX names.
                // 0x40 (or 0) is shifted right 1 bit to turn it into a mask for 0x20 to act as lowercase/uppercase switch.
                for (j = 0; j < 4; j++) {
                    c = *name++;
                    is_alpha_mask = (c & 0x40) >> 1;
                    *d++ = (c & ~is_alpha_mask) | (is_alpha_mask & lowercase_enable_mask);
                }
            }
            break;
    }

    lcd_updated_all();
#endif
}

static void calc_gain_modified(void) {
    curr.modified = (curr.modified & ~(u8) 0x11) |
                    ((u8) (curr.amp[0].gain != origpr->scene[curr.sc_idx].amp[0].gain) << (u8) 0) |
                    ((u8) (curr.amp[1].gain != origpr->scene[curr.sc_idx].amp[1].gain) << (u8) 4);
    // DEBUG_LOG1("calc_gain_modified():   0x%02X", curr.modified);
}

static void calc_fx_modified(void) {
    curr.modified = (curr.modified & ~(u8) 0x22) |
                    ((u8) (curr.amp[0].fx != origpr->scene[curr.sc_idx].amp[0].fx) << (u8) 1) |
                    ((u8) (curr.amp[1].fx != origpr->scene[curr.sc_idx].amp[1].fx) << (u8) 5);
    // DEBUG_LOG1("calc_fx_modified():     0x%02X", curr.modified);
}

static void calc_volume_modified(void) {
    curr.modified = (curr.modified & ~(u8) 0x44) |
                    ((u8) (curr.amp[0].volume != origpr->scene[curr.sc_idx].amp[0].volume) << (u8) 2) |
                    ((u8) (curr.amp[1].volume != origpr->scene[curr.sc_idx].amp[1].volume) << (u8) 6);
    // DEBUG_LOG1("calc_volume_modified(): 0x%02X", curr.modified);
}

rom struct program *program_addr(u8 pr) {
    u16 addr = (u16) sizeof(struct set_list) + (u16) (pr * sizeof(struct program));

    return (rom struct program *) flash_addr(addr);
}

void load_program(void) {
    // Load program:
    u16 addr;
    u8 pr_num;

    if (curr.setlist_mode == 0) {
        pr_num = curr.pr_idx;
    } else {
        pr_num = sl.entries[curr.sl_idx].program;
    }

    DEBUG_LOG1("load program %d", pr_num + 1);

    addr = (u16) sizeof(struct set_list) + (u16) (pr_num * sizeof(struct program));
    flash_load(addr, sizeof(struct program), (u8 *) &pr);

    origpr = (rom struct program *) flash_addr(addr);
    curr.modified = 0;
    curr.midi_program = pr.midi_program;
    curr.tempo = pr.tempo;

    // Establish a sane default for an undefined program:
    curr.sc_idx = 0;
    // TODO: better define how an undefined program is detected.
    // For now the heuristic is if an amp's volume is non-zero. A properly initialized amp will likely
    // have a value near `volume_0dB` (98).
    if (pr.scene[0].amp[1].volume == 0) {
        scene_default();
    }

    // Trigger a scene reload:
    //last.sc_idx = ~curr.sc_idx;
}

void load_scene(void) {
    DEBUG_LOG1("load scene %d", curr.sc_idx + 1);

    // Detect if scene is uninitialized:
    if ((pr.scene[curr.sc_idx].amp[0].gain == 0) && (pr.scene[curr.sc_idx].amp[0].volume == 0) &&
        (pr.scene[curr.sc_idx].amp[1].gain == 0) && (pr.scene[curr.sc_idx].amp[1].volume == 0)) {
        // Reset to default scene state:
        //scene_default();
        pr.scene[curr.sc_idx] = pr.scene[curr.sc_idx - 1];
    }

    // Copy new scene settings into current state:
    curr.amp[0] = pr.scene[curr.sc_idx].amp[0];
    curr.amp[1] = pr.scene[curr.sc_idx].amp[1];

    // Recalculate modified status for this scene:
    curr.modified = 0;
    calc_volume_modified();
    calc_fx_modified();
    calc_gain_modified();
}

void activate_program(int pr_idx) {
    curr.pr_idx = (u8)pr_idx;
    load_program();
    load_scene();
}

void activate_song(int sl_idx) {
    curr.sl_idx = (u8)sl_idx;
    load_program();
    load_scene();
}

void get_program_name(int pr_idx, char *name) {
    struct program tmp;

    u16 addr = (u16) sizeof(struct set_list) + (u16) (pr_idx * sizeof(struct program));
    flash_load(addr, sizeof(struct program), (u8 *) &tmp);

    strncpy(name, (const char *)tmp.name, 20);
}

int get_set_list_program(int sl_idx) {
    if (sl_idx < 0 || sl_idx >= sl.count) return -1;

    return sl.entries[sl_idx].program;
}

void scene_default(void) {
    rom struct program *default_pr = program_addr(0);
    u8 a, i;

    DEBUG_LOG1("default scene %d", curr.sc_idx + 1);

    // Set defaults per amp:
    for (a = 0; a < 2; a++) {
        pr.default_gain[a] = 0x5E;
        for (i = 0; i < 5; i++) {
            pr.fx_midi_cc[a][i] = default_pr->fx_midi_cc[a][i];
        }
    }
    pr.scene_count = 1;
    pr.scene[curr.sc_idx].amp[0].gain = 0;
    pr.scene[curr.sc_idx].amp[0].fx = fxm_dirty;
    pr.scene[curr.sc_idx].amp[0].volume = volume_0dB;
    pr.scene[curr.sc_idx].amp[1].gain = 0;
    pr.scene[curr.sc_idx].amp[1].fx = fxm_dirty;
    pr.scene[curr.sc_idx].amp[1].volume = volume_0dB;
}

void toggle_setlist_mode() {
    DEBUG_LOG0("change setlist mode");
    curr.setlist_mode ^= (u8) 1;
    if (curr.setlist_mode == 1) {
        // Remap sl_idx by looking up program in setlist otherwise default to first setlist entry:
        u8 i;
        sl_max = sl.count - (u8) 1;
        curr.sl_idx = 0;
        for (i = 0; i < sl.count; i++) {
            if (sl.entries[i].program == curr.pr_idx) {
                curr.sl_idx = i;
                break;
            }
        }
    } else {
        // Lookup program number from setlist:
        sl_max = 127;
        curr.pr_idx = sl.entries[curr.sl_idx].program;
    }
}

void tap_tempo() {
    tap ^= (u8) 0x7F;
    midi_axe_cc(axe_cc_taptempo, tap);
}

void midi_invalidate() {
    // Invalidate all current MIDI state so it gets re-sent at end of loop:
    DEBUG_LOG0("invalidate MIDI state");
    last.midi_program = ~curr.midi_program;
    last.tempo = ~curr.tempo;
    last.amp[0].gain = ~curr.amp[0].gain;
    last.amp[0].fx = ~curr.amp[0].fx;
    last.amp[0].volume = ~curr.amp[0].volume;
    last.amp[1].gain = ~curr.amp[1].gain;
    last.amp[1].fx = ~curr.amp[1].fx;
    last.amp[1].volume = ~curr.amp[1].volume;
    // Initialize to something neither 0x00 or 0x7F so it gets reset:
    last_amp[0].amp_xy = 0x40;
    last_amp[0].cab_xy = 0x40;
    last_amp[0].gain = ~curr.amp[0].gain;
    last_amp[0].gate = 0x40;
    last_amp[1].amp_xy = 0x40;
    last_amp[1].cab_xy = 0x40;
    last_amp[1].gain = ~curr.amp[1].gain;
    last_amp[1].gate = 0x40;
}

void prev_scene() {
    if (curr.sc_idx > 0) {
        DEBUG_LOG0("prev scene");
        curr.sc_idx--;
    } else {
        prev_song();
    }
}

void next_scene() {
    if (curr.sc_idx < pr.scene_count - 1) {
        DEBUG_LOG0("next scene");
        curr.sc_idx++;
    } else {
        next_song();
    }
}

void reset_scene() {
    DEBUG_LOG0("reset scene");
    curr.sc_idx = 0;
}

void prev_song() {
    if (curr.setlist_mode == 0) {
        if (curr.pr_idx > 0) {
            DEBUG_LOG0("prev program");
            curr.pr_idx--;
        }
    } else {
        if (curr.sl_idx > 0) {
            DEBUG_LOG0("prev song");
            curr.sl_idx--;
        }
    }
}

void next_song() {
    if (curr.setlist_mode == 0) {
        if (curr.pr_idx < 127) {
            DEBUG_LOG0("next program");
            curr.pr_idx++;
        }
    } else {
        if (curr.sl_idx < sl_max) {
            DEBUG_LOG0("next song");
            curr.sl_idx++;
        }
    }
}

void gain_set(int amp, u8 new_gain) {
    // Determine which gain variable to adjust:
    u8 *gain;
    if ((curr.amp[amp].fx & (fxm_dirty | fxm_acoustc)) == fxm_dirty) {
        if (curr.amp[amp].gain != 0) {
            gain = &curr.amp[amp].gain;
        } else {
            gain = &pr.default_gain[amp];
        }
    } else {
        gain = &last_amp[amp].clean_gain;
    }

    if ((*gain) != new_gain) {
        (*gain) = new_gain;
        calc_gain_modified();
    }
}

void volume_set(int amp, u8 new_volume) {
    if ((curr.amp[amp].volume) != new_volume) {
        curr.amp[amp].volume = new_volume;
        calc_volume_modified();
    }
}

// set the controller to an initial state
void controller_init(void) {
    u8 i;

    tap = 0;

#ifdef HWFEAT_REPORT
    // get writable report location:
    report = report_target();
#endif

    mode = MODE_LIVE;

    last.setlist_mode = 1;
    curr.setlist_mode = 1;

    // Load setlist:
    flash_load((u16) 0, sizeof(struct set_list), (u8 *) &sl);
    sl_max = sl.count - (u8) 1;

    for (i = 0; i < 2; i++) {
        curr.amp[i].gain = 0;
        last.amp[i].gain = ~(u8) 0;
        last_amp[i].clean_gain = 0x10;
    }

    // Load first program in setlist:
    curr.sl_idx = 0;
    curr.pr_idx = 0;
    last.sl_idx = 0;
    last.pr_idx = 0;
    last.midi_program = 0x7F;
    last.tempo = 0;
    load_program();
    load_scene();
    last.sc_idx = curr.sc_idx;

    for (i = 0; i < 2; i++) {
        curr.rowstate[i].mode = ROWMODE_AMP;
        curr.rowstate[i].fx = (u8) 0;
        last.rowstate[i].mode = ~ROWMODE_AMP;
        last.rowstate[i].fx = ~(u8) 0;

        // Copy current scene settings into state:
        curr.amp[i] = pr.scene[curr.sc_idx].amp[i];

        // Invert last settings to force initial switch:
        last.amp[i].fx = ~curr.amp[i].fx;
        last.amp[i].volume = ~curr.amp[i].volume;
    }

    // Force MIDI changes on init:
    midi_invalidate();

#ifdef FEAT_LCD
    for (i = 0; i < LCD_ROWS; ++i)
        lcd_rows[i] = lcd_row_get(i);

    for (i = 0; i < LCD_COLS; ++i) {
        lcd_rows[row_amp1][i] = "                    "[i];
        lcd_rows[row_amp2][i] = "                    "[i];
        lcd_rows[row_stat][i] = "                    "[i];
        lcd_rows[row_song][i] = "                    "[i];
    }
#endif
}

// called every 10ms
void controller_10msec_timer(void) {
}

// main control loop
void controller_handle(void) {
    // poll foot-switch status:
    u16 tmp = fsw_poll();
    curr.fsw = tmp;

#define is_btn_pressed(m) ( \
    ( ((last.fsw & m) != m) && ((curr.fsw & m) == m) ) || \
    ( (curr.fsw & (m << 8u)) == (m << 8u) ) \
)

    if (is_btn_pressed(M_1)) {
        prev_song();
    }
    if (is_btn_pressed(M_2)) {
        next_song();
    }
    if (is_btn_pressed(M_3)) {
        next_scene();
    }

    // Update state:
    if ((curr.setlist_mode != last.setlist_mode) || (curr.sl_idx != last.sl_idx) || (curr.pr_idx != last.pr_idx)) {
        load_program();
        load_scene();
    } else if (curr.sc_idx != last.sc_idx) {
        // Store last state into program for recall:
        pr.scene[last.sc_idx].amp[0] = curr.amp[0];
        pr.scene[last.sc_idx].amp[1] = curr.amp[1];

        load_scene();
    }

    calc_midi();

    // Record the previous state:
    last = curr;
}
