#include "llvm/IR/Module.h"
#include "llvm/PassManager.h"
#include "llvm/LinkAllPasses.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Support/raw_ostream.h"

#include "hphp/runtime/base/base-includes.h"
#include "hphp/runtime/vm/runtime.h"
#include "hphp/runtime/vm/func.h"
#include "hphp/runtime/vm/disas.h"
#include "hphp/runtime/ext/ext_file.h"
#include "hphp/zend/zend-string.h"

//using namespace llvm;

namespace HPHP {
namespace IJK {

class Translator {
  public:
    llvm::LLVMContext& m_ctx;
    llvm::StringRef m_modId;
    llvm::Module* m_mod;
    llvm::IRBuilder<>* m_builder;
    llvm::StructType* m_stringData;
    llvm::StructType* m_typedValue;
    llvm::StructType* m_stack;

    Translator(
      const HPHP::String& modId, 
      llvm::LLVMContext& ctx = llvm::getGlobalContext()): m_ctx(ctx) 
    {
      m_modId = llvm::StringRef(modId.c_str());
      m_mod = new llvm::Module(m_modId, m_ctx);
      m_builder = new llvm::IRBuilder<>(m_ctx);
    };
    virtual ~Translator() {
      delete m_mod;
      delete m_builder;
    };

    bool print();
    bool loadSourceFile(const HPHP::String& sourceFilePath);
    llvm::Function* generateMainFunction();
    llvm::Module* translateUnit(HPHP::Unit* unit);
    llvm::Module* translateFile(const HPHP::String& sourceFilePath);
    llvm::FunctionType* generateFunctionType(const Func* func);
  
  private:
    void appendFunc(const Func* func);
    void defineTypes();
    void defineStringData();
    void defineTypedValue();
    void defineStack();
};

} // namespace IJK
} // namespace HPHP







