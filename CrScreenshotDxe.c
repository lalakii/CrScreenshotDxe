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
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BmpSupportLib.h>
#include <Library/RngLib.h>
#include <Library/UefiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/SimpleTextInEx.h>
#include <Protocol/HiiFont.h>

EFI_STATUS
EFIAPI
ShowStatus(
    IN EFI_GRAPHICS_OUTPUT_PROTOCOL *GraphicsOutput, IN CONST CHAR16 *Text)
{
  EFI_STATUS Status;
  UINT8 Index;
  EFI_HII_FONT_PROTOCOL *HiiFont = NULL;
  EFI_IMAGE_OUTPUT *GlyphBlt = NULL;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *FinalBitmap = NULL;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Backup = NULL;
  EFI_FONT_DISPLAY_INFO *DefaultInfo = NULL;
  UINTN TextLen = StrLen(Text);
  UINTN TotalWidth = 0;
  UINTN MaxHeight = 0;
  UINTN XOffset = 0;
  UINTN FinalSize;

  Status = gBS->LocateProtocol(&gEfiHiiFontProtocolGuid, NULL, &HiiFont);
  if (EFI_ERROR(Status))
  {
    goto Done;
  }

  for (Index = 0; Index < TextLen; Index++)
  {
    GlyphBlt = NULL;
    Status = HiiFont->GetGlyph(HiiFont, Text[Index], DefaultInfo, &GlyphBlt, NULL);
    if (EFI_ERROR(Status) || GlyphBlt == NULL)
    {
      if (GlyphBlt != NULL)
      {
        if (GlyphBlt->Image.Bitmap != NULL)
        {
          FreePool(GlyphBlt->Image.Bitmap);
        }
        FreePool(GlyphBlt);
      }
      continue;
    }

    TotalWidth += GlyphBlt->Width;
    if (GlyphBlt->Height > MaxHeight)
    {
      MaxHeight = GlyphBlt->Height;
    }

    FreePool(GlyphBlt->Image.Bitmap);
    FreePool(GlyphBlt);
    GlyphBlt = NULL;
  }

  if (TotalWidth == 0 || MaxHeight == 0)
  {
    Status = EFI_NOT_FOUND;
    goto Done;
  }

  FinalSize = TotalWidth * MaxHeight * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL);
  FinalBitmap = AllocateZeroPool(FinalSize);
  if (FinalBitmap == NULL)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }

  for (Index = 0; Index < TextLen; Index++)
  {
    GlyphBlt = NULL;
    Status = HiiFont->GetGlyph(HiiFont, Text[Index], DefaultInfo, &GlyphBlt, NULL);
    if (EFI_ERROR(Status) || GlyphBlt == NULL)
    {
      if (GlyphBlt != NULL)
      {
        if (GlyphBlt->Image.Bitmap != NULL)
        {
          FreePool(GlyphBlt->Image.Bitmap);
        }
        FreePool(GlyphBlt);
      }
      continue;
    }

    for (UINTN Row = 0; Row < GlyphBlt->Height; Row++)
    {
      for (UINTN Col = 0; Col < GlyphBlt->Width; Col++)
      {
        UINTN SrcIndex = Row * GlyphBlt->Width + Col;
        UINTN DstIndex = Row * TotalWidth + (XOffset + Col);
        FinalBitmap[DstIndex] = GlyphBlt->Image.Bitmap[SrcIndex];
      }
    }

    XOffset += GlyphBlt->Width;
    FreePool(GlyphBlt->Image.Bitmap);
    FreePool(GlyphBlt);
    GlyphBlt = NULL;
  }

  Backup = AllocatePool(FinalSize);
  if (Backup == NULL)
  {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }

  GraphicsOutput->Blt(GraphicsOutput, Backup, EfiBltVideoToBltBuffer, 0, 0, 0, 0, TotalWidth, MaxHeight, 0);
  GraphicsOutput->Blt(GraphicsOutput, FinalBitmap, EfiBltBufferToVideo, 0, 0, 0, 0, TotalWidth, MaxHeight, 0);
  gBS->Stall(1300 * 1000);
  GraphicsOutput->Blt(GraphicsOutput, Backup, EfiBltBufferToVideo, 0, 0, 0, 0, TotalWidth, MaxHeight, 0);
  Status = EFI_SUCCESS;

