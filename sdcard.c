/*
 * Copyright (c) 2011, Takashi TOYOSHIMA <toyoshim@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the authors nor the names of its contributors may be
 *   used to endorse or promote products derived from this software with out
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUE
 * NTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#include "sdcard.h"
#if defined(EFI)
# include <efi.h>
#elif defined(UBOOT)
# include <common.h>
# include <exports.h>
#elif defined(TEST) // defined(EFI)
# include <stdio.h>
#else // defined(TEST)
# include <avr/io.h>
#endif // defined(TEST)

#define PORT PORTC
#define DDR  DDRC
#define PIN  PINC
#define P_CK _BV(PC2)
#define P_DI _BV(PC3)
#define P_PU _BV(PC4)
#define P_DO _BV(PC4)
#define P_CS _BV(PC5)

static unsigned char
buffer[512];

static unsigned short
crc;

static unsigned long
cur_blk;

#if defined(EFI)
extern EFI_FILE_HANDLE efi_fs;
EFI_FILE_HANDLE fp = NULL;
#elif defined(UBOOT) // defined(EFI)
#elif defined(TEST) // defined(UBOOT)
static FILE *fp = NULL;
#else // defined(TEST)
static void
sd_out
(char c)
{
  int i;
  for (i = 7; i >= 0; i--) {
    char out = (0 == (c & (1 << i)))? 0: P_DI;
    PORT = (PORT & ~(P_CK | P_DI)) | out;
    PORT |=  P_CK;
  }
}

static int
sd_busy
(char f)
{
  unsigned long timeout = 0xffff;
  for (; 0 != timeout; timeout--) {
    char c;
    PORT &= ~P_CK;
    c = PIN;
    PORT |=  P_CK;
    if ((f & P_DO) == (c & P_DO)) return 0;
  }
  return -1;
}

static char
sd_in
(char n)
{
  int i;
  int rc = 0;
  for (i = 0; i < n; i++) {
    char c;
    PORT &= ~P_CK;
    c = PIN;
    PORT |= P_CK;
    rc <<= 1;
    if (0 != (c & P_DO)) rc |= 1;
  }
  return rc;
}

static char
sd_cmd
(char cmd, char arg0, char arg1, char arg2, char arg3, char crc)
{
  PORT = (PORT | P_DI | P_CK) & ~P_CS;
  sd_out(cmd);
  sd_out(arg0);
  sd_out(arg1);
  sd_out(arg2);
  sd_out(arg3);
  sd_out(crc);
  PORT |= P_DI;
  if (sd_busy(0) < 0) return -1;
  return sd_in(7);
}
#endif // defined(TEST)

void
sdcard_init
(void)
{
#if defined(EFI) || defined(UBOOT)
#elif defined(TEST) // defined(EFI) || defined(UBOOT)
  if (NULL != fp) fclose(fp);
  fp = NULL;
#else // defined(TEST)
  /*
   * PC2: CK out
   * PC3: DI out
   * PC4: DO in
   * PC5: CS out
   */
  // Port Settings
  DDR  &= ~P_DO;
  DDR  |=  (P_CK | P_DI | P_CS);
  PORT &= ~(P_CK | P_DI | P_CS);
  PORT |=  P_PU;
#endif // defined(TEST)
}

int
sdcard_open
(void)
{
#if defined(EFI)
  UINT64 mode = EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE;
  EFI_STATUS status = uefi_call_wrapper(
      efi_fs->Open, 5, efi_fs, &fp, L"EFI\\cpmega88\\sdcard.img", mode, 0);
  if (!EFI_ERROR(status)) return 0;
  status = uefi_call_wrapper(
      efi_fs->Open, 5, efi_fs, &fp, L"sdcard.img", mode, 0);
  return EFI_ERROR(status) ? -1 : 0;
#elif defined(UBOOT)  // defined(EFI)
  return 0;
#elif defined(TEST) // defined(UBOOT)
  fp = fopen("sdcard.img", "r+");
  if (NULL == fp) fp = fopen("sdcard.img", "r");
  if (NULL == fp) return -1;
  return 0;
#else // defined(TEST)
  // initial clock
  PORT |= P_DI | P_CS;
  int i;
  for (i = 0; i < 80; i++) {
    PORT &= ~P_CK;
    PORT |=  P_CK;
  }
  char rc;
  // cmd0
  rc = sd_cmd(0x40, 0x00, 0x00, 0x00, 0x00, 0x95);
  if (1 != rc) return -1;
  // cmd1
  unsigned short timeout = 0xffff;
  for (; timeout != 0; timeout--) {
    rc = sd_cmd(0x41, 0x00, 0x00, 0x00, 0x00, 0x00);
    if (0 == rc) break;
    if (-1 == rc) return -2;
  }
  if (0 != rc) return -3;

  PORT &= ~P_CK;
  return 0;
#endif // defined(TEST)
}

