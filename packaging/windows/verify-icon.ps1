# SPDX-FileCopyrightText: 2026 Weikang Wang
# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [Parameter(Mandatory = $true)]
    [string]$Executable
)

$ErrorActionPreference = "Stop"
$resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class MdsScopeIconNative
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr LoadLibraryEx(
        string fileName,
        IntPtr file,
        uint flags);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr FindResource(
        IntPtr module,
        string name,
        IntPtr type);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern uint SizeofResource(
        IntPtr module,
        IntPtr resource);

    [DllImport("kernel32.dll")]
    public static extern bool FreeLibrary(IntPtr module);

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    public static extern uint ExtractIconEx(
        string fileName,
        int iconIndex,
        IntPtr[] largeIcons,
        IntPtr[] smallIcons,
        uint iconCount);

    [DllImport("user32.dll")]
    public static extern bool DestroyIcon(IntPtr icon);
}
'@

$loadLibraryAsDataFile = 0x00000002
$groupIconResource = [IntPtr]14
$module = [MdsScopeIconNative]::LoadLibraryEx(
    $resolvedExecutable,
    [IntPtr]::Zero,
    $loadLibraryAsDataFile)
if ($module -eq [IntPtr]::Zero) {
    throw "Could not load resources from $resolvedExecutable"
}

try {
    $resource = [MdsScopeIconNative]::FindResource(
        $module,
        "IDI_MDSSCOPE_ICON",
        $groupIconResource)
    if ($resource -eq [IntPtr]::Zero) {
        throw "IDI_MDSSCOPE_ICON is missing from $resolvedExecutable"
    }
    if ([MdsScopeIconNative]::SizeofResource($module, $resource) -eq 0) {
        throw "IDI_MDSSCOPE_ICON is empty in $resolvedExecutable"
    }
}
finally {
    [void][MdsScopeIconNative]::FreeLibrary($module)
}

$iconCount = [MdsScopeIconNative]::ExtractIconEx(
    $resolvedExecutable,
    -1,
    $null,
    $null,
    0)
if ($iconCount -lt 1) {
    throw "Windows could not enumerate an icon from $resolvedExecutable"
}

$largeIcons = [IntPtr[]]::new(1)
$smallIcons = [IntPtr[]]::new(1)
try {
    $extracted = [MdsScopeIconNative]::ExtractIconEx(
        $resolvedExecutable,
        0,
        $largeIcons,
        $smallIcons,
        1)
    $missingIconHandles = ($largeIcons[0] -eq [IntPtr]::Zero) -and ($smallIcons[0] -eq [IntPtr]::Zero)
    if ($extracted -ne 1 -or $missingIconHandles) {
        throw "Windows could not extract an icon from $resolvedExecutable"
    }
}
finally {
    foreach ($icon in $largeIcons + $smallIcons) {
        if ($icon -ne [IntPtr]::Zero) {
            [void][MdsScopeIconNative]::DestroyIcon($icon)
        }
    }
}

Write-Host "Verified Windows icon resources in $resolvedExecutable"
