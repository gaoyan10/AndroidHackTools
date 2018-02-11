/*

   vdexExtractor
   -----------------------------------------

   Anestis Bechtsoudis <anestis@census-labs.com>
   Copyright 2017 by CENSUS S.A. All Rights Reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

*/

#ifndef _VDEX_H_
#define _VDEX_H_

#include <zlib.h>
#include "common.h"
#include "dex.h"

#define kUnresolvedMarker (u2)(-1)

#define kNumVdexVersions 2
#define kVdexVersionLen 4

static const u1 kVdexMagic[] = { 'v', 'd', 'e', 'x' };
static const u1 kVdexMagicVersions[kNumVdexVersions][kVdexVersionLen] = {
  // Vdex version 006: API-26 Android "O".
  { '0', '0', '6', '\0' },
  // Vdex version 010: API-27 Android "O".
  { '0', '1', '0', '\0' },
};

typedef enum { kBackendV6 = 0, kBackendV10, kBackendMax } VdexBackend;

typedef u4 VdexChecksum;

typedef struct __attribute__((packed)) {
  u1 magic[4];
  u1 version[4];
  u4 numberOfDexFiles;
  u4 dexSize;
  u4 verifierDepsSize;
  u4 quickeningInfoSize;
} vdexHeader;

// VDEX files contain extracted DEX files. The VdexFile class maps the file to
// memory and provides tools for accessing its individual sections.
//
// File format:
//   VdexFile::Header    fixed-length header
//
//   DEX[0]              array of the input DEX files
//   DEX[1]              the bytecode may have been quickened
//   ...
//   DEX[D]
//   QuickeningInfo
//     uint8[]                     quickening data
//     unaligned_uint32_t[2][]     table of offsets pair:
//                                    uint32_t[0] contains code_item_offset
//                                    uint32_t[1] contains quickening data offset from the start
//                                                of QuickeningInfo
//     unalgined_uint32_t[D]       start offsets (from the start of QuickeningInfo) in previous
//                                 table for each dex file

typedef struct __attribute__((packed)) {
  vdexHeader *pVdexHeader;
  dexHeader *pDexFiles;
} vdexFile;

typedef struct __attribute__((packed)) {
  u4 numberOfStrings;
  const char **strings;
} vdexDepStrings;

typedef struct __attribute__((packed)) {
  u4 dstIndex;
  u4 srcIndex;
} vdexDepSet;

typedef struct __attribute__((packed)) {
  u2 typeIdx;
  u2 accessFlags;
} vdexDepClassRes;

typedef struct __attribute__((packed)) {
  u4 numberOfEntries;
  vdexDepSet *pVdexDepSets;
} vdexDepTypeSet;

typedef struct __attribute__((packed)) {
  u4 fieldIdx;
  u2 accessFlags;
  u4 declaringClassIdx;
} vdexDepFieldRes;

typedef struct __attribute__((packed)) {
  u4 methodIdx;
  u2 accessFlags;
  u4 declaringClassIdx;
} vdexDepMethodRes;

typedef struct __attribute__((packed)) { u2 typeIdx; } vdexDepUnvfyClass;

typedef struct __attribute__((packed)) {
  u4 numberOfEntries;
  vdexDepClassRes *pVdexDepClasses;
} vdexDepClassResSet;

typedef struct __attribute__((packed)) {
  u4 numberOfEntries;
  vdexDepFieldRes *pVdexDepFields;
} vdexDepFieldResSet;

typedef struct __attribute__((packed)) {
  u4 numberOfEntries;
  vdexDepMethodRes *pVdexDepMethods;
} vdexDepMethodResSet;

typedef struct __attribute__((packed)) {
  u4 numberOfEntries;
  vdexDepUnvfyClass *pVdexDepUnvfyClasses;
} vdexDepUnvfyClassesSet;

// Verify if valid Vdex file
bool vdex_isValidVdex(const u1 *);
bool vdex_isMagicValid(const u1 *);
bool vdex_isVersionValid(const u1 *);

bool vdex_hasDexSection(const u1 *);
u4 vdex_GetSizeOfChecksumsSection(const u1 *);
const u1 *vdex_DexBegin(const u1 *);
u4 vdex_DexBeginOffset(const u1 *);
const u1 *vdex_DexEnd(const u1 *);
u4 vdex_DexEndOffset(const u1 *);
const u1 *vdex_GetNextDexFileData(const u1 *, u4 *);
u4 vdex_GetLocationChecksum(const u1 *, u4);
void vdex_SetLocationChecksum(const u1 *, u4, u4);
const u1 *vdex_GetVerifierDepsData(const u1 *);
u4 vdex_GetVerifierDepsDataOffset(const u1 *);
u4 vdex_GetVerifierDepsDataSize(const u1 *);
const u1 *vdex_GetQuickeningInfo(const u1 *);
u4 vdex_GetQuickeningInfoSize(const u1 *);
u4 vdex_GetQuickeningInfoOffset(const u1 *);

void vdex_dumpHeaderInfo(const u1 *);

void *vdex_initDepsInfo(const u1 *);
void vdex_destroyDepsInfo(const void *);
void vdex_dumpDepsInfo(const u1 *, const void *);

void vdex_backendInit(VdexBackend);
int vdex_process(const char *, const u1 *, const runArgs_t *);

bool vdex_updateChecksums(const char *, int, u4 *, const runArgs_t *);

#endif