Done:
  if (Backup != NULL)
  {
    FreePool(Backup);
  }

  if (FinalBitmap != NULL)
  {
    FreePool(FinalBitmap);
  }

  return Status;
}

EFI_STATUS
EFIAPI
FindWritableFile(
    OUT EFI_FILE_PROTOCOL **WritableFile, IN CHAR16 *FileName)
{
  EFI_HANDLE *HandleBuffer = NULL;
  UINTN HandleCount;
  UINT8 Index;
  EFI_STATUS Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &HandleCount, &HandleBuffer);
  if (EFI_ERROR(Status))
  {
    return Status;
  }

  for (Index = 0; Index < HandleCount; Index++)
  {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *SimpleFs = NULL;
    Status = gBS->HandleProtocol(HandleBuffer[Index], &gEfiSimpleFileSystemProtocolGuid, &SimpleFs);
    if (EFI_ERROR(Status))
    {
      continue;
    }

    EFI_FILE_PROTOCOL *RootFs = NULL;
    Status = SimpleFs->OpenVolume(SimpleFs, &RootFs);
    if (EFI_ERROR(Status))
    {
      continue;
    }

    // Open C:\Users\Public\Pictures
    EFI_FILE_PROTOCOL *PicturesDir = NULL;
    Status = RootFs->Open(RootFs, &PicturesDir, L"Users\\Public\\Pictures\\", EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (EFI_ERROR(Status))
    {
      continue;
    }

    EFI_FILE_PROTOCOL *File = NULL;
    Status = PicturesDir->Open(PicturesDir, &File, FileName, EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    PicturesDir->Close(PicturesDir);
    if (EFI_ERROR(Status))
    {
      continue;
    }

    // Writable File found
    UINT8 TestByte = 0xFF;
    UINTN TestByteSize = 1;
    Status = File->Write(File, &TestByteSize, &TestByte);
    if (EFI_ERROR(Status))
    {
      File->Delete(File);
      continue;
    }

    *WritableFile = File;
    Status = EFI_SUCCESS;
    break;
  }

  if (HandleBuffer)
  {
    gBS->FreePool(HandleBuffer);
  }

  return Status;
}

EFI_STATUS
EFIAPI
TakeScreenshot(
    IN EFI_KEY_DATA *KeyData)
{
  EFI_FILE_PROTOCOL *WritableFile = NULL;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *GraphicsOutput = NULL;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Image = NULL;
  UINT8 *BmpFile = NULL;
  UINT8 Index;
  UINTN ImageSize, BmpFileSize, HandleCount;
  EFI_STATUS Status;
  EFI_HANDLE *HandleBuffer = NULL;
  UINT32 ScreenWidth, ScreenHeight;
  CHAR16 FileName[48];
  EFI_TIME Time;
  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiGraphicsOutputProtocolGuid, NULL, &HandleCount, &HandleBuffer);
  if (EFI_ERROR(Status))
  {
    return EFI_UNSUPPORTED;
  }

  for (Index = 0; Index < HandleCount; Index++)
  {
    do
    {
      EFI_DEVICE_PATH_PROTOCOL *DevicePath = NULL;
      Status = gBS->HandleProtocol(HandleBuffer[Index], &gEfiDevicePathProtocolGuid, &DevicePath);
      if (EFI_ERROR(Status))
      {
        break;
      }

      Status = gBS->HandleProtocol(HandleBuffer[Index], &gEfiGraphicsOutputProtocolGuid, &GraphicsOutput);
      if (EFI_ERROR(Status))
      {
        break;
      }

      ScreenWidth = GraphicsOutput->Mode->Info->HorizontalResolution;
      ScreenHeight = GraphicsOutput->Mode->Info->VerticalResolution;
      ImageSize = ScreenWidth * ScreenHeight;
      UINTN BufferSize = ImageSize * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL);
      Status = gBS->AllocatePool(EfiBootServicesData, BufferSize, &Image);
      if (EFI_ERROR(Status))
      {
        break;
      }

      Status = GraphicsOutput->Blt(GraphicsOutput, Image, EfiBltVideoToBltBuffer, 0, 0, 0, 0, ScreenWidth, ScreenHeight, 0);
      if (EFI_ERROR(Status))
      {
        break;
      }

      if (IsZeroBuffer(Image, BufferSize))
      {
        ShowStatus(GraphicsOutput, L"Screenshot is black.");
        break;
      }

      Status = TranslateGopBltToBmp(Image, ScreenHeight, ScreenWidth, &BmpFile, (UINT32 *)&BmpFileSize);
      if (EFI_ERROR(Status))
      {
        break;
      }

      Status = gRT->GetTime(&Time, NULL);
      if (EFI_ERROR(Status))
      {
        UINT32 randomNumber;
        GetRandomNumber32(&randomNumber);
        UnicodeSPrint(FileName, sizeof(FileName), L"Screenshot_%04x.BMP", randomNumber & 0xFFFF);
      }
      else
      {
        UnicodeSPrint(FileName, sizeof(FileName), L"%04d%02d%02d_%02d%02d%02d.BMP", Time.Year, Time.Month, Time.Day, Time.Hour, Time.Minute, Time.Second);
      }

      Status = FindWritableFile(&WritableFile, FileName);
      if (EFI_ERROR(Status))
      {
        ShowStatus(GraphicsOutput, L"No writable file system found.");
        break;
      }

      Status = WritableFile->SetPosition(WritableFile, 0);
      if (EFI_ERROR(Status))
      {
        break;
      }

      Status = WritableFile->Write(WritableFile, &BmpFileSize, BmpFile);
      if (EFI_ERROR(Status))
      {
        break;
      }

      ShowStatus(GraphicsOutput, L"Screenshot succeeded.");
    } while (0);

    if (Image)
    {
      gBS->FreePool(Image);
    }

    Image = NULL;
    if (BmpFile)
    {
      gBS->FreePool(BmpFile);
    }

    BmpFile = NULL;
    if (WritableFile)
    {
      WritableFile->Close(WritableFile);
    }

    WritableFile = NULL;
  }

  if (HandleBuffer)
  {
    FreePool(HandleBuffer);
  }

  HandleBuffer = NULL;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
