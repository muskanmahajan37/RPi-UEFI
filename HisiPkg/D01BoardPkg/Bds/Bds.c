/** @file
*
*  Copyright (c) 2011-2013, ARM Limited. All rights reserved.
*  Copyright (c) Huawei Technologies Co., Ltd. 2013. All rights reserved.
*
*  This program and the accompanying materials
*  are licensed and made available under the terms and conditions of the BSD License
*  which accompanies this distribution.  The full text of the license may be found at
*  http://opensource.org/licenses/bsd-license.php
*
*  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
*  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
*
**/

#include "BdsInternal.h"

#include <Library/PcdLib.h>
#include <Library/PerformanceLib.h>

#include <Protocol/Bds.h>
#include <Protocol/NorFlashProtocol.h>
#include <Library/BspUartLib.h>
#include <libfdt_env.h>
#include <Protocol/NandFlashProtocol.h>

char gpoint3[2][100] = {0};

#ifndef U8
typedef unsigned char   U8;
#endif

#define EFI_SET_TIMER_TO_SECOND      10000000
#define FLASH_ECC_RESADDR            0x31000000
#define FLASH_ECC_RESADDR_OFFSET     0x1000000
#define RAM_TEST_WORK_TAG            (0x5A5A5A5A) 
#define RAM_TEST_NOWORK_TAG          (0x0A0A0A0A) 

EFI_HANDLE mImageHandle;

//************************************************
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2
#define EOF     (-1)

#define CPBSP_BASE_ID  (0x0)
#define M_bootload                            (26 << 16)
#define M_common                              (0 << 16)
#define M_common_PARAMETER_ERR                (M_common | 1)
#define M_bootload_LOADVXWORKS_FAIL           (CPBSP_BASE_ID | M_bootload | 7)

#define GET_CHAR_FROM_COM(timeout)   BspGetChar(timeout*23000)

/* max number of image which you can choice,there are two file name for NODEB*/
#define PRODUCT_FILENUM       2
U32 g_ulProductVerSelectNum = PRODUCT_FILENUM;
#define BOOTUP_CONFIG_FILE           "Bootup.ini"   /* bootup config file */

STATUS BootTovxWorks( void );
void BSP_GetProductFileName(char* cProductVerName);
U32 BSP_LoadVxworks (char *pFileName);
STATUS  BSP_LoadVxworksByName( char *fileName );

extern EFI_STATUS
BootLinuxAtagLoader (
  IN LIST_ENTRY *BootOptionsList
  );
EFI_STATUS
BootGo (
  IN LIST_ENTRY *BootOptionsList
  );

//************************************************
//address of Linux in DDR
#define TEXT_DDR_BASE                   0x10c00000
#define MONITOR_DDR_BASE                0x10c08000         
#define KERNEL_DDR_BASE                 0x10008000
#define FILESYSTEM_DDR_BASE             0x10d00000

//estimate size of Linux kernel,the size for copying file to DDR isn't bigger than this
#define TEXT_SIZE                       0x400000
#define MONITOR_SIZE                    0x400000
#define KERNEL_SIZE                     0xa00000
#define FILESYSTEM_SIZE                 0x1800000

//actual size of copying file to DDR, it should not bigger than estimate size
#define TEXT_COPY_SIZE                  0x20000
#define MONITOR_COPY_SIZE               0x20000
#define KERNEL_COPY_SIZE                0xa00000
#define FILESYSTEM_COPY_SIZE            0x1800000

//address of Linux in NORFLASH
#define TEXT_FLASH_BASE                 (PcdGet32(PcdNorFlashBase) + 0x1000000)
#define MONITOR_FLASH_BASE              (TEXT_FLASH_BASE + TEXT_SIZE)
#define KERNEL_FLASH_BASE               (MONITOR_FLASH_BASE + MONITOR_SIZE)
#define FILESYSTEM_FLASH_BASE           (KERNEL_FLASH_BASE + KERNEL_SIZE)

