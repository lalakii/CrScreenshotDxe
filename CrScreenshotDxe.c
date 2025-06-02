/* CrScreenshotDxe.c

Copyright (c) 2016, Nikolaj Schlej, All rights reserved.

Redistribution and use in source and binary forms,
with or without modification, are permitted provided that the following conditions are met:
- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleTextInEx.h>
#include <Protocol/SimpleFileSystem.h>
#include <Library/RngLib.h>
#include "lodepng.h" //PNG encoding library

EFI_STATUS
EFIAPI
FindWritableFs(
  OUT EFI_FILE_PROTOCOL** WritableFs
)
{
  EFI_HANDLE* HandleBuffer = NULL;
  UINTN      HandleCount;
  UINTN      i;

  // Locate all the simple file system devices in the system
  EFI_STATUS Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &HandleCount, &HandleBuffer);
  if (!EFI_ERROR(Status)) {
    EFI_FILE_PROTOCOL* Fs = NULL;
    // For each located volume
    for (i = 0; i < HandleCount; i++) {
      EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* SimpleFs = NULL;
      EFI_FILE_PROTOCOL* File = NULL;

      // Get protocol pointer for current volume
      Status = gBS->HandleProtocol(HandleBuffer[i], &gEfiSimpleFileSystemProtocolGuid, (VOID**)&SimpleFs);
      if (EFI_ERROR(Status)) {
        DEBUG((-1, "FindWritableFs: gBS->HandleProtocol[%d] returned %r\n", i, Status));
        continue;
      }

      // Open the volume
      Status = SimpleFs->OpenVolume(SimpleFs, &Fs);
      if (EFI_ERROR(Status)) {
        DEBUG((-1, "FindWritableFs: SimpleFs->OpenVolume[%d] returned %r\n", i, Status));
        continue;
      }

      // Open C:\Windows Directory
      EFI_FILE_PROTOCOL* WinDir = NULL;
      Status = Fs->Open(Fs, &WinDir, L"Windows\\", EFI_FILE_MODE_READ, 0);
      if (EFI_ERROR(Status)) {
        DEBUG((-1, "FindWritableFs: Fs->Open[%d] returned %r\n", i, Status));
        continue;
      }

      WinDir->Close(WinDir);

      // Try opening a file for writing
      Status = Fs->Open(Fs, &File, L"crsdtest.fil", EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
      if (EFI_ERROR(Status)) {
        DEBUG((-1, "FindWritableFs: Fs->Open[%d] returned %r\n", i, Status));
        continue;
      }

      // Writable FS found
      Fs->Delete(File);
      *WritableFs = Fs;
      Status = EFI_SUCCESS;
      break;
    }
  }

  // Free memory
  if (HandleBuffer) {
    gBS->FreePool(HandleBuffer);
  }

  return Status;
}

EFI_STATUS
EFIAPI
ShowStatus(
  IN UINT8 Red,
  IN UINT8 Green,
  IN UINT8 Blue
)
{
  // Determines the size of status square
#define STATUS_SQUARE_SIDE 5

  UINTN        HandleCount;
  EFI_HANDLE* HandleBuffer = NULL;
  EFI_GRAPHICS_OUTPUT_PROTOCOL* GraphicsOutput = NULL;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Square[STATUS_SQUARE_SIDE * STATUS_SQUARE_SIDE];
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Backup[STATUS_SQUARE_SIDE * STATUS_SQUARE_SIDE];
  UINTN i;

  // Locate all instances of GOP
  EFI_STATUS Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &HandleCount, &HandleBuffer);
  if (EFI_ERROR(Status)) {
    DEBUG((-1, "ShowStatus: Graphics output protocol not found\n"));
    return EFI_UNSUPPORTED;
  }

  // Set square color
  for (i = 0; i < STATUS_SQUARE_SIDE * STATUS_SQUARE_SIDE; i++) {
    Square[i].Blue = Blue;
    Square[i].Green = Green;
    Square[i].Red = Red;
    Square[i].Reserved = 0x00;
  }

  // For each GOP instance
  for (i = 0; i < HandleCount; i++) {
    // Handle protocol
    Status = gBS->HandleProtocol(HandleBuffer[i], &gEfiGraphicsOutputProtocolGuid, (VOID**)&GraphicsOutput);
    if (EFI_ERROR(Status)) {
      DEBUG((-1, "ShowStatus: gBS->HandleProtocol[%d] returned %r\n", i, Status));
      continue;
    }

    // Backup current image
    GraphicsOutput->Blt(GraphicsOutput, Backup, EfiBltVideoToBltBuffer, 0, 0, 0, 0, STATUS_SQUARE_SIDE, STATUS_SQUARE_SIDE, 0);

    // Draw the status square
    GraphicsOutput->Blt(GraphicsOutput, Square, EfiBltBufferToVideo, 0, 0, 0, 0, STATUS_SQUARE_SIDE, STATUS_SQUARE_SIDE, 0);

    // Wait 500ms
    gBS->Stall(500 * 1000);

    // Restore the backup
    GraphicsOutput->Blt(GraphicsOutput, Backup, EfiBltBufferToVideo, 0, 0, 0, 0, STATUS_SQUARE_SIDE, STATUS_SQUARE_SIDE, 0);
  }

  return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
TakeScreenshot(
  IN EFI_KEY_DATA* KeyData
)
{
  EFI_FILE_PROTOCOL* Fs = NULL;
  EFI_FILE_PROTOCOL* File = NULL;
  EFI_GRAPHICS_OUTPUT_PROTOCOL* GraphicsOutput = NULL;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL* Image = NULL;
  UINTN      ImageSize;         // Size in pixels
  UINT8* PngFile = NULL;
  UINTN      PngFileSize;       // Size in bytes
  EFI_STATUS Status;
  UINTN      HandleCount;
  EFI_HANDLE* HandleBuffer = NULL;
  UINT32     ScreenWidth;
  UINT32     ScreenHeight;
  CHAR16     FileName[50]; // 0-terminated 8.3 file name
  EFI_TIME   Time;
  UINTN      i, j;

  // Find writable FS
  Status = FindWritableFs(&Fs);
  if (EFI_ERROR(Status)) {
    DEBUG((-1, "TakeScreenshot: Can't find writable FS\n"));
    ShowStatus(0xFF, 0xFF, 0x00); //Yellow
    return EFI_SUCCESS;
  }

  // Locate all instances of GOP
  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &HandleCount, &HandleBuffer);
  if (EFI_ERROR(Status)) {
    DEBUG((-1, "ShowStatus: Graphics output protocol not found\n"));
    return EFI_SUCCESS;
  }

  // For each GOP instance
  for (i = 0; i < HandleCount; i++) {
    do { // Break from do used instead of "goto error"
      // Handle protocol
      Status = gBS->HandleProtocol(HandleBuffer[i], &gEfiGraphicsOutputProtocolGuid, (VOID**)&GraphicsOutput);
      if (EFI_ERROR(Status)) {
        DEBUG((-1, "ShowStatus: gBS->HandleProtocol[%d] returned %r\n", i, Status));
        break;
      }

      // Set screen width, height and image size in pixels
      ScreenWidth = GraphicsOutput->Mode->Info->HorizontalResolution;
      ScreenHeight = GraphicsOutput->Mode->Info->VerticalResolution;
      ImageSize = ScreenWidth * ScreenHeight;

      // Get current time
      Status = gRT->GetTime(&Time, NULL);
      if (!EFI_ERROR(Status)) {
        // Set file name to current day and time
        UnicodeSPrint(FileName, 50, L"%04d%02d%02d%02d%02d%02d.png", Time.Year, Time.Month, Time.Day, Time.Hour, Time.Minute, Time.Second);
      }
      else {
        // Set file name to screenshot_%random%.png
        UINT32 randomNumber;
        GetRandomNumber32(&randomNumber);
        UnicodeSPrint(FileName, 50, L"screenshot_%07d.png", randomNumber);
      }

      // Allocate memory for screenshot
      Status = gBS->AllocatePool(EfiBootServicesData, ImageSize * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL), (VOID**)&Image);
      if (EFI_ERROR(Status)) {
        DEBUG((-1, "TakeScreenshot: gBS->AllocatePool returned %r\n", Status));
        break;
      }

      // Take screenshot
      Status = GraphicsOutput->Blt(GraphicsOutput, Image, EfiBltVideoToBltBuffer, 0, 0, 0, 0, ScreenWidth, ScreenHeight, 0);
      if (EFI_ERROR(Status)) {
        DEBUG((-1, "TakeScreenshot: GraphicsOutput->Blt returned %r\n", Status));
        break;
      }

      // Check for pitch black image (it means we are using a wrong GOP)
      for (j = 0; j < ImageSize; j++) {
        if (Image[j].Red != 0x00 || Image[j].Green != 0x00 || Image[j].Blue != 0x00)
          break;
      }
      if (j == ImageSize) {
        DEBUG((-1, "TakeScreenshot: GraphicsOutput->Blt returned pitch black image, skipped\n"));
        ShowStatus(0x00, 0x00, 0xFF); //Blue
        break;
      }

      // Open or create output file
      Status = Fs->Open(Fs, &File, FileName, EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
      if (EFI_ERROR(Status)) {
        DEBUG((-1, "TakeScreenshot: Fs->Open of %s returned %r\n", FileName, Status));
        break;
      }

      // Convert BGR to RGBA with Alpha set to 0xFF
      for (j = 0; j < ImageSize; j++) {
        UINT8 Temp = Image[j].Blue;
        Image[j].Blue = Image[j].Red;
        Image[j].Red = Temp;
        Image[j].Reserved = 0xFF;
      }

      // Encode raw RGB image to PNG format
      j = lodepng_encode32(&PngFile, &PngFileSize, (CONST UINT8*)Image, ScreenWidth, ScreenHeight);
      if (j) {
        DEBUG((-1, "TakeScreenshot: lodepng_encode32 returned %d\n", j));
        break;
      }

      // Write PNG image into the file and close it
      Status = File->Write(File, &PngFileSize, PngFile);
      File->Close(File);
      if (EFI_ERROR(Status)) {
        DEBUG((-1, "TakeScreenshot: File->Write returned %r\n", Status));
        break;
      }

      // Show success
      ShowStatus(0x00, 0xFF, 0x00); //Green
    } while (0);

    // Free memory
    if (Image)
      gBS->FreePool(Image);
    if (PngFile)
      gBS->FreePool(PngFile);
    Image = NULL;
    PngFile = NULL;
  }

  // Show error
  if (EFI_ERROR(Status))
    ShowStatus(0xFF, 0x00, 0x00); //Red

  return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
CrScreenshotDxeEntry(
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE* SystemTable
)
{
  EFI_STATUS   Status;
  EFI_KEY_DATA KeyStroke;
  UINTN        HandleCount;
  EFI_HANDLE* HandleBuffer = NULL;
  UINTN        i;

  // Set keystroke to be RCtrl + F12
  KeyStroke.Key.ScanCode = SCAN_F12;
  KeyStroke.Key.UnicodeChar = 0;
  KeyStroke.KeyState.KeyShiftState = EFI_SHIFT_STATE_VALID | EFI_RIGHT_CONTROL_PRESSED;
  KeyStroke.KeyState.KeyToggleState = 0;

  // Locate all SimpleTextInEx protocols
  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiSimpleTextInputExProtocolGuid, NULL, &HandleCount, &HandleBuffer);
  if (EFI_ERROR(Status)) {
    DEBUG((-1, "CrScreenshotDxeEntry: gBS->LocateHandleBuffer returned %r\n", Status));
    return EFI_UNSUPPORTED;
  }

  // For each instance
  for (i = 0; i < HandleCount; i++) {
    EFI_HANDLE Handle;
    EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL* SimpleTextInEx;

    // Get protocol handle
    Status = gBS->HandleProtocol(HandleBuffer[i], &gEfiSimpleTextInputExProtocolGuid, (VOID**)&SimpleTextInEx);
    if (EFI_ERROR(Status)) {
      DEBUG((-1, "CrScreenshotDxeEntry: gBS->HandleProtocol[%d] returned %r\n", i, Status));
      continue;
    }

    // Register key notification function
    Status = SimpleTextInEx->RegisterKeyNotify(
      SimpleTextInEx,
      &KeyStroke,
      TakeScreenshot,
      &Handle);
    if (EFI_ERROR(Status)) {
      DEBUG((-1, "CrScreenshotDxeEntry: SimpleTextInEx->RegisterKeyNotify[%d] returned %r\n", i, Status));
    }
  }

  // Free memory used for handle buffer
  if (HandleBuffer)
    gBS->FreePool(HandleBuffer);

  // Show success
  ShowStatus(0xFF, 0xFF, 0xFF); //White

  return EFI_SUCCESS;
}
