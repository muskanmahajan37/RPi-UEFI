/** @file  NorFlashDxe.h

  Copyright (c) 2011 - 2014, ARM Ltd. All rights reserved.<BR>

  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#ifndef __NOR_FLASH_DXE_H__
#define __NOR_FLASH_DXE_H__


#include <Base.h>
#include <PiDxe.h>

#include <Protocol/BlockIo.h>
#include <Protocol/FirmwareVolumeBlock.h>

#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/NorFlashPlatformLib.h>
#include <Library/UefiLib.h>

#define NOR_FLASH_ERASE_RETRY                     10

#define MAX_RUNTIME_BUFFER_SIZE                   0x100000

// Device access macros
// These are necessary because we use 2 x 16bit parts to make up 32bit data

#define HIGH_16_BITS                              0xFFFF0000
#define LOW_16_BITS                               0x0000FFFF
#define LOW_8_BITS                                0x000000FF

#define FOLD_32BIT_INTO_16BIT(value)              ( ( value >> 16 ) | ( value & LOW_16_BITS ) )

#define GET_LOW_BYTE(value)                       ( value & LOW_8_BITS )
#define GET_HIGH_BYTE(value)                      ( GET_LOW_BYTE( value >> 16 ) )

// Each command must be sent simultaneously to both chips,
// i.e. at the lower 16 bits AND at the higher 16 bits
#if !defined(APM_XGENE_GFC_FLASH) && !defined(APM_XGENE_SPI_FLASH)
#define CREATE_NOR_ADDRESS(BaseAddr,OffsetAddr)   ((BaseAddr) + ((OffsetAddr) << 2))
#define CREATE_DUAL_CMD(Cmd)                      ( ( Cmd << 16) | ( Cmd & LOW_16_BITS) )
#define SEND_NOR_COMMAND(BaseAddr,Offset,Cmd) MmioWrite32 (CREATE_NOR_ADDRESS(BaseAddr,Offset), CREATE_DUAL_CMD(Cmd))
#else
#define CREATE_NOR_ADDRESS(BaseAddr,OffsetAddr)   ((BaseAddr) + ((OffsetAddr) << 1))
#endif

#define GET_NOR_BLOCK_ADDRESS(BaseAddr,Lba,LbaSize)( BaseAddr + (UINTN)((Lba) * LbaSize) )

#ifdef APM_XGENE_GFC_FLASH
// Status Register Bits
#define P30_SR_BIT_WRITE                          (BIT7 << 16 | BIT7)
#define P30_SR_BIT_ERASE_SUSPEND                  (BIT6 << 16 | BIT6)
#define P30_SR_BIT_ERASE                          (BIT5 << 16 | BIT5)
#define P30_SR_BIT_PROGRAM                        (BIT4 << 16 | BIT4)
#define P30_SR_BIT_VPP                            (BIT3 << 16 | BIT3)
#define P30_SR_BIT_PROGRAM_SUSPEND                (BIT2 << 16 | BIT2)
#define P30_SR_BIT_BLOCK_LOCKED                   (BIT1 << 16 | BIT1)
#define P30_SR_BIT_BEFP                           (BIT0 << 16 | BIT0)

// Device Commands for Intel StrataFlash(R) Embedded Memory (P30) Family

// On chip buffer size for buffered programming operations
// There are 2 chips, each chip can buffer up to 32 (16-bit)words, and each word is 2 bytes.
// Therefore the total size of the buffer is 2 x 32 x 2 = 128 bytes
#define P30_MAX_BUFFER_SIZE_IN_BYTES              ((UINTN)128)
#define P30_MAX_BUFFER_SIZE_IN_WORDS              (P30_MAX_BUFFER_SIZE_IN_BYTES/((UINTN)4))
#define MAX_BUFFERED_PROG_ITERATIONS              10000000
#define BOUNDARY_OF_32_WORDS                      0x7F

// CFI Addresses
#define P30_CFI_ADDR_QUERY_UNIQUE_QRY             0x10
#define P30_CFI_ADDR_VENDOR_ID                    0x13

// CFI Data
#define CFI_QRY                                   0x00595251

// READ Commands
#define P30_CMD_READ_DEVICE_ID                    0x0090
#define P30_CMD_READ_STATUS_REGISTER              0x0070
#define P30_CMD_CLEAR_STATUS_REGISTER             0x0050
#define P30_CMD_READ_ARRAY                        0x00FF
#define P30_CMD_READ_CFI_QUERY                    0x0098

// WRITE Commands
#define P30_CMD_WORD_PROGRAM_SETUP                0x0040
#define P30_CMD_ALTERNATE_WORD_PROGRAM_SETUP      0x0010
#define P30_CMD_BUFFERED_PROGRAM_SETUP            0x00E8
#define P30_CMD_BUFFERED_PROGRAM_CONFIRM          0x00D0
#define P30_CMD_BEFP_SETUP                        0x0080
#define P30_CMD_BEFP_CONFIRM                      0x00D0

// ERASE Commands
#define P30_CMD_BLOCK_ERASE_SETUP                 0x0020
#define P30_CMD_BLOCK_ERASE_CONFIRM               0x00D0

// SUSPEND Commands
#define P30_CMD_PROGRAM_OR_ERASE_SUSPEND          0x00B0
#define P30_CMD_SUSPEND_RESUME                    0x00D0

// BLOCK LOCKING / UNLOCKING Commands
#define P30_CMD_LOCK_BLOCK_SETUP                  0x0060
#define P30_CMD_LOCK_BLOCK                        0x0001
#define P30_CMD_UNLOCK_BLOCK                      0x00D0
#define P30_CMD_LOCK_DOWN_BLOCK                   0x002F

// PROTECTION Commands
#define P30_CMD_PROGRAM_PROTECTION_REGISTER_SETUP 0x00C0

// CONFIGURATION Commands
#define P30_CMD_READ_CONFIGURATION_REGISTER_SETUP 0x0060
#define P30_CMD_READ_CONFIGURATION_REGISTER       0x0003

#endif

#define SEND_NOR_COMMAND(BaseAddr,Offset,Cmd)  NorCmdSet2SendNorCmd(BaseAddr,Offset,Cmd)

#define SEND_TO_NOR(BaseAddr,Offset,Data) \
	*((volatile UINT16 *)(CREATE_NOR_ADDRESS(BaseAddr,Offset))) = Data; \
        asm volatile("dsb sy":::); \
        asm volatile("isb":::)

#define VALID_NOR_ADDR(Addr, Base) ((((Addr) >= (Base)) && ((Addr) < (Base)  + APM_XGENE_SYS_FLASH_LEN)) ? 1 : 0)
// Device Commands for Spansion S29WS-N (CFI CMDSET2 - AMD)

enum NorFlashCmd {

/* READ Commands  */
CMDSET2_CMD_READ_DEVICE_ID,
CMDSET2_CMD_READ_STATUS_REGISTER,
CMDSET2_CMD_CLEAR_STATUS_REGISTER,
CMDSET2_CMD_READ_ARRAY,
CMDSET2_CMD_READ_CFI_QUERY,