//address of Linux in NANDFlash
#define TEXT_BLOCKNUM_NANDFLASH         (0)
#define MONITOR_BLOCKNUM_NANDFLASH      (TEXT_BLOCKNUM_NANDFLASH + TEXT_SIZE / 0x20000)
#define KERNEL_BLOCKNUM_NANDFLASH       (MONITOR_BLOCKNUM_NANDFLASH + MONITOR_SIZE / 0x20000)
#define FILESYSTEM_BLOCKNUM_NANDFLASH   (KERNEL_BLOCKNUM_NANDFLASH + KERNEL_SIZE / 0x20000)

STATIC
EFI_STATUS
GetConsoleDevicePathFromVariable (
  IN  CHAR16*             ConsoleVarName,
  IN  CHAR16*             DefaultConsolePaths,
  OUT EFI_DEVICE_PATH**   DevicePaths
  )
{
  EFI_STATUS                Status;
  UINTN                     Size;
  EFI_DEVICE_PATH_PROTOCOL* DevicePathInstances;
  EFI_DEVICE_PATH_PROTOCOL* DevicePathInstance;
  CHAR16*                   DevicePathStr;
  CHAR16*                   NextDevicePathStr;
  EFI_DEVICE_PATH_FROM_TEXT_PROTOCOL  *EfiDevicePathFromTextProtocol;

  Status = GetGlobalEnvironmentVariable (ConsoleVarName, NULL, NULL, (VOID**)&DevicePathInstances);
  if (EFI_ERROR(Status)) {
    // In case no default console device path has been defined we assume a driver handles the console (eg: SimpleTextInOutSerial)
    if ((DefaultConsolePaths == NULL) || (DefaultConsolePaths[0] == L'\0')) {
      *DevicePaths = NULL;
      return EFI_SUCCESS;
    }

    Status = gBS->LocateProtocol (&gEfiDevicePathFromTextProtocolGuid, NULL, (VOID **)&EfiDevicePathFromTextProtocol);
    ASSERT_EFI_ERROR(Status);

    DevicePathInstances = NULL;

    // Extract the Device Path instances from the multi-device path string
    while ((DefaultConsolePaths != NULL) && (DefaultConsolePaths[0] != L'\0')) {
      NextDevicePathStr = StrStr (DefaultConsolePaths, L";");
      if (NextDevicePathStr == NULL) {
        DevicePathStr = DefaultConsolePaths;
        DefaultConsolePaths = NULL;
      } else {
        DevicePathStr = (CHAR16*)AllocateCopyPool ((NextDevicePathStr - DefaultConsolePaths + 1) * sizeof(CHAR16), DefaultConsolePaths);
        *(DevicePathStr + (NextDevicePathStr - DefaultConsolePaths)) = L'\0';
        DefaultConsolePaths = NextDevicePathStr;
        if (DefaultConsolePaths[0] == L';') {
          DefaultConsolePaths++;
        }
      }

      DevicePathInstance = EfiDevicePathFromTextProtocol->ConvertTextToDevicePath (DevicePathStr);
      ASSERT(DevicePathInstance != NULL);
      DevicePathInstances = AppendDevicePathInstance (DevicePathInstances, DevicePathInstance);

      if (NextDevicePathStr != NULL) {
        FreePool (DevicePathStr);
      }
      FreePool (DevicePathInstance);
    }

    // Set the environment variable with this device path multi-instances
    Size = GetDevicePathSize (DevicePathInstances);
    if (Size > 0) {
      gRT->SetVariable (
          ConsoleVarName,
          &gEfiGlobalVariableGuid,
          EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
          Size,
          DevicePathInstances
          );
    } else {
      Status = EFI_INVALID_PARAMETER;
    }
  }

  if (!EFI_ERROR(Status)) {
    *DevicePaths = DevicePathInstances;
  }
  return Status;
}

