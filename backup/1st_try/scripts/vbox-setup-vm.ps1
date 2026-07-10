# Configure a VirtualBox VM to boot pymetal with OVMF (avoids VBox built-in EFI CpuDxe #GP).
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$VmName,
    [Parameter(Position = 1)]
    [string]$Disk = ""
)

$ErrorActionPreference = "Stop"

function Find-VBoxManage {
    if (Get-Command VBoxManage -ErrorAction SilentlyContinue) {
        return (Get-Command VBoxManage).Source
    }
    $candidates = @(
        "${env:ProgramFiles}\Oracle\VirtualBox\VBoxManage.exe",
        "${env:ProgramFiles(x86)}\Oracle\VirtualBox\VBoxManage.exe"
    )
    foreach ($path in $candidates) {
        if (Test-Path $path) { return $path }
    }
    throw "VBoxManage not found. Add VirtualBox to PATH or install Oracle VM VirtualBox."
}

function Find-OvmfCode {
    if ($env:OVMF_CODE -and (Test-Path $env:OVMF_CODE)) {
        return (Resolve-Path $env:OVMF_CODE).Path
    }
    $candidates = @(
        "${env:ProgramFiles}\qemu\share\edk2-x86_64-code.fd",
        "${env:ProgramFiles}\qemu\share\OVMF_CODE.fd",
        "${env:ProgramFiles}\edk2\OVMF_CODE.fd",
        "${env:ProgramFiles}\edk2\OVMF_CODE_4M.fd",
        "${env:LOCALAPPDATA}\pymetal\OVMF_CODE_4M.fd"
    )
    foreach ($path in $candidates) {
        if (Test-Path $path) { return (Resolve-Path $path).Path }
    }
    return $null
}

$VBoxManage = Find-VBoxManage
$OvmfCode = Find-OvmfCode

if (-not $OvmfCode) {
    Write-Error @"
OVMF firmware not found.

Windows does not ship OVMF by default. Options:
  1. Install QEMU for Windows (includes edk2-x86_64-code.fd), then re-run this script
  2. Download OVMF_CODE_4M.fd from EDK2/OVMF releases and set:
       `$env:OVMF_CODE = 'C:\path\to\OVMF_CODE_4M.fd'
  3. Use QEMU on the host instead (reference path that already boots)

Example after installing QEMU:
  .\scripts\vbox-setup-vm.ps1 pymetal runtime\zephyr\images\pymetal-zephyr-efi.vdi
"@
}

& $VBoxManage showvminfo $VmName | Out-Null

& $VBoxManage modifyvm $VmName --firmware efi
& $VBoxManage modifyvm $VmName --memory 512
& $VBoxManage modifyvm $VmName --cpus 1
& $VBoxManage modifyvm $VmName --chipset ich9
& $VBoxManage modifyvm $VmName --ioapic on
& $VBoxManage setextradata $VmName "VBoxInternal/Devices/efi/0/Config/EfiRom" $OvmfCode

if ($Disk) {
    $Disk = (Resolve-Path $Disk).Path
    if (-not (Test-Path $Disk)) {
        throw "missing disk image: $Disk"
    }
    $info = & $VBoxManage showvminfo $VmName --machinereadable
    if ($info -notmatch 'storagecontrollername0="SATA"') {
        & $VBoxManage storagectl $VmName --name SATA --add sata --controller IntelAhci --portcount 2
    }
    try {
        & $VBoxManage storageattach $VmName --storagectl SATA --port 0 --device 0 `
            --type hdd --medium $Disk | Out-Null
    } catch {
        # already attached
    }
}

Write-Host "configured $VmName"
Write-Host "  firmware : OVMF ($OvmfCode)"
Write-Host "  memory   : 512 MiB"
Write-Host "  cpus     : 1"
Write-Host "  chipset  : ich9"
if ($Disk) { Write-Host "  disk     : $Disk" }
Write-Host ""
Write-Host "Power off the VM fully before first boot with OVMF."
Write-Host "Serial console: Settings > Ports > COM1 > host pipe or file."
Write-Host ""
Write-Host "If CpuDxe #GP persists:"
Write-Host "  - Update VirtualBox to the latest 7.x build"
Write-Host "  - System > Acceleration: try toggling Hyper-V / nested paging"
Write-Host "  - Or boot with QEMU+OVMF on Windows (same .img works)"
