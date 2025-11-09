// Filesystem wrapper for LittleFS
#pragma once
#include <FS.h>
#include <LittleFS.h>

#ifndef FS_PART_LABEL
#define FS_PART_LABEL "spiffs"
#endif

#define FSYS LittleFS
#define FSYS_NAME "LittleFS"

inline bool FS_BEGIN(bool formatOnFail = true) {
  return LittleFS.begin(formatOnFail, "/littlefs", 10, FS_PART_LABEL);
}