STATIC
EFI_STATUS
InitializeConsolePipe (
  IN EFI_DEVICE_PATH    *ConsoleDevicePaths,
  IN EFI_GUID           *Protocol,
  OUT EFI_HANDLE        *Handle,
  OUT VOID*             *Interface
  )
{
  EFI_STATUS                Status;
  UINTN                     Size;
  UINTN                     NoHandles;
  EFI_HANDLE                *Buffer;
  EFI_DEVICE_PATH_PROTOCOL* DevicePath;

  // Connect all the Device Path Consoles
  while (ConsoleDevicePaths != NULL) {
    DevicePath = GetNextDevicePathInstance (&ConsoleDevicePaths, &Size);

    Status = BdsConnectDevicePath (DevicePath, Handle, NULL);
    DEBUG_CODE_BEGIN();
      if (EFI_ERROR(Status)) {
        // We convert back to the text representation of the device Path
        EFI_DEVICE_PATH_TO_TEXT_PROTOCOL* DevicePathToTextProtocol;
        CHAR16* DevicePathTxt;
        EFI_STATUS Status;

        Status = gBS->LocateProtocol(&gEfiDevicePathToTextProtocolGuid, NULL, (VOID **)&DevicePathToTextProtocol);
        if (!EFI_ERROR(Status)) {
          DevicePathTxt = DevicePathToTextProtocol->ConvertDevicePathToText (DevicePath, TRUE, TRUE);

          DEBUG((EFI_D_ERROR,"Fail to start the console with the Device Path '%s'. (Error '%r')\n", DevicePathTxt, Status));

          FreePool (DevicePathTxt);
        }
      }
    DEBUG_CODE_END();

    // If the console splitter driver is not supported by the platform then use the first Device Path
    // instance for the console interface.
    if (!EFI_ERROR(Status) && (*Interface == NULL)) {
      Status = gBS->HandleProtocol (*Handle, Protocol, Interface);
    }
  }

  // No Device Path has been defined for this console interface. We take the first protocol implementation
  if (*Interface == NULL) {
    Status = gBS->LocateHandleBuffer (ByProtocol, Protocol, NULL, &NoHandles, &Buffer);
    if (EFI_ERROR (Status)) {
      BdsConnectAllDrivers();
      Status = gBS->LocateHandleBuffer (ByProtocol, Protocol, NULL, &NoHandles, &Buffer);
    }

    if (!EFI_ERROR(Status)) {
      *Handle = Buffer[0];
      Status = gBS->HandleProtocol (*Handle, Protocol, Interface);
      ASSERT_EFI_ERROR(Status);
    }
    FreePool (Buffer);
  } else {
    Status = EFI_SUCCESS;
  }

  return Status;
}

EFI_STATUS
InitializeConsole (
  VOID
  )
{
  EFI_STATUS                Status;
  EFI_DEVICE_PATH*          ConOutDevicePaths;
  EFI_DEVICE_PATH*          ConInDevicePaths;
  EFI_DEVICE_PATH*          ConErrDevicePaths;

  // By getting the Console Device Paths from the environment variables before initializing the console pipe, we
  // create the 3 environment variables (ConIn, ConOut, ConErr) that allows to initialize all the console interface
  // of newly installed console drivers
  Status = GetConsoleDevicePathFromVariable (L"ConOut", (CHAR16*)PcdGetPtr(PcdDefaultConOutPaths), &ConOutDevicePaths);
  ASSERT_EFI_ERROR (Status);
  Status = GetConsoleDevicePathFromVariable (L"ConIn", (CHAR16*)PcdGetPtr(PcdDefaultConInPaths), &ConInDevicePaths);
  ASSERT_EFI_ERROR (Status);
  Status = GetConsoleDevicePathFromVariable (L"ErrOut", (CHAR16*)PcdGetPtr(PcdDefaultConOutPaths), &ConErrDevicePaths);
  ASSERT_EFI_ERROR (Status);

  // Initialize the Consoles
  Status = InitializeConsolePipe (ConOutDevicePaths, &gEfiSimpleTextOutProtocolGuid, &gST->ConsoleOutHandle, (VOID **)&gST->ConOut);
  ASSERT_EFI_ERROR (Status);
  Status = InitializeConsolePipe (ConInDevicePaths, &gEfiSimpleTextInProtocolGuid, &gST->ConsoleInHandle, (VOID **)&gST->ConIn);
  ASSERT_EFI_ERROR (Status);
  Status = InitializeConsolePipe (ConErrDevicePaths, &gEfiSimpleTextOutProtocolGuid, &gST->StandardErrorHandle, (VOID **)&gST->StdErr);
  if (EFI_ERROR(Status)) {
    // In case of error, we reuse the console output for the error output
    gST->StandardErrorHandle = gST->ConsoleOutHandle;
    gST->StdErr = gST->ConOut;
  }

  // Free Memory allocated for reading the UEFI Variables
  if (ConOutDevicePaths) {
    FreePool (ConOutDevicePaths);
  }
  if (ConInDevicePaths) {
    FreePool (ConInDevicePaths);
  }
  if (ConErrDevicePaths) {
    FreePool (ConErrDevicePaths);
  }

  return EFI_SUCCESS;
}

