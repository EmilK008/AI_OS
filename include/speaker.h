#ifndef SPEAKER_H
#define SPEAKER_H

#include "types.h"

void speaker_on(uint32_t frequency);
void speaker_off(void);
void speaker_beep(uint32_t freq, uint32_t duration_ticks);
void speaker_play_note(char note, int octave, uint32_t duration_ticks);

/* Standard note frequencies */
#define NOTE_C4  262
#define NOTE_CS4 277
#define NOTE_D4  294
#define NOTE_DS4 311
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_FS4 370
#define NOTE_G4  392
#define NOTE_GS4 415
#define NOTE_A4  440
#define NOTE_AS4 466
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_D5  587
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_G5  784
#define NOTE_A5  880
#define NOTE_B5  988
#define NOTE_C6  1047

#endif
