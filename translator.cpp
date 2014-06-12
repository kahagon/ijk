#include "ijk.h"


namespace HPHP {
namespace IJK {
  
llvm::Function* Translator::generateMainFunction() {
  llvm::Type*               resultType = llvm::Type::getInt64Ty(m_ctx);
  std::vector<llvm::Type*>  paramTypes(0, llvm::Type::getVoidTy(m_ctx));
  std::string               functionName = "main";
  llvm::FunctionType*       functionType = llvm::FunctionType::get(resultType, paramTypes, false);
  llvm::Function*           function = llvm::Function::Create(
                                        functionType, 
                                        llvm::Function::ExternalLinkage, 
                                        functionName, 
                                        m_mod);

  llvm::BasicBlock* basicBlock = llvm::BasicBlock::Create(m_ctx, "entry", function);
  m_builder->SetInsertPoint(basicBlock);
  std::string varName = "i";
  llvm::AllocaInst* var = m_builder->CreateAlloca(llvm::Type::getInt64Ty(m_ctx), 0, varName);

  llvm::ValueSymbolTable& functionSymbolTable = function->getValueSymbolTable();

  llvm::Value* lhs_v = functionSymbolTable.lookup(varName);
  llvm::Value* rhs_v = llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_ctx), 0);

  m_builder->CreateStore(rhs_v, lhs_v);
  m_builder->CreateRet(m_builder->CreateLoad(lhs_v, "var_tmp"));

  return function;
};

llvm::FunctionType* Translator::generateFunctionType(const Func* func) {
  DataType returnType = func->returnType();
  std::string funcName = func->name()->data();
  printf("funcName: %s\n", funcName.c_str());
  printf("  numParams: %d\n", func->params().size());
  llvm::Type* resultType;
  switch (returnType) {
    case DataType::KindOfBoolean:
      printf("  resultType: KindOfBoolean\n");
      resultType = llvm::Type::getInt1Ty(m_ctx);
      break;
    case DataType::KindOfInt64:
      printf("  resultType: KindOfInt64\n");
      resultType = llvm::Type::getInt64Ty(m_ctx);
      break;
    case DataType::KindOfDouble:
      printf("  resultType: KindOfDouble\n");
      resultType = llvm::Type::getDoubleTy(m_ctx);
      break;
    case DataType::KindOfString:
      printf("  resultType: KindOfDouble\n");
      resultType = llvm::PointerType::get(llvm::Type::getInt64Ty(m_ctx), 0);
      break;
    case DataType::KindOfNull:
      printf("  resultType: KindOfNull\n");
      resultType = llvm::Type::getVoidTy(m_ctx);
      break;
    case DataType::KindOfUninit:
      printf("  resultType: KindOfUninit\n");
      resultType = llvm::Type::getVoidTy(m_ctx);
      break;
    case DataType::KindOfInvalid:
      printf("  resultType: KindOfInvalid\n");
      resultType = llvm::Type::getVoidTy(m_ctx);
      break;
    default:
      printf("  resultType: default(%d)\n", returnType);
      resultType = llvm::Type::getVoidTy(m_ctx);
      break;
  }
 
  return nullptr;
  
//  std::vector<llvm::Type*>  paramTypes(0, llvm::Type::getVoidTy(m_ctx));
//  llvm::FunctionType*       funcType = llvm::FunctionType::get(resultType, paramTypes, false);
//  
//  return funcType;
}

void Translator::appendFunc(const Func* func) {
  std::string funcName = func->name()->data();
//  DataType returnDataType = func->returnType();
  llvm::FunctionType* funcType = generateFunctionType(func);
  
//  llvm::Function* function = llvm::Function::Create(
//    funcType, llvm::Function::ExternalLinkage, funcName, m_mod);
  
  
  if (func->isPseudoMain()) {
    
  } else {
//    out.fmtln(".function {}({}){}{{", func->name()->data(),
//      func_param_list(finfo), func_flag_list(finfo));
  }
  
}

llvm::Module* Translator::translateUnit(HPHP::Unit* unit) {
  UnitMergeInfo::FuncRange funcRange = unit->funcs();
  for (Func* func : funcRange) {
    appendFunc(func);
  }
  
//  Array funcs = unit->getUserFunctions();
//  for (ArrayIter funcIter = funcs.begin(); funcIter; ++funcIter) {
//    Variant k = funcIter.first();
//    Variant v = funcIter.second();
//    //printf("data type: %02x\n", v.getType()); // hphp/runtime/base/datatype.h 中の enum DataType
//    
//    Func* func = unit->lookupFunc(v.toString().get());
//    appendFunc(func);
//  }
  
  return m_mod;
};

