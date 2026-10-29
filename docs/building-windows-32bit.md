# Building for 32-bit Windows (Windows 7 and later)

These steps build Acorn Disk Wrangler and CP/M Disk Wrangler as 32-bit Windows programs that run on Windows 7 SP1
and later, with Qt 5.15.2 and MinGW 8.1. You build on a Windows 10 or 11 PC and copy the result to the older
machine. The official releases are 64-bit only (see [building.md](building.md)), so this is the route for older
or 32-bit PCs. **This toolchain has not been tried with these programs yet: please report how it goes.**

(This file is the same in both repositories.)

## What you need
* **A Windows 10 or 11 PC** to build on (64-bit is fine; the result is still 32-bit). Building on Windows 7
  itself is harder, because current Python and Git no longer install there.
* **Python 3** from [python.org](https://www.python.org/downloads/windows/). Tick *Add python.exe to PATH* in
  the installer. It is only used to download Qt.
* **Git for Windows** from [git-scm.com](https://git-scm.com/download/win). It fetches the source and gives the
  build its version number (Help > About).
* About 3 GB of free disk space.

Use an ordinary Command Prompt (`cmd`) for everything below, not PowerShell, and not an MSYS2 shell.

## Install the compiler and Qt
Qt's own installer now hides Qt 5, so use [aqtinstall](https://aqtinstall.readthedocs.io/), which downloads Qt's
official 32-bit MinGW build and its matching compiler into `C:\Qt`. No Qt account is needed.

```
python -m pip install aqtinstall
python -m aqt install-qt windows desktop 5.15.2 win32_mingw81 --outputdir C:\Qt
python -m aqt install-tool windows desktop tools_mingw81 qt.tools.win32_mingw810 --outputdir C:\Qt
```

You should then have `C:\Qt\5.15.2\mingw81_32\bin\qmake.exe` and `C:\Qt\Tools\mingw810_32\bin\mingw32-make.exe`.
If a name has changed, `python -m aqt list-qt windows desktop --arch 5.15.2` and
`python -m aqt list-tool windows desktop` show what is available.

The Qt build and the compiler must match: both 32-bit, both MinGW 8.1. Do not mix in an MSYS2 or Qt 6 compiler.

## Build
1. Put the 32-bit Qt and compiler first on the PATH. This lasts only for this Command Prompt window, so repeat
   it in any new one:

   ```
   set PATH=C:\Qt\5.15.2\mingw81_32\bin;C:\Qt\Tools\mingw810_32\bin;%PATH%
   ```

2. Get the source into a working folder:

   ```
   mkdir C:\src
   cd C:\src
   git clone https://github.com/drdpj/AcornDiskWrangler.git
   git clone https://github.com/drdpj/CPMDiskWrangler.git
   ```

   For a particular release rather than the latest code, add `git -C AcornDiskWrangler checkout <tag>` (tags are
   listed on each repository's Releases page).

3. Build Acorn Disk Wrangler in its own build folder:

   ```
   mkdir C:\src\build-acorn
   cd C:\src\build-acorn
   qmake ..\AcornDiskWrangler\acorndiskwrangler.pro CONFIG+=release CONFIG-=debug_and_release
   mingw32-make -j4
   ```

   This leaves `acorndiskwrangler.exe` in `C:\src\build-acorn`.

4. Build CP/M Disk Wrangler the same way:

   ```
   mkdir C:\src\build-cpm
   cd C:\src\build-cpm
   qmake ..\CPMDiskWrangler\cpmdiskwrangler.pro CONFIG+=release CONFIG-=debug_and_release
   mingw32-make -j4
   ```

   This leaves `cpmdiskwrangler.exe` in `C:\src\build-cpm`.

**Keep these build folders to this toolchain.** Don't build in a folder that another Qt or compiler has used -
the source tree's own `build/` from `ci/build.sh`, say - and don't run `ci/build.sh` in a folder used here. make
reuses object files it thinks are up to date, and objects from another Qt version fail to link with a long list
of `undefined reference to __imp__ZN10QArrayData...` errors. If that happens, delete the build folder and start
again.

The version shown in Help > About comes from Git when `qmake` runs. Built from a downloaded source zip instead
of a clone, it says "unknown" unless you add `APP_VERSION=v1.0.0` (or whichever) to the `qmake` line.

## Package it for another PC
The `.exe` on its own will not start on another machine: it needs Qt's DLLs, the Windows platform plugin and
the MinGW runtime beside it. `windeployqt` (part of the Qt you installed) copies them in. With the PATH still set
as in Build:

```
mkdir C:\dist\AcornDiskWrangler
copy C:\src\build-acorn\acorndiskwrangler.exe C:\dist\AcornDiskWrangler\
windeployqt --release --compiler-runtime --no-translations C:\dist\AcornDiskWrangler\acorndiskwrangler.exe

mkdir C:\dist\CPMDiskWrangler
copy C:\src\build-cpm\cpmdiskwrangler.exe C:\dist\CPMDiskWrangler\
windeployqt --release --compiler-runtime --no-translations C:\dist\CPMDiskWrangler\cpmdiskwrangler.exe
```

Each folder under `C:\dist` is now complete: copy or zip the whole folder, never the `.exe` alone. It should
hold, among others, `Qt5Core.dll`, `Qt5Gui.dll`, `Qt5Widgets.dll`, `libgcc_s_dw2-1.dll`, `libstdc++-6.dll`,
`libwinpthread-1.dll` and a `platforms` folder with `qwindows.dll`.

To carry a program on a USB stick with its settings, start it once from the stick and choose
File > Create local config file.

## Check it on Windows 7
Copy a folder from `C:\dist` to the Windows 7 PC (it must have Service Pack 1) and double-click the `.exe`.
Help > About should show the version, and opening and saving a copy of a disc image should work. If something
goes wrong:

| What you see | Cause | Fix |
|---|---|---|
| `'qmake' is not recognized` | PATH not set in this window | Run the `set PATH=...` line from Build again |
| `mingw32-make` stops with an error about `sh.exe` | Git's or MSYS2's Unix tools are on the PATH | Build in a window whose PATH has only `Git\cmd`, not `Git\usr\bin` or `C:\msys64\...` |
| Many `undefined reference to __imp__...` errors when linking | Objects left in the build folder by another Qt or compiler | Delete the build folder and build again |
| "api-ms-win-crt-runtime-l1-1-0.dll is missing" | Built with a different compiler (MSYS2 UCRT64 or Qt 6) | Rebuild with the Qt and MinGW installed above |
| "is not a valid Win32 application" | A 64-bit build | `qmake -query QT_INSTALL_PREFIX` must end in `mingw81_32`; fix the PATH and rebuild |
| "no Qt platform plugin could be initialized" | The `platforms` folder is missing | Run `windeployqt` again and copy the whole folder |
| "The procedure entry point ... could not be located" | Picked up DLLs from another Qt or program | Run the `.exe` from its own folder, which holds its own `Qt5*.dll` |
| Errors while compiling | This toolchain hasn't built the code before | Report the first few error lines (GitHub issues) |

## Licences if you pass the build on
For your own use nothing more is needed. If you give the folder to others:

* Copy `LICENSE` and `THIRD-PARTY-NOTICES.md` from the program's source folder into it. Both are also built into
  the program (Help > Licences), along with Qt's LGPL.
* Say where the source is: [AcornDiskWrangler](https://github.com/drdpj/AcornDiskWrangler) and
  [CPMDiskWrangler](https://github.com/drdpj/CPMDiskWrangler) on GitHub. The programs are GPL v3, which requires
  that.
* Keep Qt as separate DLLs, as `windeployqt` leaves it. The LGPL lets users replace them, so don't merge them
  into the `.exe`.
* The official releases also list the licence of every DLL they ship; a self-built folder doesn't. The MinGW
  runtime DLLs are under the GCC Runtime Library Exception (`libgcc`, `libstdc++`) and a permissive licence
  (`libwinpthread`), so adding those licence texts covers them.