EFI_STATUS
DefineDefaultBootEntries (
  VOID
  )
{
  BDS_LOAD_OPTION*                    BdsLoadOption;
  UINTN                               Size;
  EFI_STATUS                          Status;
  EFI_DEVICE_PATH_FROM_TEXT_PROTOCOL* EfiDevicePathFromTextProtocol;
  EFI_DEVICE_PATH*                    BootDevicePath;
  ARM_BDS_LOADER_ARGUMENTS*           BootArguments;
  ARM_BDS_LOADER_TYPE                 BootType;

  //
  // If Boot Order does not exist then create a default entry
  //
  Size = 0;
  Status = gRT->GetVariable (L"BootOrder", &gEfiGlobalVariableGuid, NULL, &Size, NULL);
  if (Status == EFI_NOT_FOUND) {
    if ((PcdGetPtr(PcdDefaultBootDevicePath) == NULL) || (StrLen ((CHAR16*)PcdGetPtr(PcdDefaultBootDevicePath)) == 0)) {
      return EFI_UNSUPPORTED;
    }

    Status = gBS->LocateProtocol (&gEfiDevicePathFromTextProtocolGuid, NULL, (VOID **)&EfiDevicePathFromTextProtocol);
    if (EFI_ERROR(Status)) {
      // You must provide an implementation of DevicePathFromTextProtocol in your firmware (eg: DevicePathDxe)
      DEBUG((EFI_D_ERROR,"Error: Bds requires DevicePathFromTextProtocol\n"));
      return Status;
    }
    BootDevicePath = EfiDevicePathFromTextProtocol->ConvertTextToDevicePath ((CHAR16*)PcdGetPtr(PcdDefaultBootDevicePath));

    DEBUG_CODE_BEGIN();
      // We convert back to the text representation of the device Path to see if the initial text is correct
      EFI_DEVICE_PATH_TO_TEXT_PROTOCOL* DevicePathToTextProtocol;
      CHAR16* DevicePathTxt;

      Status = gBS->LocateProtocol(&gEfiDevicePathToTextProtocolGuid, NULL, (VOID **)&DevicePathToTextProtocol);
      ASSERT_EFI_ERROR(Status);
      DevicePathTxt = DevicePathToTextProtocol->ConvertDevicePathToText (BootDevicePath, TRUE, TRUE);

      ASSERT (StrCmp ((CHAR16*)PcdGetPtr(PcdDefaultBootDevicePath), DevicePathTxt) == 0);

      if (DevicePathTxt != NULL){
        FreePool (DevicePathTxt);
      }
    DEBUG_CODE_END();

    BootArguments = NULL;

    // Create the entry is the Default values are correct
    if (BootDevicePath != NULL) {
      BootType = (ARM_BDS_LOADER_TYPE)PcdGet32 (PcdDefaultBootType);

      BootOptionCreate (LOAD_OPTION_ACTIVE | LOAD_OPTION_CATEGORY_BOOT,
        (CHAR16*)PcdGetPtr(PcdDefaultBootDescription),
        BootDevicePath,
        BootType,
        BootArguments,
        &BdsLoadOption
        );
      if (BdsLoadOption != NULL){
        FreePool (BdsLoadOption);
      }
      if (BootDevicePath != NULL){
        FreePool (BootDevicePath);
      }
    } else {
      Status = EFI_UNSUPPORTED;
    }
  }

  return EFI_SUCCESS;
}

