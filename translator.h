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

  
struct EHFault { std::string label; };
struct EHCatch { std::map<std::string,std::string> blocks; };
using EHInfo = boost::variant< EHFault
                             , EHCatch
                             >;

struct FuncInfo {
  FuncInfo(const Unit* u, const Func* f) : unit(u), func(f) {}

  const Unit* unit;
  const Func* func;

  // Map from offset to label names we should use for that offset.
  std::map<Offset,std::string> labels;

  // Information for each EHEnt in the func (basically which label
  // names we chose for its handlers).
  std::unordered_map<const EHEnt*,EHInfo> ehInfo;

  // Fault and catch protected region starts in order.
  std::vector<std::pair<Offset,const EHEnt*>> ehStarts;
};

struct PseudoActRec {
  const StringData* m_funcName;
  uint32_t m_numArgs;
  std::vector<llvm::Value*> m_params;
  
  PseudoActRec(const StringData* funcName, uint32_t numArgs) {
    m_funcName = funcName;
    m_numArgs = numArgs;
    m_params.resize(numArgs);
  };
  ~PseudoActRec() {
    
  };
  
  void setParam(llvm::Value* value) {
    m_params.push_back(value);
  }
};
  
class Translator {
  public:
    llvm::LLVMContext& m_ctx;
    llvm::StringRef m_modId;
    llvm::Module* m_mod;
    llvm::IRBuilder<>* m_builder;
    llvm::StructType* m_stringData;
    llvm::StructType* m_typedValue;
    llvm::StructType* m_stack;
    bool m_currentFunctionIsPseudoMain;
    llvm::Function* m_currentFunction;
    std::vector<llvm::Value*> m_currentFunctionArguments;
    llvm::Function* m_CFunctionPuts;
    llvm::GlobalVariable* m_emptyString;
    std::vector<PseudoActRec*> m_parStack;
    std::map<std::string, llvm::Function*>m_functions;
    
    Translator(
      const HPHP::String& modId, 
      llvm::LLVMContext& ctx = llvm::getGlobalContext()): m_ctx(ctx) 
    {
      m_currentFunctionIsPseudoMain = false;
      m_modId = llvm::StringRef(modId.c_str());
      m_mod = new llvm::Module(m_modId, m_ctx);
      m_builder = new llvm::IRBuilder<>(m_ctx);
    };
    virtual ~Translator() {
      delete m_mod;
      delete m_builder;
    };

    void pushPAR(PseudoActRec* par) {
      m_parStack.push_back(par);
    };
    
    PseudoActRec* popPAR() {
      PseudoActRec* elem = m_parStack.back();
      m_parStack.pop_back();
      return elem;
    };
    
    PseudoActRec* currentPAR() {
      return m_parStack.back();
    };
    
    bool inFuncCall() {
      return 0 < m_parStack.size();
    };
    
    bool print();
    bool loadSourceFile(const HPHP::String& sourceFilePath);
    llvm::Function* generateMainFunction(const FuncInfo& finfo, PC pc);
    llvm::Module* translateUnit(HPHP::Unit* unit);
    llvm::Module* translateFile(const HPHP::String& sourceFilePath);
    llvm::FunctionType* generateFunctionType(const Func* func);
    PseudoActRec* getCurrentActRec() {
      
    };
  
  private:
    void initGlobals();
    void declarePuts();
    void declareFuncs();
    
    void defineTypes();
    void defineStringData();
    void defineTypedValue();
    void defineStack();
    
    std::string loc_name(const FuncInfo& finfo, uint32_t id);
    
    llvm::Function* generateFunction(const FuncInfo& finfo);
    void appendFunc(const Func* func);
    void appendFuncBody(llvm::Value* stack_p, const FuncInfo& finfo, bool isPseudoMain=false);
    void appendInstruction(llvm::Value* stack_p, const FuncInfo& finfo, PC pc);
    
    llvm::Value* createTypedValueNull();
    llvm::Value* createTypedValueInt(int64_t num);
    llvm::Value* createTypedValueString(std::string str);
    
    void insertInstructionRetPseudoMain(llvm::Value* stack_p);
    llvm::Value* insertInstructionNull(llvm::Value* stack_p);
    llvm::Value* insertInstructionInt(llvm::Value* stack_p, int64_t num);
    llvm::Value* insertInstructionString(llvm::Value* stack_p, const StringData* stringData);
    llvm::Value* insertInstructionSetL(llvm::Value* stack_p, std::string varName);
    llvm::Value* insertInstructionPrint(llvm::Value* stack_p);
    llvm::Value* insertInstructionPopC(llvm::Value* stack_p);
    llvm::Value* insertInstructionPopR(llvm::Value* stack_p);
    void insertInstructionRetC(llvm::Value* stack_p);
    void insertInstructionFPushFuncD(llvm::Value* stack_p, uint32_t numArgs, const StringData* funcName);
    void insertInstructionFPassCE(llvm::Value* stack_p, uint32_t paramId);
    llvm::Value* insertInstructionFCall(llvm::Value* stack_p, uint32_t numArgs);
    
    llvm::Value* insertInstructionMalloc(llvm::Type* type);
    llvm::Value* insertInstructionStackAllocInit(uint32_t stackSize);
    llvm::Value* insertInstructionStackPop(llvm::Value* stack_p);
    void insertInstructionStackPush(llvm::Value* stack_p, llvm::Value* typed_value_p);
    llvm::Value* insertInstructionGetTopOfStack(llvm::Value* stack_p);
};

} // namespace IJK
} // namespace HPHP







