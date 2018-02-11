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

#include <sys/mman.h>

#include "out_writer.h"
#include "utils.h"
#include "vdex.h"
#include "vdex_backend_v10.h"
#include "vdex_backend_v6.h"

static void *(*initDepsInfoPtr)(const u1 *);
static void (*destroyDepsInfoPtr)(const void *);
static void (*dumpDepsInfoPtr)(const u1 *, const void *);
static int (*processPtr)(const char *, const u1 *, const runArgs_t *);

void vdex_backendInit(VdexBackend ver) {
  switch (ver) {
    case kBackendV6:
      initDepsInfoPtr = &vdex_initDepsInfo_v6;
      destroyDepsInfoPtr = &vdex_destroyDepsInfo_v6;
      dumpDepsInfoPtr = &vdex_dumpDepsInfo_v6;
      processPtr = &vdex_process_v6;
      break;
    case kBackendV10:
      initDepsInfoPtr = &vdex_initDepsInfo_v10;
      destroyDepsInfoPtr = &vdex_destroyDepsInfo_v10;
      dumpDepsInfoPtr = &vdex_dumpDepsInfo_v10;
      processPtr = &vdex_process_v10;
      break;
    default:
      LOGMSG(l_FATAL, "Invalid Vdex backend version");
  }
}

bool vdex_isMagicValid(const u1 *cursor) {
  const vdexHeader *pVdexHeader = (const vdexHeader *)cursor;
  return (memcmp(pVdexHeader->magic, kVdexMagic, sizeof(kVdexMagic)) == 0);
}

bool vdex_isVersionValid(const u1 *cursor) {
  const vdexHeader *pVdexHeader = (const vdexHeader *)cursor;
  for (u4 i = 0; i < kNumVdexVersions; i++) {
    if (memcmp(pVdexHeader->version, kVdexMagicVersions[i], kVdexVersionLen) == 0) {
      LOGMSG(l_DEBUG, "Vdex version '%s' detected", pVdexHeader->version);
      return true;
    }
  }
  return false;
}

bool vdex_isValidVdex(const u1 *cursor) {
  return vdex_isMagicValid(cursor) && vdex_isVersionValid(cursor);
}

bool vdex_hasDexSection(const u1 *cursor) {
  const vdexHeader *pVdexHeader = (const vdexHeader *)cursor;
  return pVdexHeader->dexSize != 0;
}

u4 vdex_GetSizeOfChecksumsSection(const u1 *cursor) {
  const vdexHeader *pVdexHeader = (const vdexHeader *)cursor;
  return sizeof(VdexChecksum) * pVdexHeader->numberOfDexFiles;
}

const u1 *vdex_DexBegin(const u1 *cursor) {
  return cursor + sizeof(vdexHeader) + vdex_GetSizeOfChecksumsSection(cursor);
}

u4 vdex_DexBeginOffset(const u1 *cursor) {
  return sizeof(vdexHeader) + vdex_GetSizeOfChecksumsSection(cursor);
}

const u1 *vdex_DexEnd(const u1 *cursor) {
  const vdexHeader *pVdexHeader = (const vdexHeader *)cursor;
  return vdex_DexBegin(cursor) + pVdexHeader->dexSize;
}

u4 vdex_DexEndOffset(const u1 *cursor) {
  const vdexHeader *pVdexHeader = (const vdexHeader *)cursor;
  return vdex_DexBeginOffset(cursor) + pVdexHeader->dexSize;
}

