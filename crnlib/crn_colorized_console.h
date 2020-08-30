// File: crn_colorized_console.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once

#include "crn_console.h"
#include "crn_export.h"

namespace crnlib {
class CRN_EXPORT colorized_console {
 public:
  static void init();
  static void deinit();
  static void tick();

 private:
  static bool console_output_func(eConsoleMessageType type, const char* pMsg, void* pData);
};

}  // namespace crnlib
