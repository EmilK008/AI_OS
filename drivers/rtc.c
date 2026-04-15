/* ===========================================================================
 * RTC Driver - CMOS Real-Time Clock
 * Reads time/date from the MC146818 compatible RTC via ports 0x70/0x71
 * =========================================================================== */
#include "rtc.h"
#include "io.h"

#define CMOS_ADDR  0x70
#define CMOS_DATA  0x71

/* CMOS register indices */
#define RTC_SECONDS   0x00
#define RTC_MINUTES   0x02
#define RTC_HOURS     0x04
#define RTC_WEEKDAY   0x06
#define RTC_DAY       0x07
#define RTC_MONTH     0x08
#define RTC_YEAR      0x09
#define RTC_CENTURY   0x32
#define RTC_STATUS_A  0x0A
#define RTC_STATUS_B  0x0B

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    io_wait();
    return inb(CMOS_DATA);
}

static bool rtc_updating(void) {
    /* Status Register A bit 7: update-in-progress flag */
    return (cmos_read(RTC_STATUS_A) & 0x80) != 0;
}

static uint8_t bcd_to_bin(uint8_t bcd) {
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

void rtc_init(void) {
    /* Nothing needed — CMOS RTC runs independently */
}

void rtc_read(struct rtc_time *t) {
    /* Wait for any in-progress update to finish */
    while (rtc_updating());

    /* Read first snapshot */
    uint8_t sec  = cmos_read(RTC_SECONDS);
    uint8_t min  = cmos_read(RTC_MINUTES);
    uint8_t hr   = cmos_read(RTC_HOURS);
    uint8_t day  = cmos_read(RTC_DAY);
    uint8_t mon  = cmos_read(RTC_MONTH);
    uint8_t yr   = cmos_read(RTC_YEAR);
    uint8_t cent = cmos_read(RTC_CENTURY);
    uint8_t wday = cmos_read(RTC_WEEKDAY);

    /* Read again and compare to avoid split-second inconsistency */
    uint8_t sec2, min2, hr2, day2, mon2, yr2;
    do {
        sec2 = sec; min2 = min; hr2 = hr;
        day2 = day; mon2 = mon; yr2 = yr;

        while (rtc_updating());
        sec  = cmos_read(RTC_SECONDS);
        min  = cmos_read(RTC_MINUTES);
        hr   = cmos_read(RTC_HOURS);
        day  = cmos_read(RTC_DAY);
        mon  = cmos_read(RTC_MONTH);
        yr   = cmos_read(RTC_YEAR);
        cent = cmos_read(RTC_CENTURY);
        wday = cmos_read(RTC_WEEKDAY);
    } while (sec != sec2 || min != min2 || hr != hr2 ||
             day != day2 || mon != mon2 || yr != yr2);

    /* Check if values are BCD (Status Register B bit 2 = 0 means BCD) */
    uint8_t status_b = cmos_read(RTC_STATUS_B);
    bool is_bcd = !(status_b & 0x04);

    if (is_bcd) {
        sec  = bcd_to_bin(sec);
        min  = bcd_to_bin(min);
        hr   = bcd_to_bin(hr & 0x7F); /* mask off PM bit if 12-hour */
        day  = bcd_to_bin(day);
        mon  = bcd_to_bin(mon);
        yr   = bcd_to_bin(yr);
        cent = bcd_to_bin(cent);
    }

    /* Handle 12-hour mode (Status Register B bit 1 = 0 means 12-hour) */
    if (!(status_b & 0x02)) {
        bool pm = (hr & 0x80) != 0;
        hr &= 0x7F;
        if (is_bcd) {
            /* already converted above */
        }
        if (hr == 12) hr = 0;
        if (pm) hr += 12;
    }

    /* Default century to 20 if CMOS century register returned 0 */
    if (cent == 0) cent = 20;

    t->seconds = sec;
    t->minutes = min;
    t->hours   = hr;
    t->day     = day;
    t->month   = mon;
    t->year    = yr;
    t->century = cent;
    t->weekday = wday;
}

void rtc_format_time(char *buf) {
    struct rtc_time t;
    rtc_read(&t);
    buf[0] = '0' + (t.hours / 10);
    buf[1] = '0' + (t.hours % 10);
    buf[2] = ':';
    buf[3] = '0' + (t.minutes / 10);
    buf[4] = '0' + (t.minutes % 10);
    buf[5] = ':';
    buf[6] = '0' + (t.seconds / 10);
    buf[7] = '0' + (t.seconds % 10);
    buf[8] = '\0';
}

void rtc_format_date(char *buf) {
    struct rtc_time t;
    rtc_read(&t);
    uint16_t full_year = t.century * 100 + t.year;
    buf[0] = '0' + (full_year / 1000);
    buf[1] = '0' + ((full_year / 100) % 10);
    buf[2] = '0' + ((full_year / 10) % 10);
    buf[3] = '0' + (full_year % 10);
    buf[4] = '-';
    buf[5] = '0' + (t.month / 10);
    buf[6] = '0' + (t.month % 10);
    buf[7] = '-';
    buf[8] = '0' + (t.day / 10);
    buf[9] = '0' + (t.day % 10);
    buf[10] = '\0';
}