/* WRITE Commands */
CMDSET2_CMD_WORD_PROGRAM_SETUP,
CMDSET2_CMD_ALTERNATE_WORD_PROGRAM_SETUP,
CMDSET2_CMD_BUFFERED_PROGRAM_SETUP,
CMDSET2_CMD_BUFFERED_PROGRAM_CONFIRM,
CMDSET2_CMD_BEFP_SETUP,
CMDSET2_CMD_BEFP_CONFIRM,

/* ERASE Commands */
CMDSET2_CMD_BLOCK_ERASE_SETUP,
CMDSET2_CMD_BLOCK_ERASE_CONFIRM,

/* SUSPEND Commands */
CMDSET2_CMD_PROGRAM_OR_ERASE_SUSPEND,
CMDSET2_CMD_SUSPEND_RESUME,

/* BLOCK LOCKING / UNLOCKING Commands */
CMDSET2_CMD_LOCK_BLOCK_SETUP,
CMDSET2_CMD_LOCK_BLOCK,
CMDSET2_CMD_UNLOCK_BLOCK,
CMDSET2_CMD_LOCK_DOWN_BLOCK,

/* PROTECTION Commands */
CMDSET2_CMD_PROGRAM_PROTECTION_REGISTER_SETUP,

/* CONFIGURATION Commands  */
CMDSET2_CMD_READ_CONFIGURATION_REGISTER_SETUP,
CMDSET2_CMD_READ_CONFIGURATION_REGISTER,

};