EFI_STATUS
StartDefaultBootOnTimeout (
  VOID
  )
{
  UINTN               Size;
  UINT16              Timeout;
  UINT16              *TimeoutPtr;
  EFI_EVENT           WaitList[2];
  UINTN               WaitIndex;
  UINT16              *BootOrder;
  UINTN               BootOrderSize;
  UINTN               Index;
  CHAR16              BootVariableName[9];
  EFI_STATUS          Status;
  EFI_INPUT_KEY       Key;

  Size = sizeof(UINT16);
  Timeout = (UINT16)PcdGet16 (PcdPlatformBootTimeOut);
  TimeoutPtr = &Timeout;
  GetGlobalEnvironmentVariable (L"Timeout", &Timeout, &Size, (VOID**)&TimeoutPtr);

  if (Timeout != 0xFFFF) {
    if (Timeout > 0) {
      // Create the waiting events (keystroke and 1sec timer)
      gBS->CreateEvent (EVT_TIMER, 0, NULL, NULL, &WaitList[0]);
      gBS->SetTimer (WaitList[0], TimerPeriodic, EFI_SET_TIMER_TO_SECOND);
      WaitList[1] = gST->ConIn->WaitForKey;

      // Start the timer
      WaitIndex = 0;
      Print(L"The default boot selection will start in ");
      while ((Timeout > 0) && (WaitIndex == 0)) {
        Print(L"%3d seconds",Timeout);
        gBS->WaitForEvent (2, WaitList, &WaitIndex);
        if (WaitIndex == 0) {
          Print(L"\b\b\b\b\b\b\b\b\b\b\b");
          Timeout--;
        }
      }
      // Discard key in the buffer
      do {
      	Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
      } while(!EFI_ERROR(Status));
      gBS->CloseEvent (WaitList[0]);
      Print(L"\n\r");
    }

    // In case of Timeout we start the default boot selection
    if (Timeout == 0) {
      // Get the Boot Option Order from the environment variable (a default value should have been created)
      GetGlobalEnvironmentVariable (L"BootOrder", NULL, &BootOrderSize, (VOID**)&BootOrder);

      for (Index = 0; Index < BootOrderSize / sizeof (UINT16); Index++) {
        UnicodeSPrint (BootVariableName, 9 * sizeof(CHAR16), L"Boot%04X", BootOrder[Index]);
        Status = BdsStartBootOption (BootVariableName);
        if(!EFI_ERROR(Status)){
        	// Boot option returned successfully, hence don't need to start next boot option
        	break;
        }
        // In case of success, we should not return from this call.
      }
      FreePool (BootOrder);
    }
  }
  return EFI_SUCCESS;
}

