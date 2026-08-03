//! tests.wasm_guest_audio — W16.3d: null audio open/queue/drain/backend.
#![no_std]

const FMT_S16LE_STEREO_22050: u32 = 1;

#[link(wasm_import_module = "pymergetic.metal.log")]
extern "C" {
    fn pm_metal_log(line: *const u8);
}

#[link(wasm_import_module = "pymergetic.metal.dev.audio")]
extern "C" {
    fn pm_metal_dev_audio_ready() -> i32;
    fn pm_metal_dev_audio_open(format: u32, frames_buffered: u32) -> u32;
    fn pm_metal_dev_audio_close(s: u32);
    fn pm_metal_dev_audio_queue(s: u32, pcm: u32, nbytes: u32) -> u32;
    fn pm_metal_dev_audio_drain(s: u32, nbytes: u32) -> u32;
    fn pm_metal_dev_audio_mute(on: i32);
    fn pm_metal_dev_audio_muted() -> i32;
    fn pm_metal_dev_audio_volume_set(pct: u32);
    fn pm_metal_dev_audio_volume_get() -> u32;
    fn pm_metal_dev_audio_backend(out: u32, out_cap: u32) -> i32;
}

#[no_mangle]
pub extern "C" fn ready() -> i32 {
    unsafe {
        if pm_metal_dev_audio_ready() == 0 {
            return -1;
        }
        let s = pm_metal_dev_audio_open(FMT_S16LE_STEREO_22050, 512);
        if s == 0 {
            return -2;
        }
        let mut pcm = [0i16; 4];
        pcm[0] = 100;
        pcm[1] = -100;
        let nbytes = (pcm.len() * 2) as u32;
        if pm_metal_dev_audio_queue(s, pcm.as_ptr() as u32, nbytes) != nbytes {
            pm_metal_dev_audio_close(s);
            return -3;
        }
        let h = pm_metal_dev_audio_drain(s, nbytes);
        if h == 0 {
            pm_metal_dev_audio_close(s);
            return -4;
        }
        pm_metal_dev_audio_mute(1);
        if pm_metal_dev_audio_muted() == 0 {
            pm_metal_dev_audio_close(s);
            return -5;
        }
        pm_metal_dev_audio_mute(0);
        pm_metal_dev_audio_volume_set(50);
        if pm_metal_dev_audio_volume_get() != 50 {
            pm_metal_dev_audio_close(s);
            return -6;
        }
        let mut name = [0u8; 8];
        if pm_metal_dev_audio_backend(name.as_mut_ptr() as u32, 8) != 0 {
            pm_metal_dev_audio_close(s);
            return -7;
        }
        if name[0] != b'n' || name[1] != b'u' || name[2] != b'l' || name[3] != b'l' {
            pm_metal_dev_audio_close(s);
            return -8;
        }
        pm_metal_dev_audio_close(s);
        pm_metal_log(b"guest audio ok\0".as_ptr());
    }
    0
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
