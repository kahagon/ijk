#include "ijk.h"
#include "hphp/util/match.h"

namespace HPHP {
namespace IJK {

// Priority queue where the smaller elements come first.
template<class T> using min_priority_queue =
  std::priority_queue<T,std::vector<T>,std::greater<T>>;

FuncInfo find_func_info(const Func* func) {
  auto finfo = FuncInfo(func->unit(), func);

  auto label_num = uint32_t{0};
  auto gen_label = [&] (const char* kind) {
    return folly::format("{}{}", kind, label_num++).str();
  };

  auto add_target = [&] (const char* kind, Offset off) -> std::string {
    auto it = finfo.labels.find(off);
    if (it != end(finfo.labels)) return it->second;
    auto const label = gen_label(kind);
    finfo.labels[off] = label;
    return label;
  };

  auto find_jump_targets = [&] {
    auto it           = func->unit()->at(func->base());
    auto const stop   = func->unit()->at(func->past());
    auto const bcBase = reinterpret_cast<const Op*>(func->unit()->at(0));

    for (; it != stop; it += instrLen(reinterpret_cast<const Op*>(it))) {
      auto const pop = reinterpret_cast<const Op*>(it);
      auto const off = func->unit()->offsetOf(it);
      if (isSwitch(*pop)) {
        foreachSwitchTarget(pop, [&] (Offset off) {
          add_target("L", pop - bcBase + off);
        });
        continue;
      }
      auto const target = instrJumpTarget(bcBase, off);
      if (target != InvalidAbsoluteOffset) {
        add_target("L", target);
        continue;
      }
    }
  };

  auto find_eh_entries = [&] {
    for (auto& eh : func->ehtab()) {
      finfo.ehInfo[&eh] = [&]() -> EHInfo {
        switch (eh.m_type) {
        case EHEnt::Type::Catch:
          {
            auto catches = EHCatch {};
            for (auto& kv : eh.m_catches) {
              auto const clsName = func->unit()->lookupLitstrId(kv.first);
              catches.blocks[clsName->data()] = add_target("C", kv.second);
            }
            return catches;
          }
        case EHEnt::Type::Fault:
          return EHFault { add_target("F", eh.m_fault) };
        }
        not_reached();
      }();
      finfo.ehStarts.emplace_back(eh.m_base, &eh);
    }
  };

  auto find_dv_entries = [&] {
    for (auto i = uint32_t{0}; i < func->numParams(); ++i) {
      auto& param = func->params()[i];
      if (param.hasDefaultValue()) {
        add_target("DV", func->params()[i].funcletOff());
      }
    }
  };

  find_jump_targets();
  find_eh_entries();
  find_dv_entries();
  return finfo;
}

//////////////////////////////////////////////////////////////////////

template<class T> T decode(PC& pc) {
  auto const ret = *reinterpret_cast<const T*>(pc);
  pc += sizeof ret;
  return ret;
}

llvm::Function* Translator::generateFunction(const FuncInfo& finfo) {
  m_currentFunctionArguments.clear();
  
  auto const func = finfo.func;
  llvm::Type*               resultType = llvm::Type::getVoidTy(m_ctx);
  std::vector<llvm::Type*>  paramTypes;
  paramTypes.push_back(m_typedValue->getPointerTo()); // for return value
  for (auto i = uint32_t{0}; i < func->numParams(); ++i) {
    paramTypes.push_back(m_typedValue->getPointerTo());
  }
          
  std::string functionName = func->name()->data();
  llvm::FunctionType* functionType = llvm::FunctionType::get(
          resultType, paramTypes, false);
//  llvm::Function* function = llvm::Function::Create(
//          functionType, llvm::Function::ExternalLinkage, functionName, m_mod);
  llvm::Function* function = llvm::cast<llvm::Function>(m_mod->getOrInsertFunction(functionName, functionType));
  
  int paramSize = paramTypes.size();
  llvm::Function::arg_iterator ai = function->arg_begin();
  for(
          int i = 0;
          i < paramSize && ai != function->arg_end();
          i++, ai++)
  {
    m_currentFunctionArguments.push_back(ai);
    if (i == 0) {
      ai->setName("retval");
    } else {
      ai->setName(loc_name(finfo, i-1));
    }
  }
  
  m_functions[functionName] = function;
  
  return function;
}

std::string Translator::loc_name(const FuncInfo& finfo, uint32_t id) {
  auto const sd = finfo.func->localVarName(id);
  if (!sd || sd->empty()) {
    always_assert(!"unnamed locals need to be supported");
  }
  return sd->data();
}

std::string jmp_label(const FuncInfo& finfo, Offset tgt) {
  auto const it  = finfo.labels.find(tgt);
  always_assert(it != end(finfo.labels));
  return it->second;
};

void Translator::appendInstruction(
  llvm::Value* stack_p, 
  const FuncInfo& finfo, 
  PC pc) 
{
  auto const startPc = pc;
  
//
//  auto rel_label = [&] (Offset off) {
//    auto const tgt = startPc - finfo.unit->at(0) + off;
//    return jmp_label(finfo, tgt);
//  };
//
//  auto print_minstr = [&] {
//    auto const immVec = ImmVector::createFromStream(pc);
//    pc += immVec.size() + sizeof(int32_t) + sizeof(int32_t);
//    auto vec = immVec.vec();
//    auto const lcode = static_cast<LocationCode>(*vec++);
//
//    out.fmt(" <{}", locationCodeString(lcode));
//    if (numLocationCodeImms(lcode)) {
//      always_assert(numLocationCodeImms(lcode) == 1);
//      out.fmt(":${}", loc_name(finfo, decodeVariableSizeImm(&vec)));
//    }
//
//    while (vec < pc) {
//      auto const mcode = static_cast<MemberCode>(*vec++);
//      out.fmt(" {}", memberCodeString(mcode));
//      auto const imm = [&] { return decodeMemberCodeImm(&vec, mcode); };
//      switch (memberCodeImmType(mcode)) {
//      case MCodeImm::None:
//        break;
//      case MCodeImm::Local:
//        out.fmt(":${}", loc_name(finfo, imm()));
//        break;
//      case MCodeImm::String:
//        out.fmt(":{}", escaped(finfo.unit->lookupLitstrId(imm())));
//        break;
//      case MCodeImm::Int:
//        out.fmt(":{}", imm());
//        break;
//      }
//    }
//    assert(vec == pc);
//
//    out.fmt(">");
//  };
//
//  auto print_switch = [&] {
//    auto const vecLen = decode<int32_t>(pc);
//    out.fmt(" <");
//    for (auto i = int32_t{0}; i < vecLen; ++i) {
//      auto const off = decode<Offset>(pc);
//      FTRACE(1, "sw label: {}\n", off);
//      out.fmt("{}{}", i != 0 ? " " : "", rel_label(off));
//    }
//    out.fmt(">");
//  };
//
//  auto print_sswitch = [&] {
//    auto const vecLen = decode<int32_t>(pc);
//    out.fmt(" <");
//    for (auto i = int32_t{0}; i < vecLen; ++i) {
//      auto const strId  = decode<Id>(pc);
//      auto const offset = decode<Offset>(pc);
//      out.fmt("{}{}:{}",
//        i != 0 ? " " : "",
//        strId == -1 ? "-" : escaped(finfo.unit->lookupLitstrId(strId)),
//        rel_label(offset)
//      );
//    }
//    out.fmt(">");
//  };
//
//  auto print_itertab = [&] {
//    auto const vecLen = decode<int32_t>(pc);
//    out.fmt(" <");
//    for (auto i = int32_t{0}; i < vecLen; ++i) {
//      auto const kind = static_cast<IterKind>(decode<int32_t>(pc));
//      auto const id   = decode<int32_t>(pc);
//      auto const kindStr = [&]() -> const char* {
//        switch (kind) {
//        case KindOfIter:   return "(Iter)";
//        case KindOfMIter:  return "(MIter)";
//        case KindOfCIter:  return "(CIter)";
//        }
//        not_reached();
//      }();
//      out.fmt("{}{} {}", i != 0 ? ", " : "", kindStr, id);
//    }
//    out.fmt(">");
//  };
//
//  auto print_stringvec = [&] {
//    auto const vecLen = decode<int32_t>(pc);
//    out.fmt(" <");
//    for (auto i = uint32_t{0}; i < vecLen; ++i) {
//      auto const str = finfo.unit->lookupLitstrId(decode<int32_t>(pc));
//      out.fmt("{}{}", i != 0 ? " " : "", escaped(str));
//    }
//    out.fmt(">");
//  };

  switch (*reinterpret_cast<const Op*>(pc)) {
    case Op::Int:
      ++pc;
      printf("Op::Int\n");
      insertInstructionInt(stack_p, decode<int64_t>(pc));
      break;
    case Op::String:
      ++pc;
      printf("Op::String\n");
      insertInstructionString(stack_p, finfo.unit->lookupLitstrId(decode<Id>(pc)));
      break;
    case Op::SetL:
      ++pc;
      printf("Op::SetL\n");
      insertInstructionSetL(stack_p, loc_name(finfo, decodeVariableSizeImm(&pc)));
      break;
    case Op::PopC:
      ++pc;
      printf("Op::PopC\n");
      insertInstructionPopC(stack_p);
      break;
    case Op::PopR:
      ++pc;
      printf("Op::PopR\n");
      insertInstructionPopR(stack_p);
      break;
    case Op::Print:
      ++pc;
      printf("Op::Print\n");
      insertInstructionPrint(stack_p);
      break;
    case Op::RetC:
      ++pc;
      printf("Op::RetC\n");
      insertInstructionRetC(stack_p);
      break;
    case Op::Null:
      ++pc;
      printf("Op::Null\n");
      insertInstructionNull(stack_p);
      break;
    case Op::FPushFuncD:
      ++pc;
      printf("Op::FPushFuncD\n");
      {
        uint32_t numArgs = decodeVariableSizeImm(&pc);
        StringData* funcName = finfo.unit->lookupLitstrId(decode<Id>(pc));
        insertInstructionFPushFuncD(stack_p,
                numArgs,
                funcName);
      }
      break;
    case Op::FPassCE:
      printf("Op::FPassCE\n");
      ++pc;
      insertInstructionFPassCE(stack_p, decodeVariableSizeImm(&pc));
      break;
    case Op::FCall:
      printf("Op::FCall\n");
      ++pc;
      insertInstructionFCall(stack_p, decodeVariableSizeImm(&pc));
      break;
    default:
      printf("default\n");
      ++pc;
      //printf("op: %d\n", op);
      break;
  }

}


void Translator::appendFuncBody(
  llvm::Value* stack_p, 
  const FuncInfo& finfo,
  bool isPseudoMain) 
{
  auto const func = finfo.func;
  // begin() end() defined in hphp/runtime/base/type-array.h
  auto       lblIter = begin(finfo.labels);
  auto const lblStop = end(finfo.labels);
  auto       ehIter  = begin(finfo.ehStarts);
  auto const ehStop  = end(finfo.ehStarts);
  auto       bcIter  = func->unit()->at(func->base());
  auto const bcStop  = func->unit()->at(func->past());

  min_priority_queue<Offset> ehEnds;

  while (bcIter != bcStop) {
    auto const off = func->unit()->offsetOf(bcIter);

    // First, close any protected EH regions that are past-the-end at
    // this offset.
    while (!ehEnds.empty() && ehEnds.top() == off) {
      ehEnds.pop();
//      out.dec_indent();
//      out.fmtln("}}");
    }

    // Next, open any new protected regions that start at this offset.
    for (; ehIter != ehStop && ehIter->first == off; ++ehIter) {
      auto const info = finfo.ehInfo.find(ehIter->second);
      always_assert(info != end(finfo.ehInfo));
      match<void>(
        info->second,
        [&] (const EHCatch& catches) {
//          out.indent();
//          out.fmt(".try_catch");
//          for (auto& kv : catches.blocks) {
//            out.fmt(" ({} {})", kv.first, kv.second);
//          }
//          out.fmt(" {{");
//          out.nl();
        },
        [&] (const EHFault& fault) {
//          out.fmtln(".try_fault {} {{", fault.label);
        }
      );
//      out.inc_indent();
      ehEnds.push(ehIter->second->m_past);
    }

    // Then, print labels if we have any.  This order keeps the labels
    // from dangling on weird sides of .try_fault or .try_catch
    // braces.
    while (lblIter != lblStop && lblIter->first < off) ++lblIter;
    if (lblIter != lblStop && lblIter->first == off) {
//      out.dec_indent();
//      out.fmtln("{}:", lblIter->second);
//      out.inc_indent();
    }

    appendInstruction(stack_p, finfo, bcIter);

    bcIter += instrLen(reinterpret_cast<const Op*>(bcIter));
  }
}

void Translator::appendFunc(const Func* func) {
  auto const finfo = find_func_info(func);
  
  if (func->isPseudoMain()) {
    m_currentFunctionIsPseudoMain = true;
    llvm::Type*               resultType = llvm::Type::getInt64Ty(m_ctx);
    std::vector<llvm::Type*>  paramTypes(0, llvm::Type::getVoidTy(m_ctx));
    std::string               functionName = "main";
    llvm::FunctionType*       functionType = llvm::FunctionType::get(resultType, paramTypes, false);
    m_currentFunction = llvm::Function::Create(
                                          functionType, 
                                          llvm::Function::ExternalLinkage, 
                                          functionName, 
                                          m_mod);

    llvm::BasicBlock* basicBlock = llvm::BasicBlock::Create(m_ctx, "entry", m_currentFunction);
    m_builder->SetInsertPoint(basicBlock);
    llvm::Value* stack_p = insertInstructionStackAllocInit(1024);
    
    appendFuncBody(stack_p, finfo, true);

    llvm::Value* retval = llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_ctx), 0);
    m_builder->CreateRet(retval);
    m_currentFunctionIsPseudoMain = false;
  } else {
    m_currentFunctionIsPseudoMain = false;
    m_currentFunction = generateFunction(finfo);

    llvm::BasicBlock* basicBlock = llvm::BasicBlock::Create(m_ctx, "entry", m_currentFunction);
    m_builder->SetInsertPoint(basicBlock);
    llvm::Value* stack_p = insertInstructionStackAllocInit(1024);
    
    appendFuncBody(stack_p, finfo, true);
    if (!basicBlock->getTerminator()) {
      insertInstructionRetC(stack_p);
    }
  }
}

