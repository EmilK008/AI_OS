/* ===========================================================================
 * PC Speaker Driver
 * Uses PIT Channel 2 to generate tones through the PC speaker
 * =========================================================================== */
#include "speaker.h"
#include "io.h"
#include "timer.h"

#define PIT_FREQ 1193180

void speaker_on(uint32_t frequency) {
    if (frequency == 0) return;

    uint32_t divisor = PIT_FREQ / frequency;
    if (divisor > 0xFFFF) divisor = 0xFFFF;
    if (divisor == 0) divisor = 1;

    /* Step 1: Enable PIT Channel 2 gate (bit 0 of port 0x61) */
    uint8_t port61 = inb(0x61);
    outb(0x61, port61 | 0x01);
    io_wait();

    /* Step 2: Program PIT Channel 2 for square wave mode */
    outb(0x43, 0xB6);   /* Channel 2, lobyte/hibyte, mode 3, binary */
    io_wait();

    /* Step 3: Load frequency divisor (low byte first, then high byte) */
    outb(0x42, (uint8_t)(divisor & 0xFF));
    io_wait();
    outb(0x42, (uint8_t)((divisor >> 8) & 0xFF));
    io_wait();

    /* Step 4: Enable speaker output (bit 1 of port 0x61) */
    port61 = inb(0x61);
    outb(0x61, port61 | 0x03);
}

void speaker_off(void) {
    /* Clear bits 0 and 1 of port 0x61 */
    uint8_t port61 = inb(0x61);
    outb(0x61, port61 & 0xFC);
}

void speaker_beep(uint32_t freq, uint32_t duration_ticks) {
    speaker_on(freq);
    timer_wait(duration_ticks);
    speaker_off();
}

/* Play a musical note: C D E F G A B, octave 3-6 */
void speaker_play_note(char note, int octave, uint32_t duration_ticks) {
    /* Base frequencies for octave 4 */
    static const uint16_t base_freq[] = {
        /* A    B    C    D    E    F    G */
        440, 494, 262, 294, 330, 349, 392
    };

    int idx;
    if (note >= 'A' && note <= 'G') idx = note - 'A';
    else if (note >= 'a' && note <= 'g') idx = note - 'a';
    else { timer_wait(duration_ticks); return; } /* rest */

    uint32_t freq = base_freq[idx];

    /* Adjust for octave (4 is base) */
    if (octave < 4) {
        for (int i = 0; i < 4 - octave; i++) freq >>= 1;
    } else if (octave > 4) {
        for (int i = 0; i < octave - 4; i++) freq <<= 1;
    }

    speaker_beep(freq, duration_ticks);
}