// TODO: Cache embedded Dex file offsets so that we don't have to parse from scratch when we
// want to iterate over all files.
const u1 *vdex_GetNextDexFileData(const u1 *cursor, u4 *offset) {
  if (*offset == 0) {
    if (vdex_hasDexSection(cursor)) {
      const u1 *dexBuf = vdex_DexBegin(cursor);
      *offset = sizeof(vdexHeader) + vdex_GetSizeOfChecksumsSection(cursor);
      LOGMSG(l_DEBUG, "Processing first Dex file at offset:0x%x", *offset);

      // Adjust offset to point at the end of current Dex file
      dexHeader *pDexHeader = (dexHeader *)(dexBuf);
      *offset += pDexHeader->fileSize;
      return dexBuf;
    } else {
      return NULL;
    }
  } else {
    dexHeader *pDexHeader = (dexHeader *)(cursor + *offset);

    // Check boundaries
    const u1 *dexBuf = cursor + *offset;
    const u1 *dexBufMax = dexBuf + pDexHeader->fileSize;
    if (dexBufMax == vdex_DexEnd(cursor)) {
      LOGMSG(l_DEBUG, "Processing last Dex file at offset:0x%x", *offset);
    } else if (dexBufMax <= vdex_DexEnd(cursor)) {
      LOGMSG(l_DEBUG, "Processing Dex file at offset:0x%x", *offset);
    } else {
      LOGMSG(l_ERROR, "Invalid cursor offset '0x%x'", *offset);
      return NULL;
    }

    // Adjust offset to point at the end of current Dex file
    *offset += pDexHeader->fileSize;
    return dexBuf;
  }
}

u4 vdex_GetLocationChecksum(const u1 *cursor, u4 fileIdx) {
  u4 *checksums = (u4 *)(cursor + sizeof(vdexHeader));
  return checksums[fileIdx];
}

void vdex_SetLocationChecksum(const u1 *cursor, u4 fileIdx, u4 value) {
  u4 *checksums = (u4 *)(cursor + sizeof(vdexHeader));
  checksums[fileIdx] = value;
}

const u1 *vdex_GetVerifierDepsData(const u1 *cursor) {
  const vdexHeader *pVdexHeader = (const vdexHeader *)cursor;
  return vdex_DexBegin(cursor) + pVdexHeader->dexSize;
}

u4 vdex_GetVerifierDepsDataOffset(const u1 *cursor) {
  const vdexHeader *pVdexHeader = (const vdexHeader *)cursor;
  return vdex_DexBeginOffset(cursor) + pVdexHeader->dexSize;
}

u4 vdex_GetVerifierDepsDataSize(const u1 *cursor) {
  const vdexHeader *pVdexHeader = (const vdexHeader *)cursor;
  return pVdexHeader->verifierDepsSize;
}

const u1 *vdex_GetQuickeningInfo(const u1 *cursor) {
  const vdexHeader *pVdexHeader = (const vdexHeader *)cursor;
  return vdex_GetVerifierDepsData(cursor) + pVdexHeader->verifierDepsSize;
}

u4 vdex_GetQuickeningInfoOffset(const u1 *cursor) {
  const vdexHeader *pVdexHeader = (const vdexHeader *)cursor;
  return vdex_GetVerifierDepsDataOffset(cursor) + pVdexHeader->verifierDepsSize;
}

u4 vdex_GetQuickeningInfoSize(const u1 *cursor) {
  const vdexHeader *pVdexHeader = (const vdexHeader *)cursor;
  return pVdexHeader->quickeningInfoSize;
}