llvm::Module* Translator::translateUnit(HPHP::Unit* unit) {
  UnitMergeInfo::FuncRange funcRange = unit->funcs();
  Func* pseudoMain = nullptr;
  for (Func* func : funcRange) {
    if (func->isPseudoMain()) {
      pseudoMain = func;
      continue;
    }
    appendFunc(func);
  }
  
  if (pseudoMain) {
    appendFunc(pseudoMain);
  }
  
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
  m_typedValue = llvm::StructType::create(m_ctx, "typed_value_t");
  std::vector<llvm::Type*> typedValueElems;
  typedValueElems.push_back(llvm::Type::getInt8Ty(m_ctx)); // HPHP::TypedValue.m_type
  typedValueElems.push_back(llvm::Type::getInt64Ty(m_ctx)); // HPHP::TypedValue.m_data.num
  typedValueElems.push_back(llvm::Type::getDoubleTy(m_ctx)); // HPHP::TypedValue.m_data.dbl
  typedValueElems.push_back(m_stringData->getPointerTo()); // HPHP::TypedValue.m_data.pstr
  m_typedValue->setBody(typedValueElems);
}

void Translator::defineStack() {
  m_stack = llvm::StructType::create(m_ctx, "stack_t");
  std::vector<llvm::Type*> elems;
  elems.push_back(llvm::Type::getInt64Ty(m_ctx));
  elems.push_back(llvm::Type::getInt64Ty(m_ctx));
  elems.push_back(m_typedValue->getPointerTo()->getPointerTo());
  m_stack->setBody(elems);
}