#define MaxNorCmdSet2Cycles 6

struct NorCmdSequence {
	UINT16 NorBusCycleAddr[MaxNorCmdSet2Cycles];
	UINT16 NorBusCycleData[MaxNorCmdSet2Cycles];
};

// Status Register Bits
#define CMDSET2_SR_BIT_WRITE                          (BIT7 << 16 | BIT7)
#define CMDSET2_SR_BIT_ERASE_SUSPEND                  (BIT6 << 16 | BIT6)
#define CMDSET2_SR_BIT_ERASE                          (BIT5 << 16 | BIT5)
#define CMDSET2_SR_BIT_PROGRAM                        (BIT4 << 16 | BIT4)
#define CMDSET2_SR_BIT_VPP                            (BIT3 << 16 | BIT3)
#define CMDSET2_SR_BIT_PROGRAM_SUSPEND                (BIT2 << 16 | BIT2)
#define CMDSET2_SR_BIT_BLOCK_LOCKED                   (BIT1 << 16 | BIT1)
#define CMDSET2_SR_BIT_BEFP                           (BIT0 << 16 | BIT0)


#define CMDSET2_MAX_BUFFER_SIZE_IN_BYTES              ((UINTN)64)
#define CMDSET2_MAX_BUFFER_SIZE_IN_WORDS              (CMDSET2_MAX_BUFFER_SIZE_IN_BYTES/((UINTN)2))
#define MAX_BUFFERED_PROG_ITERATIONS              10000000
#define BOUNDARY_OF_32_WORDS                      0x7F

// CFI Addresses
#define CMDSET2_CFI_ADDR_QUERY_UNIQUE_QRY             0x10
#define CMDSET2_CFI_ADDR_VENDOR_ID                    0x13

#define NOR_FLASH_SIGNATURE                       SIGNATURE_32('n', 'o', 'r', '0')
#define INSTANCE_FROM_FVB_THIS(a)                 CR(a, NOR_FLASH_INSTANCE, FvbProtocol, NOR_FLASH_SIGNATURE)
#define INSTANCE_FROM_BLKIO_THIS(a)               CR(a, NOR_FLASH_INSTANCE, BlockIoProtocol, NOR_FLASH_SIGNATURE)

typedef struct _NOR_FLASH_INSTANCE                NOR_FLASH_INSTANCE;

typedef EFI_STATUS (*NOR_FLASH_INITIALIZE)        (NOR_FLASH_INSTANCE* Instance);

typedef struct {
  VENDOR_DEVICE_PATH                  Vendor;
  EFI_DEVICE_PATH_PROTOCOL            End;
} NOR_FLASH_DEVICE_PATH;

struct _NOR_FLASH_INSTANCE {
  UINT32                              Signature;
  EFI_HANDLE                          Handle;

  BOOLEAN                             Initialized;
  NOR_FLASH_INITIALIZE                Initialize;

  UINTN                               DeviceBaseAddress;
  UINTN                               RegionBaseAddress;
  UINTN                               Size;
  EFI_LBA                             StartLba;

  EFI_BLOCK_IO_PROTOCOL               BlockIoProtocol;
  EFI_BLOCK_IO_MEDIA                  Media;

  BOOLEAN                             SupportFvb;
  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL FvbProtocol;

  NOR_FLASH_DEVICE_PATH	              DevicePath;
  EFI_PHYSICAL_ADDRESS                PhysicalAddress;
};

EFI_STATUS
NorFlashReadCfiData (
  IN  UINTN                   DeviceBaseAddress,
  IN  UINTN                   CFI_Offset,
  IN  UINT32                  NumberOfBytes,
  OUT UINT32                  *Data
  );

EFI_STATUS
NorFlashWriteBuffer (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN UINTN                  TargetAddress,
  IN UINTN                  BufferSizeInBytes,
  IN UINT32                 *Buffer
  );

//
// BlockIO Protocol function EFI_BLOCK_IO_PROTOCOL.Reset
//
EFI_STATUS
EFIAPI
NorFlashBlockIoReset (
  IN EFI_BLOCK_IO_PROTOCOL    *This,
  IN BOOLEAN                  ExtendedVerification
  );

