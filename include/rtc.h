#ifndef RTC_H
#define RTC_H

#include "types.h"

struct rtc_time {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;
    uint8_t month;
    uint8_t year;     /* 0-99 */
    uint8_t century;  /* 19 or 20 */
    uint8_t weekday;  /* 1=Sun, 7=Sat (may be unreliable) */
};

void rtc_init(void);
void rtc_read(struct rtc_time *t);

/* Convenience: format "HH:MM:SS" into buf (needs 12 bytes for 12h mode with AM/PM) */
void rtc_format_time(char *buf);

/* Convenience: format "YYYY-MM-DD" into buf (needs 11 bytes) */
void rtc_format_date(char *buf);

/* Clock format: true = 24h (default), false = 12h AM/PM */
void rtc_set_24h(bool use_24h);
bool rtc_get_24h(void);

#endif
