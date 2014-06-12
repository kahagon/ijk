#include <iostream>

#include "hphp/runtime/ext/std/ext_std_classobj.h"
#include "translator.h"

namespace HPHP {
  
bool HHVM_FUNCTION(ijk_translate_file, const String& moduleName, const String& filePath);
bool HHVM_FUNCTION(ijk_class_exists, const String& className);
String HHVM_FUNCTION(ijk_assemble, const String& sourceFilePath);

}