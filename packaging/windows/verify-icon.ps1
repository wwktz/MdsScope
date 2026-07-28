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
        IntPtr name,
        IntPtr type);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern uint SizeofResource(
        IntPtr module,
        IntPtr resource);

    [DllImport("kernel32.dll")]
    public static extern bool FreeLibrary(IntPtr module);

    [DllImport("shell32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    public static extern IntPtr ExtractIconW(
        IntPtr instance,
        string fileName,
        uint iconIndex);

    [DllImport("user32.dll")]
    public static extern bool DestroyIcon(IntPtr icon);
}
'@

$loadLibraryAsDataFile = 0x00000002
$iconResource = [IntPtr]1
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
        $iconResource,
        $groupIconResource)
    if ($resource -eq [IntPtr]::Zero) {
        throw "Icon resource 1 is missing from $resolvedExecutable"
    }
    if ([MdsScopeIconNative]::SizeofResource($module, $resource) -eq 0) {
        throw "Icon resource 1 is empty in $resolvedExecutable"
    }
}
finally {
    [void][MdsScopeIconNative]::FreeLibrary($module)
}

$icon = [MdsScopeIconNative]::ExtractIconW(
    [IntPtr]::Zero,
    $resolvedExecutable,
    0)
try {
    if ($icon -eq [IntPtr]::Zero -or $icon -eq [IntPtr]1) {
        throw "Windows could not extract an icon from $resolvedExecutable"
    }
}
finally {
    if ($icon -ne [IntPtr]::Zero -and $icon -ne [IntPtr]1) {
        [void][MdsScopeIconNative]::DestroyIcon($icon)
    }
}

Write-Host "Verified Windows icon resources in $resolvedExecutable"