CrScreenshotDxeEntry(
    IN EFI_HANDLE ImageHandle,
    IN EFI_SYSTEM_TABLE *SystemTable)
{
  UINT8 Index;
  EFI_STATUS Status;
  EFI_KEY_DATA KeyStroke;
  UINTN HandleCount = 0;
  EFI_HANDLE *HandleBuffer = NULL;
  // Set keystroke to be RCtrl + F12
  KeyStroke.Key.ScanCode = SCAN_LEFT;
  KeyStroke.Key.UnicodeChar = 0;
  KeyStroke.KeyState.KeyToggleState = 0;
  KeyStroke.KeyState.KeyShiftState = EFI_SHIFT_STATE_VALID | EFI_RIGHT_CONTROL_PRESSED;
  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiSimpleTextInputExProtocolGuid, NULL, &HandleCount, &HandleBuffer);
  if (EFI_ERROR(Status))
  {
    return EFI_UNSUPPORTED;
  }

  for (Index = 0; Index < HandleCount; Index++)
  {
    EFI_HANDLE Handle;
    EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *SimpleTextInEx;
    Status = gBS->HandleProtocol(HandleBuffer[Index], &gEfiSimpleTextInputExProtocolGuid, &SimpleTextInEx);
    if (!EFI_ERROR(Status))
    {
      SimpleTextInEx->RegisterKeyNotify(SimpleTextInEx, &KeyStroke, TakeScreenshot, &Handle);
    }
  }

  if (HandleBuffer)
  {
    gBS->FreePool(HandleBuffer);
  }

  return EFI_SUCCESS;
}
