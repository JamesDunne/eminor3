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
*/

#include <string.h>
#include "util.h"

#include "program-v5.h"
#include "hardware.h"

// Hard-coded MIDI channel #s:
#define gmaj_midi_channel    0
#define rjm_midi_channel     1
#define axe_midi_channel     2

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

// Tap tempo CC value (toggles between 0x00 and 0x7F):
u8 tap;

// Max program #:
u8 sl_max;
// Pointer to unmodified program:
rom struct program *origpr;

// Structure to represent state that should be compared from current to last to detect changes in program.
struct state {
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

    // Whether current program is modified in any way:
    u8 modified;
};

// Current and last state:
struct state curr, last;
struct {
    u8 amp_byp, amp_xy, cab_xy;
    u8 gain, clean_gain, gate;
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

// Fill in a report structure with latest controller data:
void report_fill(struct report *report) {
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

        report->amp[i].gain[AMP_TONE_DIRTY] = last_amp[i].gain;
        report->amp[i].gain[AMP_TONE_CLEAN] = last_amp[i].clean_gain;
        // TODO: unify on AMP_TONE_* enums

        report->amp[i].volume = curr.amp[i].volume;

        // Copy FX settings:
        u8 test_fx = 1;
        for (int f = 0; f < FX_COUNT; f++, test_fx <<= 1) {
            report->amp[i].fx_enabled[f] = (curr.amp[i].fx & test_fx) == test_fx;
            report->amp[i].fx_midi_cc[f] = pr.fx_midi_cc[i][f];
        }
    }
}

// MIDI is sent at a fixed 3,125 bytes/sec transfer rate; 56 bytes takes 175ms to complete.
// calculate the difference from last MIDI state to current MIDI state and send the difference as MIDI commands:
static bool calc_midi(void) {
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

    if (curr.modified != last.modified) {
        diff = 1;
    }

    return diff;
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
        // Copy current scene settings into state:
        curr.amp[i] = pr.scene[curr.sc_idx].amp[i];

        // Invert last settings to force initial switch:
        last.amp[i].fx = ~curr.amp[i].fx;
        last.amp[i].volume = ~curr.amp[i].volume;
    }

    // Force MIDI changes on init:
    midi_invalidate();
}

bool controller_update(void) {
    bool updated;

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

    updated = calc_midi();

    // Record the previous state:
    last = curr;

    return updated;
}
