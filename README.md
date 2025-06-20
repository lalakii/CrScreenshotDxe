# CrScreenshotDxe
UEFI DXE driver to take screenshots from GOP-compatible graphic consoles.

[This blog post in Russian](http://habrahabr.ru/post/274463/) explains more, here is just a description and usage.

## Features

Screenshot shortcuts: RightCtrl + F12

Screenshots are saved in C:\Users\Public\Pictures

Requires ntfsdxe driver: https://github.com/pbatard/uefi-ntfs

## Description
This DXE driver tries to register keyboard shortcut (LCtrl + LAlt + F12) handler for all text input devices. The handler tries to find a writable FS, enumerates all GOP-capable video devices, takes screenshots from them and saves the result as PNG files on that writable FS.

The main goal is to be able to make BIOS Setup screenshots for systems without serial console redirection support, but it can also be used to take screenshot from UEFI shell, UEFI apps and UEFI bootloaders. 

To start the driver, you can either:
- Integrate it into DXE volume of your UEFI firmware using [UEFITool](https://github.com/LongSoft/UEFITool) or any other suitable software (don't forget a DepEx section to prevent too early start)
- Add it to an OptionROM of a PCIe device (will try it once I have a device needed)
- Let BDS dispatcher load it by copying it to ESP and creating a DriverXXXX variable
- Load it from UEFI Shell with load command

## Build
It's a normal EDK2-compatible DXE driver, just add it to your package's DSC file to include in the build process.

## Usage
Load the driver, insert FAT32-formatted USB drive and press LCtrl + LAlt + F12 to take screenshots from all GOP-compatible graphic consoles available at the moment. 

To indicate it's status, the driver shows a small colored rectangle in top-left corner of the screen for half a second.

Rectangle color codes:
- White  - driver is loaded
- Yellow - no writable FS found, screenshot is not taken
- Blue   - current GOP is pitch black, screenshot is not taken
- Red    - something went wrong, screenshot is not taken
- Green  - screnshot taken and saved to PNG file
