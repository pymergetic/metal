# deploy/ — PXE / ops (not the µPy port)

| Path | Role |
|------|------|
| [`bootserver/`](bootserver/) | iPXE NBP build, `metal.ipxe` template, QEMU PXE lab |
| [`upload-bootserver`](upload-bootserver) | Push NBP + cfg to OpenWrt/SSH TFTP |

Product images stay on **metal-cdn**. This tree never hosts the firmware blob.

```bash
./deploy/bootserver/build-nbp.sh
./deploy/upload-bootserver
./deploy/bootserver/qemu-pxe.sh
```
