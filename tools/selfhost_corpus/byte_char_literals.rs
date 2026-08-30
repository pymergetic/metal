/* b'x' is an integer (uint8_t), not a pointer — assigning it into a char
 * must not fight the `const char *` string-literal inference. */
pub fn classify(c: u8) -> u8 {
    let q = b'q';
    let nl = b'\n';
    if c == q {
        1
    } else if c == nl {
        2
    } else {
        0
    }
}

pub fn punct(s: *const u8) -> bool {
    let c = unsafe { *s };
    c == b'(' || c == b')' || c == b'[' || c == b']' || c == b'{'
}