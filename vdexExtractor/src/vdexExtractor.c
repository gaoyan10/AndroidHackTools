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

#include <getopt.h>
#include <libgen.h>
#include <sys/mman.h>

#include "common.h"
#include "log.h"
#include "utils.h"
#include "vdex.h"

// exit() wrapper
void exitWrapper(int errCode) {
  log_closeLogFile();
  exit(errCode);
}

// clang-format off
static void usage(bool exit_success) {
  LOGMSG_RAW(l_INFO, "              " PROG_NAME " ver. " PROG_VERSION "\n");
  LOGMSG_RAW(l_INFO, PROG_AUTHORS "\n\n");
  LOGMSG_RAW(l_INFO,"%s",
             " -i, --input=<path>   : input dir (1 max depth) or single file\n"
             " -o, --output=<path>  : output path (default is same as input)\n"
             " -f, --file-override  : allow output file override if already exists (default: false)\n"
             " --no-unquicken       : disable unquicken bytecode decompiler (don't de-odex)\n"
             " --deps               : dump verified dependencies information\n"
             " --dis                : enable bytecode disassembler\n"
             " --new-crc=<path>     : text file with extracted Apk or Dex file location checksum(s)\n"
             " -v, --debug=LEVEL    : log level (0 - FATAL ... 4 - DEBUG), default: '3' (INFO)\n"
             " -l, --log-file=<path>: save disassembler and/or verified dependencies output to log "
                                     "file (default is STDOUT)\n"
             " -h, --help           : this help\n");

  if (exit_success)
    exitWrapper(EXIT_SUCCESS);
  else
    exitWrapper(EXIT_FAILURE);
}
// clang-format on

static bool selectVdexBackend(const u1 *cursor) {
  const vdexHeader *pVdexHeader = (const vdexHeader *)cursor;

  VdexBackend ver = kBackendMax;
  char *end;
  switch (strtol((char *)pVdexHeader->version, &end, 10)) {
    case 6:
      ver = kBackendV6;
      break;
    case 10:
      ver = kBackendV10;
      break;
    default:
      LOGMSG(l_ERROR, "Invalid Vdex version");
      return false;
  }
  vdex_backendInit(ver);
  return true;
}

