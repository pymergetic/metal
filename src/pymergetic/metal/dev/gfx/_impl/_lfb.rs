//! Iron fallback — chunked shadow → LFB memcpy (no GPU flip).

use crate::scanout::{self, Bind, Ops, CAP_CHUNKED};

static mut JOB_LIVE: i32 = 0;
static mut JOB_X: i32 = 0;
static mut JOB_Y: i32 = 0;
static mut JOB_W: i32 = 0;
static mut JOB_H: i32 = 0;
static mut JOB_ROW: i32 = 0;
static mut JOB_BAND: i32 = 64;

fn probe(b: &Bind) -> i32 {
    if b.fb.is_null() || b.owned == 0 {
        return -1;
    }
    unsafe {
        JOB_LIVE = 0;
        JOB_BAND = 64;
    }
    0
}

fn present_rect(x: i32, y: i32, w: i32, h: i32) -> i32 {
    let b = scanout::bind_info();
    if b.fb.is_null() {
        return -1;
    }
    scanout::copy_rect(b.fb, b.fb_ppsl, x, y, w, h, b);
    0
}

fn job_begin(x: i32, y: i32, w: i32, h: i32) -> i32 {
    let b = scanout::bind_info();
    if b.fb.is_null() {
        return -1;
    }
    if h < 96 {
        return if present_rect(x, y, w, h) == 0 { 0 } else { -1 };
    }
    unsafe {
        JOB_X = x;
        JOB_Y = y;
        JOB_W = w;
        JOB_H = h;
        JOB_ROW = 0;
        JOB_BAND = if 64 > h { h } else { 64 };
        JOB_LIVE = 1;
    }
    1
}

fn job_step() -> i32 {
    unsafe {
        if JOB_LIVE == 0 {
            return 0;
        }
        let b = scanout::bind_info();
        if b.fb.is_null() {
            JOB_LIVE = 0;
            return -1;
        }
        let mut band = JOB_BAND;
        if band < 16 {
            band = 16;
        }
        if JOB_ROW + band > JOB_H {
            band = JOB_H - JOB_ROW;
        }
        if band <= 0 {
            JOB_LIVE = 0;
            return 0;
        }
        let y = JOB_Y + JOB_ROW;
        scanout::copy_rect(b.fb, b.fb_ppsl, JOB_X, y, JOB_W, band, b);
        JOB_ROW += band;
        if JOB_ROW >= JOB_H {
            JOB_LIVE = 0;
            return 0;
        }
        1
    }
}

fn caps() -> u32 {
    CAP_CHUNKED
}

fn fini() {
    unsafe {
        JOB_LIVE = 0;
    }
}

pub static OPS: Ops = Ops {
    name: "lfb_copy",
    probe,
    present_rect,
    job_begin,
    job_step,
    caps,
    adopt_shadow: None,
    after_flip: None,
    fini,
};
