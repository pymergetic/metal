# Metal's own time (no CPython float seconds anywhere in this build --
# MICROPY_PY_BUILTINS_FLOAT is off, see mpconfigport.h -- every value here
# is an int: ms/us/whole seconds, never a fractional second). Backed by
# pymergetic.metal.time.* -> the real wall clock (EFI's gRT->GetTime() /
# BIOS's CMOS RTC, refined by SNTP -- dev/net/ntp.c, dev/random/random.c)
# and the monotonic TSC clock. This is the whole surface datetime.py (and
# anything else importing time) is written against.

import pymergetic.metal.time as _time

_DAYS_IN_MONTH = (31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)
_EPOCH_WDAY = 3  # 1970-01-01 was a Thursday; Mon=0..Sun=6 (CPython convention)


def _is_leap(y):
    return y % 4 == 0 and (y % 100 != 0 or y % 400 == 0)


def _days_in_month(y, m):
    if m == 2 and _is_leap(y):
        return 29
    return _DAYS_IN_MONTH[m - 1]


def _days_from_ymd(y, m, d):
    days = 0
    if y >= 1970:
        for yy in range(1970, y):
            days += 366 if _is_leap(yy) else 365
    else:
        for yy in range(y, 1970):
            days -= 366 if _is_leap(yy) else 365
    for mm in range(1, m):
        days += _days_in_month(y, mm)
    days += d - 1
    return days


def _ymd_from_days(days):
    y = 1970
    if days >= 0:
        while True:
            n = 366 if _is_leap(y) else 365
            if days < n:
                break
            days -= n
            y += 1
    else:
        while days < 0:
            y -= 1
            days += 366 if _is_leap(y) else 365
    m = 1
    while days >= _days_in_month(y, m):
        days -= _days_in_month(y, m)
        m += 1
    return (y, m, days + 1)


def _struct_time(secs):
    days, rem = divmod(secs, 86400)
    if rem < 0:
        rem += 86400
        days -= 1
    y, m, d = _ymd_from_days(days)
    hh, rem2 = divmod(rem, 3600)
    mm, ss = divmod(rem2, 60)
    wday = (days + _EPOCH_WDAY) % 7
    yday = _days_from_ymd(y, m, d) - _days_from_ymd(y, 1, 1) + 1
    return (y, m, d, hh, mm, ss, wday, yday, 0)


def time():
    return _time.realtime_ms() // 1000


def time_ns():
    return _time.realtime_ms() * 1000000


def gmtime(secs=None):
    if secs is None:
        secs = time()
    return _struct_time(secs)


def localtime(secs=None):
    if secs is None:
        secs = time()
    return _struct_time(secs + _time.tz_minutes() * 60)


def mktime(t):
    days = _days_from_ymd(t[0], t[1], t[2])
    secs = days * 86400 + t[3] * 3600 + t[4] * 60 + t[5]
    return secs - _time.tz_minutes() * 60


def monotonic():
    return _time.mono_us() // 1000000


def monotonic_ns():
    return _time.mono_us() * 1000


perf_counter = monotonic
perf_counter_ns = monotonic_ns


def sleep(secs):
    # No floats in this build: whole seconds only. Use sleep_ms() directly
    # for sub-second delays.
    _time.sleep_ms(secs * 1000)


def sleep_ms(ms):
    _time.sleep_ms(ms)


def sleep_us(us):
    _time.sleep_ms((us + 999) // 1000)