int main(int argc, char **argv) {
  int c;
  int logLevel = l_INFO;
  const char *logFile = NULL;
  runArgs_t pRunArgs = {
    .outputDir = NULL,
    .fileOverride = false,
    .unquicken = true,
    .enableDisassembler = false,
    .dumpDeps = false,
    .newCrcFile = NULL,
  };
  infiles_t pFiles = {
    .inputFile = NULL, .files = NULL, .fileCnt = 0,
  };

  if (argc < 1) usage(true);

  struct option longopts[] = { { "input", required_argument, 0, 'i' },
                               { "output", required_argument, 0, 'o' },
                               { "file-override", no_argument, 0, 'f' },
                               { "no-unquicken", no_argument, 0, 0x101 },
                               { "dis", no_argument, 0, 0x102 },
                               { "deps", no_argument, 0, 0x103 },
                               { "new-crc", required_argument, 0, 0x104 },
                               { "debug", required_argument, 0, 'v' },
                               { "log-file", required_argument, 0, 'l' },
                               { "help", no_argument, 0, 'h' },
                               { 0, 0, 0, 0 } };

  while ((c = getopt_long(argc, argv, "i:o:fv:l:h?", longopts, NULL)) != -1) {
    switch (c) {
      case 'i':
        pFiles.inputFile = optarg;
        break;
      case 'o':
        pRunArgs.outputDir = optarg;
        break;
      case 'f':
        pRunArgs.fileOverride = true;
        break;
      case 0x101:
        pRunArgs.unquicken = false;
        break;
      case 0x102:
        pRunArgs.enableDisassembler = true;
        break;
      case 0x103:
        pRunArgs.dumpDeps = true;
        break;
      case 0x104:
        pRunArgs.newCrcFile = optarg;
        break;
      case 'v':
        logLevel = atoi(optarg);
        break;
      case 'l':
        logFile = optarg;
        break;
      case '?':
      case 'h':
        usage(true);
        break;
      default:
        exitWrapper(EXIT_FAILURE);
        break;
    }
  }

  // Adjust log level
  if (logLevel < 0 || logLevel >= l_MAX_LEVEL) {
    LOGMSG(l_FATAL, "Invalid debug level '%d'", logLevel);
  }
  log_setMinLevel(logLevel);

  // Set log file
  if (log_initLogFile(logFile) == false) {
    LOGMSG(l_FATAL, "Failed to initialize log file");
    exitWrapper(EXIT_FAILURE);
  }

  // Initialize input files
  if (!utils_init(&pFiles)) {
    LOGMSG(l_FATAL, "Couldn't load input files");
    exitWrapper(EXIT_FAILURE);
  }

  int mainRet = EXIT_FAILURE;

  // Parse input file with checksums (expects one per line) and update location checksum
  if (pRunArgs.newCrcFile) {
    if (pFiles.fileCnt != 1) {
      LOGMSG(l_ERROR, "Exactly one input Vdex file is expected when updating location checksums");
      goto complete;
    }

    int nSums = -1;
    u4 *checksums = utils_processFileWithCsums(pRunArgs.newCrcFile, &nSums);
    if (checksums == NULL || nSums < 1) {
      LOGMSG(l_ERROR, "Failed to extract new location checksums from '%s'", pRunArgs.newCrcFile);
      goto complete;
    }

    if (!vdex_updateChecksums(pFiles.files[0], nSums, checksums, &pRunArgs)) {
      LOGMSG(l_ERROR, "Failed to update location checksums");
    } else {
      mainRet = EXIT_SUCCESS;
      DISPLAY(l_INFO, "%d location checksums have been updated", nSums);
      DISPLAY(l_INFO, "Update Vdex file is available in '%s'",
              pRunArgs.outputDir ? pRunArgs.outputDir : dirname(pFiles.inputFile));
    }

    free(checksums);
    goto complete;
  }

  size_t processedVdexCnt = 0, processedDexCnt = 0;
  DISPLAY(l_INFO, "Processing %zu file(s) from %s", pFiles.fileCnt, pFiles.inputFile);

  for (size_t f = 0; f < pFiles.fileCnt; f++) {
    off_t fileSz = 0;
    int srcfd = -1;
    u1 *buf = NULL;

    LOGMSG(l_DEBUG, "Processing '%s'", pFiles.files[f]);

    // mmap file
    buf = utils_mapFileToRead(pFiles.files[f], &fileSz, &srcfd);
    if (buf == NULL) {
      LOGMSG(l_ERROR, "Open & map failed - skipping '%s'", pFiles.files[f]);
      continue;
    }

    // Quick size checks for minimum valid file
    if ((size_t)fileSz < (sizeof(vdexHeader) + sizeof(dexHeader))) {
      LOGMSG(l_WARN, "Invalid input file - skipping '%s'", pFiles.files[f]);
      munmap(buf, fileSz);
      close(srcfd);
      continue;
    }

    // Validate Vdex magic header
    if (!vdex_isValidVdex(buf)) {
      LOGMSG(l_WARN, "Invalid Vdex header - skipping '%s'", pFiles.files[f]);
      munmap(buf, fileSz);
      close(srcfd);
      continue;
    }
    vdex_dumpHeaderInfo(buf);

    if (!selectVdexBackend(buf)) {
      LOGMSG(l_WARN, "Failed to initialize Vdex backend - skipping '%s'", pFiles.files[f]);
      munmap(buf, fileSz);
      close(srcfd);
      continue;
    }

    // Dump Vdex verified dependencies info
    if (pRunArgs.dumpDeps) {
      log_setDisStatus(true);
      void *pDepsData = vdex_initDepsInfo(buf);
      if (pDepsData == NULL) {
        LOGMSG(l_WARN, "Empty verified dependency data")
      } else {
        // TODO: Migrate this to vdex_process to avoid iterating Dex files twice. For now it's not
        // a priority since the two flags offer different functionalities thus no point using them
        // at the same time.
        vdex_dumpDepsInfo(buf, pDepsData);
        vdex_destroyDepsInfo(pDepsData);
      }
      log_setDisStatus(false);
    }

    if (pRunArgs.enableDisassembler) {
      log_setDisStatus(true);
    }

    // Unquicken Dex bytecode or simply walk optimized Dex files
    int ret = vdex_process(pFiles.files[f], buf, &pRunArgs);
    if (ret == -1) {
      LOGMSG(l_ERROR, "Failed to process Dex files - skipping '%s'", pFiles.files[f]);
      munmap(buf, fileSz);
      close(srcfd);
      continue;
    }

    processedDexCnt += ret;
    processedVdexCnt++;

    // Clean-up
    munmap(buf, fileSz);
    buf = NULL;
    close(srcfd);
  }

  DISPLAY(l_INFO, "%u out of %u Vdex files have been processed", processedVdexCnt, pFiles.fileCnt);
  DISPLAY(l_INFO, "%u Dex files have been extracted in total", processedDexCnt);
  DISPLAY(l_INFO, "Extracted Dex files are available in '%s'",
          pRunArgs.outputDir ? pRunArgs.outputDir : dirname(pFiles.inputFile));
  mainRet = EXIT_SUCCESS;

complete:
  free(pFiles.files);
  exitWrapper(mainRet);
}
