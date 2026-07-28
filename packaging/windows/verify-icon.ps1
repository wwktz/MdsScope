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

Add-Type -AssemblyName System.Drawing
$icon = [System.Drawing.Icon]::ExtractAssociatedIcon($resolvedExecutable)
if ($null -eq $icon) {
    throw "Windows could not extract an associated icon from $resolvedExecutable"
}

$bitmap = $icon.ToBitmap()
try {
    $hasOrange = $false
    for ($y = 0; $y -lt $bitmap.Height -and !$hasOrange; ++$y) {
        for ($x = 0; $x -lt $bitmap.Width; ++$x) {
            $pixel = $bitmap.GetPixel($x, $y)
            $isOrange = $pixel.A -gt 0 -and $pixel.R -ge 180 -and $pixel.G -ge 50 -and $pixel.G -le 180 -and $pixel.B -le 100
            if ($isOrange) {
                $hasOrange = $true
                break
            }
        }
    }
    if (!$hasOrange) {
        throw "Windows extracted a fallback icon instead of the MdsScope icon from $resolvedExecutable"
    }
}
finally {
    $bitmap.Dispose()
    $icon.Dispose()
}

Write-Host "Verified Windows icon resources in $resolvedExecutable"
