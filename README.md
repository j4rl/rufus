Rufus: The Reliable USB Formatting Utility
==========================================

[![VS2022 Build Status](https://img.shields.io/github/actions/workflow/status/pbatard/rufus/vs2026.yml?branch=master&style=flat-square&label=VS2026%20Build)](https://github.com/pbatard/rufus/actions/workflows/vs2026.yml)
[![MinGW Build Status](https://img.shields.io/github/actions/workflow/status/pbatard/rufus/mingw.yml?branch=master&style=flat-square&label=MinGW%20Build)](https://github.com/pbatard/rufus/actions/workflows/mingw.yml)
[![Coverity Scan Status](https://img.shields.io/coverity/scan/2172.svg?style=flat-square&label=Coverity%20Analysis)](https://scan.coverity.com/projects/pbatard-rufus)  
[![Latest Release](https://img.shields.io/github/release-pre/pbatard/rufus.svg?style=flat-square&label=Latest%20Release)](https://github.com/pbatard/rufus/releases)
[![Licence](https://img.shields.io/badge/license-GPLv3-blue.svg?style=flat-square&label=License)](https://www.gnu.org/licenses/gpl-3.0.en.html)
[![Download Stats](https://img.shields.io/github/downloads/pbatard/rufus/total.svg?label=Downloads&style=flat-square)](https://github.com/pbatard/rufus/releases)
[![Contributors](https://img.shields.io/github/contributors/pbatard/rufus.svg?style=flat-square&label=Contributors)](https://github.com/pbatard/rufus/graphs/contributors)

![Rufus logo](https://raw.githubusercontent.com/pbatard/rufus/master/res/icons/rufus-128.png)

Rufus is a utility that helps format and create bootable USB flash drives.

Features
--------

* Format USB, flash card and virtual drives to FAT/FAT32/NTFS/UDF/exFAT/ReFS/ext2/ext3
* Create DOS bootable USB drives using [FreeDOS](https://www.freedos.org) or MS-DOS
* Create BIOS or UEFI bootable drives, including [UEFI bootable NTFS](https://github.com/pbatard/uefi-ntfs)
* Create bootable drives from bootable ISOs (Windows, Linux, etc.)
* Create bootable drives from bootable disk images, including compressed ones
* Create Windows 11 installation drives for PCs that don't have TPM or Secure Boot
* Create [Windows To Go](https://en.wikipedia.org/wiki/Windows_To_Go) drives
* Create VHD/DD, VHDX and FFU images of an existing drive
* Create persistent Linux partitions
* Compute MD5, SHA-1, SHA-256 and SHA-512 checksums of the selected image
* Perform runtime validation of UEFI bootable media
* Improve Windows installation experience by automatically setting up OOBE parameters (local account, privacy options, etc.)
* Perform bad blocks checks, including detection of "fake" flash drives
* Download official Microsoft Windows 8, Windows 10 or Windows 11 retail ISOs
* Download [UEFI Shell](https://github.com/pbatard/UEFI-Shell) ISOs
* Modern and familiar UI, with [38 languages natively supported](https://github.com/pbatard/rufus/wiki/FAQ#What_languages_are_natively_supported_by_Rufus)
* Portable application files (WinUI builds require the Windows App Runtime).
* Portable. Secure Boot compatible.
* 100% [Free Software](https://www.gnu.org/philosophy/free-sw) ([GPL v3](https://www.gnu.org/licenses/gpl-3.0))

Compilation
-----------

Visual Studio 2026 builds use WinUI 3 for the main window. MinGW's
`configure`/`make` build continues to use the legacy Win32 interface.

#### Visual Studio

Rufus is an OSI compliant Open Source project. You are entitled to
download and use the *freely available* [Visual Studio Community Edition](https://www.visualstudio.com/vs/community/)
to build, run or develop for Rufus. As per the Visual Studio Community Edition license,
this applies regardless of whether you are an individual or a corporate user.

Install the Desktop development with C++ workload (v145), a Windows 10/11 SDK
(10.0.19041.0 or newer), and the [NuGet CLI](https://www.nuget.org/downloads).
From a developer command prompt in this directory:

```bat
nuget restore .vs\packages.config -PackagesDirectory packages
msbuild rufus.sln /m /p:Configuration=Release /p:Platform=x64
```

The solution also supports `x86` and `arm64`. Open `rufus.sln` after restoring
the packages to build or debug in Visual Studio. To build the original native
interface with no WinUI dependencies, pass `/p:UseWinUI=false` to MSBuild.

The WinUI build targets Windows 10 version 1809 or later and uses the native
components of **Windows App SDK 2.5.1**. Install the matching architecture of
the [Windows App Runtime 2.5.1 or newer 2.x runtime](https://learn.microsoft.com/windows/apps/windows-app-sdk/downloads).
Distribute `rufus.exe` **together with** `Microsoft.WindowsAppRuntime.Bootstrap.dll`
from the build output directory. Copying only the executable is insufficient.
The runtime is initialized explicitly after Rufus configures its DLL search
policy; a missing runtime produces an error instead of silently switching UI.

`src/winui.cpp` hosts a [WinUI 3 XAML Island](https://learn.microsoft.com/windows/apps/desktop/modernize/host-controls-existing-desktop-apps)
in the existing main window. Its controls use the existing localized strings,
device enumeration, validation, formatting and cancellation handlers. The
native controls remain as the state bridge, with their drawing regions hidden.
Log, settings, file pickers and confirmation dialogs still use the existing
native implementations. This is a migration of the main window; auxiliary
dialogs have not yet been converted to WinUI.

The WinUI view is written in C++/WinRT without compiled XAML. The build generates
the required projections from the pinned NuGet metadata and copies the runtime
bootstrap DLL. `Makefile.in` is generated for the MinGW build and does not
configure WinUI.

Additional information
----------------------

Rufus provides extensive information about what it is doing, either through its
easily accessible log, or through the [Windows debug facility](https://docs.microsoft.com/en-us/sysinternals/downloads/debugview).

* [__Official Website__](https://rufus.ie)
* [FAQ](https://github.com/pbatard/rufus/wiki/FAQ)
* [Security and safety measures](https://github.com/pbatard/rufus/wiki/Security)

Enhancements/Bugs
-----------------

Please use the [GitHub issue tracker](https://github.com/pbatard/rufus/issues)
for reporting problems or suggesting new features.