void Translator::defineTypes() {
  defineStringData();
  defineTypedValue();
  defineStack();
}

llvm::Value* Translator::createTypedValueNull() {
  llvm::Value* typed_value_p = m_builder->CreateAlloca(m_typedValue);
  llvm::Value* type_p = m_builder->CreateStructGEP(typed_value_p, 0);
  llvm::Value* type = llvm::ConstantInt::get(llvm::Type::getInt8Ty(m_ctx), KindOfNull);
  m_builder->CreateStore(type, type_p);
  
  llvm::Value* numValue = llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_ctx), 0);
  llvm::Value* num_p = m_builder->CreateStructGEP(typed_value_p, 1);
  m_builder->CreateStore(numValue, num_p);
  
  llvm::Value* string_data_p = m_builder->CreateAlloca(m_stringData);
  llvm::Value* size_p = m_builder->CreateStructGEP(string_data_p, 0);
  llvm::Value* size = llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_ctx), 1);
  m_builder->CreateStore(size, size_p);
  llvm::Value* str_pp = m_builder->CreateStructGEP(string_data_p, 1);
  llvm::Value* str_p  = m_builder->CreateConstGEP2_64(m_emptyString, 0, 0);
  m_builder->CreateStore(str_p, str_pp);
  llvm::Value* string_data_pp = m_builder->CreateStructGEP(typed_value_p, 3);
  m_builder->CreateStore(string_data_p, string_data_pp);
  
  return typed_value_p;
}