//
// BlockIO Protocol function EFI_BLOCK_IO_PROTOCOL.ReadBlocks
//
EFI_STATUS
EFIAPI
NorFlashBlockIoReadBlocks (
  IN  EFI_BLOCK_IO_PROTOCOL   *This,
  IN  UINT32                  MediaId,
  IN  EFI_LBA                 Lba,
  IN  UINTN                   BufferSizeInBytes,
  OUT VOID                    *Buffer
);

//
// BlockIO Protocol function EFI_BLOCK_IO_PROTOCOL.WriteBlocks
//
EFI_STATUS
EFIAPI
NorFlashBlockIoWriteBlocks (
  IN  EFI_BLOCK_IO_PROTOCOL   *This,
  IN  UINT32                  MediaId,
  IN  EFI_LBA                 Lba,
  IN  UINTN                   BufferSizeInBytes,
  IN  VOID                    *Buffer
);

//
// BlockIO Protocol function EFI_BLOCK_IO_PROTOCOL.FlushBlocks
//
EFI_STATUS
EFIAPI
NorFlashBlockIoFlushBlocks (
  IN EFI_BLOCK_IO_PROTOCOL    *This
);


//
// NorFlashFvbDxe.c
//

EFI_STATUS
EFIAPI
NorFlashFvbInitialize (
  IN NOR_FLASH_INSTANCE*                            Instance
  );

EFI_STATUS
EFIAPI
FvbGetAttributes(
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL     *This,
  OUT       EFI_FVB_ATTRIBUTES_2                    *Attributes
  );

EFI_STATUS
EFIAPI
FvbSetAttributes(
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL     *This,
  IN OUT    EFI_FVB_ATTRIBUTES_2                    *Attributes
  );

EFI_STATUS
EFIAPI
FvbGetPhysicalAddress(
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL     *This,
  OUT       EFI_PHYSICAL_ADDRESS                    *Address
  );

EFI_STATUS
EFIAPI
FvbGetMappedAddress(
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL     *This,
  OUT       EFI_PHYSICAL_ADDRESS                    *Address
  );

EFI_STATUS
EFIAPI
FvbGetBlockSize(
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL     *This,
  IN        EFI_LBA                                 Lba,
  OUT       UINTN                                   *BlockSize,
  OUT       UINTN                                   *NumberOfBlocks
  );

EFI_STATUS
EFIAPI
FvbRead(
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL     *This,
  IN        EFI_LBA                                 Lba,
  IN        UINTN                                   Offset,
  IN OUT    UINTN                                   *NumBytes,
  IN OUT    UINT8                                   *Buffer
  );

EFI_STATUS
EFIAPI
FvbWrite(
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL     *This,
  IN        EFI_LBA                                 Lba,
  IN        UINTN                                   Offset,
  IN OUT    UINTN                                   *NumBytes,
  IN        UINT8                                   *Buffer
  );

EFI_STATUS
EFIAPI
FvbEraseBlocks(
  IN CONST  EFI_FIRMWARE_VOLUME_BLOCK2_PROTOCOL     *This,
  ...
  );

//
// NorFlashDxe.c
//

EFI_STATUS
NorFlashUnlockAndEraseSingleBlock (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN UINTN                  BlockAddress
  );

EFI_STATUS
NorFlashWriteBlocks (
  IN  NOR_FLASH_INSTANCE *Instance,
  IN  EFI_LBA           Lba,
  IN  UINTN             BufferSizeInBytes,
  IN  VOID              *Buffer,
  IN  BOOLEAN           Erase
  );

EFI_STATUS
NorFlashReadBlocks (
  IN NOR_FLASH_INSTANCE   *Instance,
  IN EFI_LBA              Lba,
  IN UINTN                BufferSizeInBytes,
  OUT VOID                *Buffer
  );

EFI_STATUS
NorFlashReset (
  IN  NOR_FLASH_INSTANCE *Instance
  );

#if !defined(APM_XGENE_GFC_FLASH) && !defined(APM_XGENE_SPI_FLASH)

EFI_STATUS
NorFlashWriteSingleBlock (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN EFI_LBA                Lba,
  IN UINT32                 *DataBuffer,
  IN UINT32                 BlockSizeInWords
  );

#else

EFI_STATUS
NorFlashWriteMultipleBuffers (
  IN NOR_FLASH_INSTANCE     *Instance,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSizeInBytes,
  IN UINT8                  *Buffer
  );
#endif

#endif /* __NOR_FLASH_DXE_H__ */