void vdex_dumpHeaderInfo(const u1 *cursor) {
  const vdexHeader *pVdexHeader = (const vdexHeader *)cursor;

  LOGMSG_RAW(l_DEBUG, "------ Vdex Header Info ------\n");
  LOGMSG_RAW(l_DEBUG, "magic header & version      : %.4s-%.4s\n", pVdexHeader->magic,
             pVdexHeader->version);
  LOGMSG_RAW(l_DEBUG, "number of dex files         : %" PRIx32 " (%" PRIu32 ")\n",
             pVdexHeader->numberOfDexFiles, pVdexHeader->numberOfDexFiles);
  LOGMSG_RAW(l_DEBUG, "dex size (overall)          : %" PRIx32 " (%" PRIu32 ")\n",
             pVdexHeader->dexSize, pVdexHeader->dexSize);
  LOGMSG_RAW(l_DEBUG, "verifier dependencies size  : %" PRIx32 " (%" PRIu32 ")\n",
             vdex_GetVerifierDepsDataSize(cursor), vdex_GetVerifierDepsDataSize(cursor));
  LOGMSG_RAW(l_DEBUG, "verifier dependencies offset: %" PRIx32 " (%" PRIu32 ")\n",
             vdex_GetVerifierDepsDataOffset(cursor), vdex_GetVerifierDepsDataOffset(cursor));
  LOGMSG_RAW(l_DEBUG, "quickening info size        : %" PRIx32 " (%" PRIu32 ")\n",
             vdex_GetQuickeningInfoSize(cursor), vdex_GetQuickeningInfoSize(cursor));
  LOGMSG_RAW(l_DEBUG, "quickening info offset      : %" PRIx32 " (%" PRIu32 ")\n",
             vdex_GetQuickeningInfoOffset(cursor), vdex_GetQuickeningInfoOffset(cursor));
  LOGMSG_RAW(l_DEBUG, "dex files info              :\n");

  for (u4 i = 0; i < pVdexHeader->numberOfDexFiles; ++i) {
    LOGMSG_RAW(l_DEBUG, "  [%" PRIu32 "] location checksum : %" PRIx32 " (%" PRIu32 ")\n", i,
               vdex_GetLocationChecksum(cursor, i), vdex_GetLocationChecksum(cursor, i));
  }
  LOGMSG_RAW(l_DEBUG, "---- EOF Vdex Header Info ----\n");
}

int vdex_process(const char *VdexFileName, const u1 *cursor, const runArgs_t *pRunArgs) {
  // Measure time spend to process all Dex files of a Vdex file
  struct timespec timer;
  utils_startTimer(&timer);

  // Process Vdex file
  int ret = (*processPtr)(VdexFileName, cursor, pRunArgs);

  // Get elapsed time in ns
  long timeSpend = utils_endTimer(&timer);
  LOGMSG(l_DEBUG, "Took %ld ms to process Vdex file", timeSpend / 1000000);

  return ret;
}

void *vdex_initDepsInfo(const u1 *vdexFileBuf) { return (*initDepsInfoPtr)(vdexFileBuf); }

void vdex_destroyDepsInfo(const void *dataPtr) { (*destroyDepsInfoPtr)(dataPtr); }

void vdex_dumpDepsInfo(const u1 *vdexFileBuf, const void *dataPtr) {
  (*dumpDepsInfoPtr)(vdexFileBuf, dataPtr);
}

bool vdex_updateChecksums(const char *inVdexFileName,
                          int nCsums,
                          u4 *checksums,
                          const runArgs_t *pRunArgs) {
  bool ret = false;
  off_t fileSz = 0;
  int srcfd = -1;
  u1 *buf = NULL;

  buf = utils_mapFileToRead(inVdexFileName, &fileSz, &srcfd);
  if (buf == NULL) {
    LOGMSG(l_ERROR, "'%s' open & map failed", inVdexFileName);
    return ret;
  }

  if (!vdex_isValidVdex(buf)) {
    LOGMSG(l_WARN, "'%s' is an invalid Vdex file", inVdexFileName);
    goto fini;
  }

  const vdexHeader *pVdexHeader = (const vdexHeader *)buf;
  if ((u4)nCsums != pVdexHeader->numberOfDexFiles) {
    LOGMSG(l_ERROR, "%d checksums loaded from file, although Vdex has %" PRIu32 " Dex entries",
           nCsums, pVdexHeader->numberOfDexFiles)
    goto fini;
  }

  for (u4 i = 0; i < pVdexHeader->numberOfDexFiles; ++i) {
    vdex_SetLocationChecksum(buf, i, checksums[i]);
  }

  if (!outWriter_VdexFile(pRunArgs, inVdexFileName, buf, fileSz)) {
    LOGMSG(l_ERROR, "Failed to write updated Vdex file");
    goto fini;
  }

  ret = true;

fini:
  munmap(buf, fileSz);
  close(srcfd);
  return ret;
}