llvm::Value* Translator::createTypedValueInt(int64_t num) {
  
  llvm::Value* typed_value_p = createTypedValueNull();
  llvm::Value* numValue = llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_ctx), num);
  llvm::Value* type_p = m_builder->CreateStructGEP(typed_value_p, 0);
  llvm::Value* type = llvm::ConstantInt::get(llvm::Type::getInt8Ty(m_ctx), KindOfInt64);
  m_builder->CreateStore(type, type_p);
  
  llvm::Value* num_p = m_builder->CreateStructGEP(typed_value_p, 1);
  m_builder->CreateStore(numValue, num_p);
  
  return typed_value_p;
}

llvm::Value* Translator::createTypedValueString(std::string str) {
  llvm::StringRef strRef(str);
  llvm::Constant* constStrVal = llvm::ConstantDataArray::getString(m_ctx, strRef);
  llvm::GlobalVariable* global_str = new llvm::GlobalVariable(
          *m_mod, constStrVal->getType(), true,
          llvm::GlobalValue::InternalLinkage, constStrVal);

  llvm::Value* typed_value_p = createTypedValueNull();
  llvm::Value* type_p = m_builder->CreateStructGEP(typed_value_p, 0);
  llvm::Value* type = llvm::ConstantInt::get(llvm::Type::getInt8Ty(m_ctx), KindOfString);
  m_builder->CreateStore(type, type_p);
  
  llvm::Value* string_data_pp = m_builder->CreateStructGEP(typed_value_p, 3);
  llvm::Value* string_data_p = m_builder->CreateLoad(string_data_pp);
  llvm::Value* size_p = m_builder->CreateStructGEP(string_data_p, 0);
  llvm::Value* size = llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_ctx), str.size()+1);
  m_builder->CreateStore(size, size_p);
  
  llvm::Value* str_pp = m_builder->CreateStructGEP(string_data_p, 1);
  llvm::Value* str_p  = m_builder->CreateConstGEP2_64(global_str, 0, 0);
  m_builder->CreateStore(str_p, str_pp);
  
  return typed_value_p;
}