void Translator::defineStringData() {
  m_stringData = llvm::StructType::create(m_ctx, "string_data");
  std::vector<llvm::Type*> elems;
  elems.push_back(llvm::Type::getInt32Ty(m_ctx));
  elems.push_back(llvm::Type::getInt8Ty(m_ctx)->getPointerTo());
  m_stringData->setBody(elems);
}

void Translator::defineTypedValue() {
  HPHP::FuncEmitter* fe;
  m_typedValue = llvm::StructType::create(m_ctx, "typed_value_t");
  std::vector<llvm::Type*> typedValueElems;
  typedValueElems.push_back(llvm::Type::getInt8Ty(m_ctx)); // HPHP::TypedValue.m_type
  typedValueElems.push_back(llvm::Type::getInt64Ty(m_ctx)); // HPHP::TypedValue.m_data.num
  typedValueElems.push_back(llvm::Type::getDoubleTy(m_ctx)); // HPHP::TypedValue.m_data.dbl
  typedValueElems.push_back(m_stringData->getPointerTo()); // HPHP::TypedValue.m_data.pstr
  m_typedValue->setBody(typedValueElems);
  
  llvm::UndefValue* value_test_struct = llvm::UndefValue::get(m_typedValue);
  //Constant* const_struct = ConstantStruct::get(st, nullptr);
  llvm::GlobalVariable *gb = new llvm::GlobalVariable(*m_mod, m_typedValue, false,
    llvm::GlobalValue::ExternalLinkage, value_test_struct, "test_typed_value");

}

void Translator::defineStack() {
  m_stack = llvm::StructType::create(m_ctx, "stack_t");
  std::vector<llvm::Type*> elems;
  elems.push_back(llvm::Type::getInt64Ty(m_ctx));
  elems.push_back(llvm::Type::getInt64Ty(m_ctx));
  elems.push_back(m_typedValue->getPointerTo()->getPointerTo());
  m_stack->setBody(elems);
  llvm::UndefValue* value_test_struct = llvm::UndefValue::get(m_stack);
  //Constant* const_struct = ConstantStruct::get(st, nullptr);
  llvm::GlobalVariable *gb = new llvm::GlobalVariable(*m_mod, m_stack, false,
    llvm::GlobalValue::ExternalLinkage, value_test_struct, "test_stack");
}

void Translator::defineTypes() {
  defineStringData();
  defineTypedValue();
  defineStack();
}

llvm::Module* Translator::translateFile(const HPHP::String& sourceFilePath) {
#ifndef PHP_PATHINFO_BASENAME
#define DEFINE_PHP_PATHINFO_BASENAME
#define PHP_PATHINFO_BASENAME (2)
#endif
  
  defineTypes();
  
  String basename = f_pathinfo(sourceFilePath, PHP_PATHINFO_BASENAME);
  Variant contentsVariant = f_file_get_contents(sourceFilePath);
  String contentsString = contentsVariant.toString();
  const char* contents = contentsString.c_str();
  size_t contentsSize = contentsString.size();
  MD5 md5(string_md5(contents, contentsSize).c_str());
  Unit* unit = compile_file(contents, contentsSize, md5, basename.c_str());
  
#ifdef DEFINE_PHP_PATHINFO_BASENAME
#undef DEFINE_PHP_PATHINFO_BASENAME
#undef PHP_PATHINFO_BASENAME
#endif
  
  if (unit == nullptr) {
    return false;
  } else {
    return translateUnit(unit);
  }
};

bool Translator::loadSourceFile(const HPHP::String& sourceFilePath) {
  return true;
};

bool Translator::print() {
  llvm::PassManager passManager;
  std::string error;
  llvm::raw_fd_ostream rawStream(m_modId.str().c_str(), error, llvm::sys::fs::F_RW);
  passManager.add(llvm::createPromoteMemoryToRegisterPass());
  passManager.add(llvm::createPrintModulePass(rawStream));
  passManager.run(*m_mod);
  rawStream.close();
  return true;
};

} // namespace IJK
} // namespace HPHP
