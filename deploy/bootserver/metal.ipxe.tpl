#!ipxe
# MetalPython netboot — boot server holds NBP + this script only.
# Image always from CDN (METAL_PXE_CDN_URL). Optional lab-image is last-resort
# fallback if you parked a file with upload-bootserver --image (not the normal path).
#
# Architecture gating (two layers):
#   1) DHCP option 93 (client arch) picks the first-stage NBP
#      (undionly.kpxe vs ipxe.efi) — see dnsmasq.metal.conf.example
#   2) This script picks the CDN arch seat via ${platform} + ${arch}:
#      package pymergetic.metal.arch.<arch>
#      ${buildarch} = arch iPXE was built for (i386 NBP often on x86_64 CPUs)
#      cpuid --ext 29 → promote i386 → x86_64 when long mode exists
#      ${platform} = pcbios | efi
#
# Never bake 127.0.0.1 — PXE clients cannot reach the build host loopback.

echo MetalPython via iPXE
isset ${next-server} || set next-server __NEXT_SERVER__
set cdn-url __CDN_URL__
set lab-image __IMAGE_URL__
set lab-efi __IMAGE_EFI_URL__

set arch ${buildarch}
# i386 NBP + long mode → x86_64 seat; plain i386 → x86 (i686) seat.
iseq ${arch} i386 && cpuid --ext 29 && set arch x86_64 ||
iseq ${arch} i386 && set arch x86 ||

echo platform=${platform} arch=${arch} cdn=${cdn-url}

iseq ${platform} efi && goto boot-efi || goto boot-bios

:boot-efi
echo image ${cdn-url}/artifacts/lead/pymergetic.metal.arch.${arch}.efi
chain ${cdn-url}/artifacts/lead/pymergetic.metal.arch.${arch}.efi || goto fallback-efi
boot || goto fail

:boot-bios
echo image ${cdn-url}/artifacts/lead/pymergetic.metal.arch.${arch}.elf
kernel ${cdn-url}/artifacts/lead/pymergetic.metal.arch.${arch}.elf || goto fallback-bios
boot || goto fail

:fallback-efi
echo CDN miss — optional lab mirror
chain ${lab-efi} || chain http://${next-server}:8080/metal.efi || goto fail
boot || goto fail

:fallback-bios
echo CDN miss — optional lab mirror
kernel ${lab-image} || kernel http://${next-server}:8080/metal.elf || goto fail
boot || goto fail

:fail
echo MetalPython boot failed — check CDN / network
shell