void Translator::insertInstructionFPushFuncD(
  llvm::Value* stack_p, 
  uint32_t numArgs, 
  const StringData* funcName) 
{
  PseudoActRec* par = new PseudoActRec(funcName, numArgs);
  pushPAR(par);
}

void Translator::insertInstructionFPassCE(llvm::Value* stack_p, uint32_t paramId) {
//  llvm::Value* top_p = insertInstructionGetTopOfStack(stack_p);
//  PseudoActRec* par = currentPAR();
//  par->setParam(top_p);
}

llvm::Value* Translator::insertInstructionFCall(llvm::Value* stack_p, uint32_t numArgs) {
  PseudoActRec* par = popPAR();
  llvm::Function* function = m_mod->getFunction(par->m_funcName->toCppString());
  llvm::Value* retval = createTypedValueNull();
  std::vector<llvm::Value*> params;
  params.push_back(retval);
  for (int i = numArgs-1; i >= 0; --i) {
//    params.push_back(insertInstructionStackPop(stack_p));
    llvm::Value* typed_value_p = insertInstructionStackPop(stack_p);
    params.push_back(typed_value_p);
//    params[i] = typed_value_p;
  }
  m_builder->CreateCall(function, params);
  insertInstructionStackPush(stack_p, retval);
  return retval;
}