int
sdcard_fetch
(unsigned long blk_addr)
{
#if !defined(UBOOT) && defined(TEST)
  if (NULL == fp) return -1;
#endif // !defined(UBOOT)
  if (0 != (blk_addr & 0x1ff)) return -2;
#if defined(EFI)
  EFI_STATUS status = uefi_call_wrapper(fp->SetPosition, 2, fp, blk_addr);
  if (EFI_ERROR(status)) return -3;
  UINTN size = 512;
  status = uefi_call_wrapper(fp->Read, 3, fp, &size, buffer);
  if (EFI_ERROR(status) || 512 != size) return -4;
  cur_blk = blk_addr;
  return 0;
#elif defined(UBOOT) // defined(EFI)
  unsigned char* image = (unsigned char*)SDCARD_IMAGE_BASE;
  unsigned char* block = &image[blk_addr];
  size_t i;
  for (i = 0; i < 512; i++)
    buffer[i] = block[i];
  cur_blk = blk_addr;
  return 0;
#elif defined(TEST) // defined(UBOOT)
  if (0 != fseek(fp, blk_addr, SEEK_SET)) return -3;
  if (512 != fread(buffer, 1, 512, fp)) return -4;
  cur_blk = blk_addr;
  return 0;
#else // defined(TEST)
  // cmd17
  char rc = sd_cmd(0x51, blk_addr >> 24, blk_addr >> 16, blk_addr >> 8, blk_addr, 0x00);
  if (0 != rc) return -1;
  if (sd_busy(0) < 0) return -2;
  int i;
  for (i = 0; i < 512; i++) buffer[i] = sd_in(8);
  rc = sd_in(8);
  crc = (rc << 8) | sd_in(8);

  // XXX: rc check

  PORT &= ~P_CK;

  cur_blk = blk_addr;
  return 0;
#endif // defined(TEST)
}

int
sdcard_store
(unsigned long blk_addr)
{
#if !defined(UBOOT) & defined(TEST)
  if (NULL == fp) return -1;
#endif // !defined(UBOOT)
  if (0 != (blk_addr & 0x1ff)) return -2;
#if defined(EFI)
  EFI_STATUS status = uefi_call_wrapper(fp->SetPosition, 2, fp, blk_addr);
  if (EFI_ERROR(status)) return -3;
  UINTN size = 512;
  status = uefi_call_wrapper(fp->Write, 3, fp, &size, buffer);
  if (EFI_ERROR(status) || 512 != size) return -4;
  return 0;
#elif defined(UBOOT) // defined(EFI)
  unsigned char* image = (unsigned char*)SDCARD_IMAGE_BASE;
  unsigned char* block = &image[blk_addr];
  size_t i;
  // Just copy back to the original place. The data couldn't be persistent.
  for (i = 0; i < 512; i++)
    block[i] = buffer[i];
  return 0;
#elif defined(TEST) // defined(UBOOT)
  if (0 != fseek(fp, blk_addr, SEEK_SET)) return -3;
  if (512 != fwrite(buffer, 1, 512, fp)) return -4;
  return 0;
#else // defined(TEST)
  // cmd24
  char rc = sd_cmd(0x58, blk_addr >> 24, blk_addr >> 16, blk_addr >> 8, blk_addr, 0x00);
  if (0 != rc) return -1;
  sd_out(0xff); // blank 1B
  sd_out(0xfe); // Data Token
  int i;
  for (i = 0; i < 512; i++) sd_out(buffer[i]); // Data Block
  sd_out(0xff); // CRC dummy 1/2
  sd_out(0xff); // CRC dummy 2/2
  if (sd_busy(0) < 0) return -3;
  rc = sd_in(4);
  if (sd_busy(~0) < 0) return -4;

  // XXX: rc check
  // 0 0101: accepted
  // 0 1011: CRC error
  // 0 1101: write error

  PORT &= ~P_CK;

  return rc; // test result code
#endif // defined(TEST)
}

unsigned char
sdcard_read
(unsigned short offset)
{
  return buffer[offset];
}

void
sdcard_write
(unsigned short offset, unsigned char data)
{
  buffer[offset] = data;
}

unsigned short
sdcard_crc
(void)
{
  return crc;
}

int
sdcard_flush
(void)
{
  return sdcard_store(cur_blk);
}