//copy image from NANDFLASH to DDR,file in NANDFLASH must be block align
EFI_STATUS CopyNandToMem(void* Dest, UINT32 StartBlockNum, UINT32 LengthCopy)
{
    EFI_NAND_DRIVER_PROTOCOL   *nandDriver= NULL;
    EFI_STATUS          Status;
    NAND_CMD_INFO_STRU ulNandCMDInfo = {0, 0, 0};
    UINT8 *pucData = NULL;
    UINT32 ulChunkNum = 0;
    UINT32 BlockNumCopy = 0;
    UINT32 i = 0;
    UINT32 PagePerBlock = 0;
    UINT32 PageNumCopy = 0;

    DEBUG((EFI_D_ERROR, "[%a : %d]\n", __FUNCTION__, __LINE__));
    Status = gBS->LocateProtocol (&gNANDDriverProtocolGuid, NULL, (VOID *) &nandDriver);
    if (EFI_ERROR(Status))
    {
        DEBUG((EFI_D_ERROR, "[%a : %d]LocateProtocol:gNANDDriverProtocolGuid fail!\n", __FUNCTION__, __LINE__));
        return EFI_ABORTED;
    }

    ulNandCMDInfo = nandDriver->NandFlashGetCMDInfo(nandDriver);
    if(0 != LengthCopy % ulNandCMDInfo.ulBlockSize)
    {
        DEBUG((EFI_D_ERROR, "[%a : %d]input parameter ERROR!\n", __FUNCTION__, __LINE__));
        return EFI_ABORTED;
    }

    if (NULL == (pucData = (UINT8*)AllocatePool(ulNandCMDInfo.ulPageSize)))
    {
        DEBUG((EFI_D_ERROR, "[%a : %d]Apply buffer failed!\n", __FUNCTION__, __LINE__));
        return EFI_BAD_BUFFER_SIZE;
    }
    
    PagePerBlock = ulNandCMDInfo.ulBlockSize / ulNandCMDInfo.ulPageSize;
    BlockNumCopy = (0 == LengthCopy % ulNandCMDInfo.ulBlockSize) ? (LengthCopy / ulNandCMDInfo.ulBlockSize) : (LengthCopy / ulNandCMDInfo.ulBlockSize + 1);
    PageNumCopy = PagePerBlock * BlockNumCopy;
    DEBUG((EFI_D_ERROR, "[%a : %d]PageNumCopy = %x!\n", __FUNCTION__, __LINE__, PageNumCopy));
    for(i = 0;i < PageNumCopy; i++)
    {
        //calculate chunk number of your want to read in NANDFLASH
        ulChunkNum = StartBlockNum * PagePerBlock + i;
        gBS->SetMem(pucData, ulNandCMDInfo.ulPageSize, 0);
        Status = (int)nandDriver->NandFlashReadEcc(nandDriver, ulChunkNum, 0, ulNandCMDInfo.ulPageSize, pucData, NULL);
        if(EFI_SUCCESS != Status)
        {
            DEBUG((EFI_D_ERROR, "[%a : %d]NandFlashReadEcc ERROR!\n", __FUNCTION__, __LINE__));
            gBS->FreePool(pucData);
            return EFI_ABORTED;
        }
        memcpy((void *)((UINT32)Dest + i * ulNandCMDInfo.ulPageSize), (void *)pucData, ulNandCMDInfo.ulPageSize);
    }
    gBS->FreePool(pucData);
    return EFI_SUCCESS;
}
#define NANDFLASHREAD 1

/**
  This function uses policy data from the platform to determine what operating 
  system or system utility should be loaded and invoked.  This function call 
  also optionally make the use of user input to determine the operating system 
  or system utility to be loaded and invoked.  When the DXE Core has dispatched 
  all the drivers on the dispatch queue, this function is called.  This 
  function will attempt to connect the boot devices required to load and invoke 
  the selected operating system or system utility.  During this process, 
  additional firmware volumes may be discovered that may contain addition DXE 
  drivers that can be dispatched by the DXE Core.   If a boot device cannot be 
  fully connected, this function calls the DXE Service Dispatch() to allow the 
  DXE drivers from any newly discovered firmware volumes to be dispatched.  
  Then the boot device connection can be attempted again.  If the same boot 
  device connection operation fails twice in a row, then that boot device has 
  failed, and should be skipped.  This function should never return.

  @param  This             The EFI_BDS_ARCH_PROTOCOL instance.

  @return None.

**/