llvm::Value* Translator::insertInstructionNull(llvm::Value* stack_p) {
  llvm::Value* retval = createTypedValueNull();
  insertInstructionStackPush(stack_p, retval);
  return retval;
}

llvm::Value* Translator::insertInstructionInt(llvm::Value* stack_p, int64_t num) {
  llvm::Value* retval = createTypedValueInt(num);
  insertInstructionStackPush(stack_p, retval);
  return retval;
}

llvm::Value* Translator::insertInstructionString(llvm::Value* stack_p, const StringData* stringData) {
  std::string str = stringData->toCppString();
  llvm::Value* type_value_p = createTypedValueString(str);
  insertInstructionStackPush(stack_p, type_value_p);
  return type_value_p;
}

llvm::Value* Translator::insertInstructionSetL(llvm::Value* stack_p, std::string varName) {
  
}

llvm::Value* Translator::insertInstructionPrint(llvm::Value* stack_p) {
  llvm::Value* str_typed_value_p = insertInstructionStackPop(stack_p);
  llvm::Value* str_data_pp = m_builder->CreateStructGEP(str_typed_value_p, 3);
  llvm::Value* str_data_p = m_builder->CreateLoad(str_data_pp);
  llvm::Value* str_pp = m_builder->CreateStructGEP(str_data_p, 1);
  llvm::Value* str_p = m_builder->CreateLoad(str_pp);
  m_builder->CreateCall(m_CFunctionPuts, str_p);
  
  return insertInstructionInt(stack_p, 1);
}

llvm::Value* Translator::insertInstructionPopC(llvm::Value* stack_p) {
  return insertInstructionStackPop(stack_p);
}

llvm::Value* Translator::insertInstructionPopR(llvm::Value* stack_p) {
  return insertInstructionStackPop(stack_p);
}

