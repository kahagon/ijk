#include "ijk.h"


namespace HPHP {

bool HHVM_FUNCTION(ijk_translate_file, const String& moduleName, const String& filePath) {
  IJK::Translator translator(moduleName);
  //translator.generateMainFunction();
  //translator.loadSourceFile(filePath);
  translator.translateFile(filePath);
  return translator.print();
}

bool HHVM_FUNCTION(ijk_class_exists, const String& className) {
  return HHVM_FN(class_exists)(className);
}

String HHVM_FUNCTION(ijk_assemble, const String& sourceFilePath) {
  Variant contentsVariant = f_file_get_contents(sourceFilePath);
  String contentsString = contentsVariant.toString();
  const char* contents = contentsString.c_str();
  size_t contentsSize = contentsString.size();
  
  Unit* unit = compile_string(contents, contentsSize);
  return String::FromCStr((disassemble(unit)).c_str());
}

static class IJKExtension : public Extension {
  public:
  IJKExtension() : Extension("ijk") {}
  virtual void moduleInit() {
    HHVM_FE(ijk_translate_file);
    HHVM_FE(ijk_class_exists);
    HHVM_FE(ijk_assemble);
    loadSystemlib();
  }
} s_ijk_extension;

HHVM_GET_MODULE(ijk);

}