VOID
EFIAPI
BdsEntry (
  IN EFI_BDS_ARCH_PROTOCOL  *This
  )
{
  UINTN               Size;
  EFI_STATUS          Status;
  //STATUS              Result;
  UINT16             *BootNext;
  UINTN               BootNextSize;
  CHAR16              BootVariableName[9];
  EFI_NAND_DRIVER_PROTOCOL   *nandDriver= NULL;

  //UNI_NOR_FLASH_PROTOCOL        *Flash;

  PERF_END   (NULL, "DXE", NULL, 0);
  //U32 buffer[9];

  char ckeyValue = 0 ;
  LIST_ENTRY  BootOptionsList;
  //
  // Declare the Firmware Vendor
  //
  if (FixedPcdGetPtr(PcdFirmwareVendor) != NULL) {
    Size = 0x100;
    gST->FirmwareVendor = AllocateRuntimePool (Size);
    ASSERT (gST->FirmwareVendor != NULL);
    UnicodeSPrint (gST->FirmwareVendor, Size, L"%a EFI %a %a", PcdGetPtr(PcdFirmwareVendor), __DATE__, __TIME__);
  }

  // If BootNext environment variable is defined then we just load it !
  BootNextSize = sizeof(UINT16);
  Status = GetGlobalEnvironmentVariable (L"BootNext", NULL, &BootNextSize, (VOID**)&BootNext);
  if (!EFI_ERROR(Status)) {
    ASSERT(BootNextSize == sizeof(UINT16));

    // Generate the requested Boot Entry variable name
    UnicodeSPrint (BootVariableName, 9 * sizeof(CHAR16), L"Boot%04X", *BootNext);

    // Set BootCurrent variable
    gRT->SetVariable (L"BootCurrent", &gEfiGlobalVariableGuid,
              EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
              BootNextSize, BootNext);

    FreePool (BootNext);

    // Start the requested Boot Entry
    Status = BdsStartBootOption (BootVariableName);
    if (Status != EFI_NOT_FOUND) {
      // BootNext has not been succeeded launched
      if (EFI_ERROR(Status)) {
        Print(L"Fail to start BootNext.\n");
      }

      // Delete the BootNext environment variable
      gRT->SetVariable (L"BootNext", &gEfiGlobalVariableGuid,
          EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
          0, NULL);
    }

    // Clear BootCurrent variable
    gRT->SetVariable (L"BootCurrent", &gEfiGlobalVariableGuid,
        EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
        0, NULL);
  }

  // If Boot Order does not exist then create a default entry
  DefineDefaultBootEntries ();

  // Now we need to setup the EFI System Table with information about the console devices.
  InitializeConsole ();
  
    Status = gBS->LocateProtocol (&gNANDDriverProtocolGuid, NULL, (VOID *) &nandDriver);
    if (EFI_ERROR(Status))
    {
        DEBUG((EFI_D_ERROR, "LocateProtocol:gNANDDriverProtocolGuid fail!\n"));
    }
    
    DEBUG((EFI_D_ERROR, "NandFlashInit start\n"));
    Status = (int)nandDriver->NandFlashInit(nandDriver);
    if(EFI_SUCCESS != Status)
    {
        DEBUG((EFI_D_ERROR, "NAND Flash Init Error! Status = %r\n", Status));
    }
    else
    {
        DEBUG((EFI_D_ERROR, "NAND Flash Init OK! Status = %r\n", Status));
    }


    (VOID)AsciiPrint("\n\rAuto boot or not ?(Press 's' to Boot Menu)"); 
    (VOID)AsciiPrint("\n\rNow wait for 2 seconds...\n");
    
    ckeyValue = GET_CHAR_FROM_COM(2);

    if ( 0x73 != ckeyValue )   
    {
        (VOID)AsciiPrint("\n\rNot Press 's', Start Auto Boot!\n"); 
        Status = gBS->LocateProtocol (&gNANDDriverProtocolGuid, NULL, (VOID *) &nandDriver);
        if (EFI_ERROR(Status))
        {
            DEBUG((EFI_D_ERROR, "LocateProtocol:gNANDDriverProtocolGuid fail!\n"));
        }

        //Status = BootMenuMain ();
        //ASSERT_EFI_ERROR (Status);    
        
        #if 1
        /*2.copy image from FLASH to DDR,and start*/
        (VOID)AsciiPrint("\nTransmit  OS from FLASH to DDR now, please wait!");
        
        #ifdef NANDFLASHREAD
            CopyNandToMem((void *)TEXT_DDR_BASE, TEXT_BLOCKNUM_NANDFLASH, TEXT_COPY_SIZE);
            (VOID)AsciiPrint("\nThe .text file is transmitted ok!\n");
        #else
             /* copy.text */
            memcpy((void *)TEXT_DDR_BASE, (void *)TEXT_FLASH_BASE, TEXT_COPY_SIZE);
            (VOID)AsciiPrint("\nThe .text file is transmitted ok!\n");
        #endif
//        /* compare text in FLASH and the same file in DDR*/
//        if (CompareMem((void *)TEXT_DDR_BASE, (void *)TEXT_FLASH_BASE, TEXT_COPY_SIZE) != 0)
//        {
//            (VOID)AsciiPrint("The .text file check fail!\n");
//            //return;
//        }
//        else
//        {
//            (VOID)AsciiPrint("The .text file check sucess!\n");
//        }
         
        #ifdef NANDFLASHREAD
             /* copy .monitor */
            CopyNandToMem((void *)MONITOR_DDR_BASE, MONITOR_BLOCKNUM_NANDFLASH, MONITOR_COPY_SIZE);
            (VOID)AsciiPrint("The .monitor file is transmitted ok!\n");
        #else
             /* copy .monitor */
            memcpy((void *)MONITOR_DDR_BASE, (void *)MONITOR_FLASH_BASE, MONITOR_COPY_SIZE);
            (VOID)AsciiPrint("The .monitor file is transmitted ok!\n");
        #endif
//        /* compare uimage in FLASH and the same file in DDR*/
//        if (CompareMem((void *)MONITOR_DDR_BASE, (void *)MONITOR_FLASH_BASE, MONITOR_COPY_SIZE) != 0)
//        {
//            (VOID)AsciiPrint("The .monitor file check fail!\n");
//            //return;
//        }
//        else
//        {
//            (VOID)AsciiPrint("The .monitor file check sucess!\n");
//        }

        #ifdef NANDFLASHREAD
             /* copy.kernel */
            CopyNandToMem((void *)KERNEL_DDR_BASE, KERNEL_BLOCKNUM_NANDFLASH, KERNEL_COPY_SIZE);
            (VOID)AsciiPrint("\nThe .kernel file is transmitted ok!\n");
        #else
             /* copy.kernel */
            memcpy((void *)KERNEL_DDR_BASE, (void *)KERNEL_FLASH_BASE, KERNEL_COPY_SIZE);
            (VOID)AsciiPrint("\nThe .kernel file is transmitted ok!\n");
        #endif
//        /* compare kernel in FLASH and the same file in DDR*/
//        if (CompareMem((void *)KERNEL_DDR_BASE, (void *)KERNEL_FLASH_BASE, KERNEL_COPY_SIZE) != 0)
//        {
//            (VOID)AsciiPrint("The .kernel file check fail!\n");
//            //return;
//        }
//        else
//        {
//            (VOID)AsciiPrint("The .kernel file check sucess!\n\n");
//        }

        #ifdef NANDFLASHREAD
            CopyNandToMem((void *)FILESYSTEM_DDR_BASE, FILESYSTEM_BLOCKNUM_NANDFLASH, FILESYSTEM_COPY_SIZE);
            (VOID)AsciiPrint("The .filesystem file is transmitted ok!\n");
        #else
            memcpy((void *)FILESYSTEM_DDR_BASE, (void *)FILESYSTEM_FLASH_BASE, FILESYSTEM_COPY_SIZE);
            (VOID)AsciiPrint("The .filesystem file is transmitted ok!\n");
        #endif

//        /* compare initrd in FLASH and the same file in DDR*/
//        if (CompareMem((void *)FILESYSTEM_DDR_BASE, (void *)FILESYSTEM_FLASH_BASE, FILESYSTEM_COPY_SIZE) != 0)
//        {
//            (VOID)AsciiPrint("The .filesystem file check fail!\n");
//            //return;
//        }
//        else
//        {
//            (VOID)AsciiPrint("The .filesystem file check sucess!\n\n");
//        }

        /*---------------OS-----------------*/
        BootOptionList (&BootOptionsList);
        BootGo (&BootOptionsList);
        #endif
    }
    (VOID)AsciiPrint("Press 's', Start Boot Menu!\n\n"); 
  
  // Timer before initiating the default boot selection
  StartDefaultBootOnTimeout ();

  // Start the Boot Menu
  Status = BootMenuMain ();
  ASSERT_EFI_ERROR (Status);

}

EFI_BDS_ARCH_PROTOCOL  gBdsProtocol = {
  BdsEntry,
};

EFI_STATUS
EFIAPI
BdsInitialize (
  IN EFI_HANDLE                            ImageHandle,
  IN EFI_SYSTEM_TABLE                      *SystemTable
  )
{
  EFI_STATUS  Status;

  mImageHandle = ImageHandle;

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &ImageHandle,
                  &gEfiBdsArchProtocolGuid, &gBdsProtocol,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);

  return Status;
}