void Translator::insertInstructionRetC(llvm::Value* stack_p) {
  if (!m_currentFunctionIsPseudoMain) {
    llvm::ValueSymbolTable& vst = m_currentFunction->getValueSymbolTable();
    llvm::Value* retval_p = vst.lookup("retval");
//    llvm::Value* retval_p = m_currentFunctionArguments[0];
    llvm::Value* top_p = insertInstructionPopC(stack_p);
    
    llvm::Value* top_type_p = m_builder->CreateStructGEP(top_p, 0);
    llvm::Value* top_type = m_builder->CreateLoad(top_type_p);
    llvm::Value* retval_type_p = m_builder->CreateStructGEP(retval_p, 0);
    m_builder->CreateStore(top_type, retval_type_p);
    
    llvm::Value* top_num_p = m_builder->CreateStructGEP(top_p, 1);
    llvm::Value* top_num = m_builder->CreateLoad(top_num_p);
    llvm::Value* retval_num_p = m_builder->CreateStructGEP(retval_p, 1);
    m_builder->CreateStore(top_num, retval_num_p);
    
    llvm::Value* top_dbl_p = m_builder->CreateStructGEP(top_p, 2);
    llvm::Value* top_dbl = m_builder->CreateLoad(top_dbl_p);
    llvm::Value* retval_dbl_p = m_builder->CreateStructGEP(retval_p, 2);
    m_builder->CreateStore(top_dbl, retval_dbl_p);
    
    llvm::Value* top_str_data_pp = m_builder->CreateStructGEP(top_p, 3);
    llvm::Value* top_str_data_p = m_builder->CreateLoad(top_str_data_pp);
    llvm::Value* top_str_data_size_p = m_builder->CreateStructGEP(top_str_data_p, 0);
    llvm::Value* top_str_data_size = m_builder->CreateLoad(top_str_data_size_p);
    llvm::Value* top_str_data_str_pp = m_builder->CreateStructGEP(top_str_data_p, 1);
    llvm::Value* top_str_data_str_p = m_builder->CreateLoad(top_str_data_str_pp);
    
    llvm::Value* retval_str_data_pp = m_builder->CreateStructGEP(retval_p, 3);
    llvm::Value* retval_str_data_p = m_builder->CreateLoad(retval_str_data_pp);
    llvm::Value* retval_str_data_size_p = m_builder->CreateStructGEP(retval_str_data_p, 0);
    llvm::Value* retval_str_data_str_pp = m_builder->CreateStructGEP(retval_str_data_p, 1);
    
    m_builder->CreateStore(top_str_data_size, retval_str_data_size_p);
    m_builder->CreateStore(top_str_data_str_p, retval_str_data_str_pp);
    
    m_builder->CreateRetVoid();
  }
}

void Translator::insertInstructionRetPseudoMain(llvm::Value* stack_p) {
  if (m_currentFunctionIsPseudoMain) {
//    llvm::Value* retval_p = insertInstructionPopC(stack_p);
    llvm::Value* retval = llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_ctx), 0);
    m_builder->CreateRet(retval);
  }
}


llvm::Value* Translator::insertInstructionStackAllocInit(uint32_t stackSize) {
  llvm::Value* stack_p = m_builder->CreateAlloca(m_stack, nullptr, "stack_p");
  
  llvm::Value* arraySize = llvm::ConstantInt::get(
          llvm::Type::getInt32Ty(m_ctx), stackSize);
  llvm::Value* typed_value_pp = m_builder->CreateAlloca(
          m_typedValue->getPointerTo(), 
          arraySize, 
          "typed_value_pp");
  llvm::Value* typed_value_ppp = m_builder->CreateStructGEP(stack_p, 2, "typed_value_ppp");
  m_builder->CreateStore(typed_value_pp, typed_value_ppp);
  
  llvm::Value* size_p = m_builder->CreateStructGEP(stack_p, 0, "size_p");
  llvm::Constant* size = llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_ctx), 0);
  m_builder->CreateStore(size, size_p);
  
  llvm::Value* max_size_p = m_builder->CreateStructGEP(stack_p, 1, "max_size_p");
  llvm::Constant* max_size = llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_ctx), stackSize);
  m_builder->CreateStore(max_size, max_size_p);
  
  return stack_p;
}

llvm::Value* Translator::insertInstructionGetTopOfStack(llvm::Value* stack_p) {
  llvm::Value* size_p = m_builder->CreateStructGEP(stack_p, 0);
  llvm::Value* size_orig = m_builder->CreateLoad(size_p);
  
  llvm::Value* typed_value_ppp = m_builder->CreateStructGEP(stack_p, 2);
  llvm::Value* typed_value_pp = m_builder->CreateLoad(typed_value_ppp);
  llvm::Value* frame_pp = m_builder->CreateGEP(typed_value_pp, size_orig);
  return m_builder->CreateLoad(frame_pp);
}

llvm::Value* Translator::insertInstructionStackPop  (llvm::Value* stack_p) {
  llvm::Value* size_p = m_builder->CreateStructGEP(stack_p, 0);
  llvm::Value* size_orig = m_builder->CreateLoad(size_p);
  llvm::Constant* one = llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_ctx), 1);
  llvm::Value* new_size = m_builder->CreateSub(size_orig, one);
  m_builder->CreateStore(new_size, size_p);
  llvm::Value* typed_value_ppp = m_builder->CreateStructGEP(stack_p, 2);
  llvm::Value* typed_value_pp = m_builder->CreateLoad(typed_value_ppp);
  llvm::Value* frame_pp = m_builder->CreateGEP(typed_value_pp, size_orig);
  
  return m_builder->CreateLoad(frame_pp);
}

void Translator::insertInstructionStackPush(llvm::Value* stack_p, llvm::Value* typed_value_p) {
  llvm::Value* size_p = m_builder->CreateStructGEP(stack_p, 0);
  llvm::Value* size_orig = m_builder->CreateLoad(size_p);
  llvm::Constant* one = llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_ctx), 1);
  llvm::Value* new_size = m_builder->CreateAdd(size_orig, one);
  m_builder->CreateStore(new_size, size_p);
  
  llvm::Value* typed_value_ppp = m_builder->CreateStructGEP(stack_p, 2);
  llvm::Value* typed_value_pp = m_builder->CreateLoad(typed_value_ppp);
  llvm::Value* frame_pp = m_builder->CreateGEP(typed_value_pp, new_size);
  
  m_builder->CreateStore(typed_value_p, frame_pp);
}

void Translator::declarePuts() {
  llvm::Type*               resultType = llvm::Type::getInt32Ty(m_ctx);
  std::vector<llvm::Type*>  paramTypes;
  paramTypes.push_back(llvm::Type::getInt8Ty(m_ctx)->getPointerTo());
  
  std::string               functionName = "puts";
  llvm::FunctionType*       functionType = llvm::FunctionType::get(resultType, paramTypes, false);
  m_CFunctionPuts   = llvm::Function::Create(
                                            functionType, 
                                            llvm::Function::ExternalLinkage, 
                                            functionName, 
                                            m_mod);
  m_CFunctionPuts->setCallingConv(llvm::CallingConv::C);
}

void Translator::declareFuncs() {
  declarePuts();
}

void Translator::initGlobals() {
    std::string str = "";
    llvm::StringRef strRef(str);
    llvm::Constant* constStrVal = llvm::ConstantDataArray::getString(m_ctx, strRef);
    m_emptyString = new llvm::GlobalVariable(
            *m_mod, constStrVal->getType(), true,
            llvm::GlobalValue::InternalLinkage, constStrVal);
}

llvm::Module* Translator::translateFile(const HPHP::String& sourceFilePath) {
#ifndef PHP_PATHINFO_BASENAME
#define DEFINE_PHP_PATHINFO_BASENAME
#define PHP_PATHINFO_BASENAME (2)
#endif
  
  declareFuncs();
  defineTypes();
  initGlobals();
  
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
