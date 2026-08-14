// Our LLVM pass!

#include "AllocFunctions.h"
#include "Edges/Encoding.h"
#include "Edges/Writer.h"
#include "BraceletRuntimeStructs_llvm.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <algorithm>
#include <cstdlib>
#include <llvm/CodeGen/TargetLowering.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/Metadata.h>
#include <optional>
#include <string>
#include <system_error>

using namespace llvm;
using namespace bracelet;

namespace {
// TODO: track posix_memalign
// Functions that allocate
const DenseSet<StringRef> TRACE_ALLOC_FUNCTION_NAMES =
    BRACELET_ALLOC_FUNCTION_NAMES;

enum class TraceMode { off, indirect_callees, full };

llvm::cl::opt<std::string> BraceletComponentName(
    "bracelet-sbom-component",
    cl::desc("Component name for compiled translation units"));
llvm::cl::opt<std::string>
    BraceletVersion("bracelet-sbom-version",
                  cl::desc("Version string of the compiled translation units"));
llvm::cl::opt<std::string>
    BraceletFileTable("bracelet-file-table",
                    cl::desc("A json mapping from header to sbom information"));
llvm::cl::opt<bool> BraceletEmitEdges("bracelet-emit-edges",
                                    cl::desc("Emit edge data"));
llvm::cl::opt<std::string>
    BraceletDlsymRuntime("bracelet-dlsym-runtime",
                       cl::desc("The bracelet dlsym runtime bitcode"));
llvm::cl::opt<TraceMode> BraceletTraceMode(
    "bracelet-trace", cl::desc("[SLOW] trace values in the program"),
    cl::values(clEnumValN(TraceMode::off, "off", "Disable tracing."),
               clEnumValN(TraceMode::indirect_callees, "indirect_callees",
                          "Only trace indirect callees."),
               clEnumValN(TraceMode::full, "full",
                          "Trace indirect callees, arguments, and stores.")));
llvm::cl::opt<bool>
    NoIncludeDebugData("no-bracelet-include-debug-data",
                       cl::desc("Disables emitting debug data recording names "
                                "of nodes (this debug data is not required "
                                "and is performance intensive)"));

// In order to map callsites to instructions, we currently use debug
// locations. We currently store debug locations by serializing them to a
// string and prepending them to the local names for nodes.
std::string nameWithLocation(Instruction &I, StringRef LocalName) {
  if (!NoIncludeDebugData) {
    const auto &DL = I.getStableDebugLoc();
    std::string Name;
    raw_string_ostream OS(Name);
    DL.print(OS);
    OS << "|";
    if (LocalName.empty()) {
      I.printAsOperand(OS, false);
    } else {
      OS << LocalName;
    }
    return Name;
  } else {
    return "";
  }
}

edges::Node freshLocalForValue(Value *V, edges::FunctionWriter &FW) {
  if (auto *I = dyn_cast<Instruction>(V)) {
    return FW.freshLocal(
        nameWithLocation(*I, I->hasName() ? I->getName() : ""));
  }
  if (V->hasName()) {
    return FW.freshLocal(V->getName());
  }

  if (!NoIncludeDebugData) {
    std::string BBName;
    raw_string_ostream OS(BBName);
    V->printAsOperand(OS, false);
    return FW.freshLocal(BBName);
  } else {
    return FW.freshLocal("");
  }
}

// We want to turn each constant into a single node. Since we're not field
// sensitive, if we come across a constant aggregate (struct or array), we
// create a fresh node and then assign each component of the aggregate to the
// fresh node.
struct MultiConstantNodeBuilder {
  MultiConstantNodeBuilder(edges::FunctionWriter &FW, Constant *Parent)
      : FW(FW), Parent(Parent) {}

  void add(edges::Node N) {
    if (!N)
      return;
    if (Out) {
      if (!FreshNodeCreated) {
        auto OldNode = Out;
        Out = FW.freshLocal(Parent->hasName() ? Parent->getName()
                                              : "<bracelet constant>");
        FW.addEdge(edges::Assign(Out, OldNode));
        FreshNodeCreated = true;
      }
      FW.addEdge(edges::Assign(Out, N));
    } else {
      Out = N;
    }
  }
  edges::Node build() { return Out; }

private:
  edges::FunctionWriter &FW;
  Constant *Parent;
  bool FreshNodeCreated = false;
  edges::Node Out;
};

// This may return a null Node.
edges::Node constantNode(Constant *V, edges::FunctionWriter &FW) {
  if (auto *G = dyn_cast<GlobalValue>(V)) {
    return FW.getGraphWriter().symbol(*G);
  }
  if (isa<ConstantExpr>(V) || isa<ConstantAggregate>(V)) {
    MultiConstantNodeBuilder B(FW, V);
    for (auto *O : V->operand_values()) {
      if (!isa<Constant>(O)) {
        errs() << "Constant " << V << " has a non-constant child " << O << "\n";
        abort();
      }
      B.add(constantNode(cast<Constant>(O), FW));
    }
    return B.build();
  }
  if (isa<ConstantData>(V) || isa<BlockAddress>(V) || isa<NoCFIValue>(V) ||
      isa<DSOLocalEquivalent>(V) || isa<ConstantPtrAuth>(V)) {
    // We don't care about these constants.
    return edges::Node();
  }
  errs() << "Cannot handle constant " << V << "\n";
  abort();
}

struct BraceletRuntime {
  explicit BraceletRuntime(Module &M)
      : RuntimeStructs(M),
        SectionNames(llvm::Triple(M.getTargetTriple()).getVendor() ==
                     llvm::Triple::Apple),
        M(M) {}
  Function *dlsymPageInsert() {
    if (!DlsymPageInsert) {
      linkModule(BraceletDlsymRuntime);
      DlsymPageInsert = M.getFunction("braceletReachabilityDlsymPageInsert");
      assert(DlsymPageInsert);
    }
    return DlsymPageInsert;
  }

  bracelet::runtime_format_llvm::RuntimeStructs RuntimeStructs;
  edges::encoding::SectionNames SectionNames;

private:
  void linkModule(StringRef Path) {
    auto RuntimeBitcode = MemoryBuffer::getFile(Path);
    if (auto Err = RuntimeBitcode.getError()) {
      errs() << "Cannot read " << Path << " due to " << Err.message() << "\n";
      abort();
    }
    auto Runtime = parseBitcodeFile(RuntimeBitcode.get()->getMemBufferRef(),
                                    M.getContext());
    if (auto Err = Runtime.takeError()) {
      errs() << "Failed to parse BraceletDlsymRuntime " << Path << " due to "
             << Err << "\n";
      abort();
    }
    Linker L(M);
    auto Err = L.linkInModule(std::move(*Runtime));
    if (Err) {
      errs() << "Linking bracelet runtime failed.\n";
      abort();
    }
  }

  Module &M;
  Function *DlsymPageInsert = nullptr;
};

struct EdgeVisitor : public InstVisitor<EdgeVisitor> {
  EdgeVisitor(Function &F, edges::FunctionWriter &FW, BraceletRuntime &GR)
      : F(F), FW(FW), GR(GR) {
    PreProcessAllocas PPA(*this);
    PPA.visit(F);
    if (!alloca_replacements.empty()) {
      assert(BraceletTraceMode == TraceMode::full);
      for (auto [old, replacement] : alloca_replacements) {
        BasicBlock::iterator iter(old);
        ReplaceInstWithValue(iter, replacement);
      }
      alloca_replacements.clear();
    }
    NumAllocas = Locals.size();
    // If this function is variadic, we need to set up some additional
    // allocas to model the va_list. This needs to happen first since
    // all local variables with allocation must appear before other
    // local variables.
    //
    // Note, the bitcode will also include allocations and code manipulating
    // the va_list generated by the clang frontend. Since we can't easily
    // identify which allocations are the 'real' va_list, we create this
    // model va_list, and insert a copy between the model and real list
    // when we encounter a va_start intrinsic (see below).
    if (F.isVarArg()) {
      // Setting up a valid struct.__va_list_tag, the internal data
      // structure generated by the clang frontend to handle variadic arguments.
      //    %struct.__va_list_tag = type { i32, i32, ptr, ptr }
      // The first two fields are for book-keeping, and the second two
      // fields are pointers to the register save and overflow argument
      // areas.

      // First, create an allocation for the struct __va_list_tag itself
      VarArgs = FW.freshLocal("TheVarArgsAlloca");
      // Second, create an allocation for the save areas. We only need one
      // since we are field insensitive.
      auto SaveSpace = FW.freshLocal("TheVarArgsArea");

      // Both of these locals represent allocations, so increment NumAllocas
      // accordingly so that the emitted code generates allocation statements.
      NumAllocas += 2;

      // TheVarArgs is a metavariable to which every variadic argument will flow.
      auto ArgData = FW.freshLocal("TheVarArgs");

      // Flow the variadic argument into the TheVarArgs local
      FW.addEdge(
          edges::ArgumentDefinition(ArgData, FW.thisFunction(), F.arg_size()));

      // Store a pointer to TheVarArgs in the SaveSpace
      FW.addEdge(edges::Store(SaveSpace, ArgData));
      // Store the SaveSpace into TheVarArgsAlloca
      FW.addEdge(edges::Store(VarArgs, SaveSpace));
    }
    // Set up locals for non-variadic arguments. We do this second because
    // these locals do not need allocations.
    for (uint32_t I = 0; I < F.arg_size(); I++) {
      FW.addEdge(
		 edges::ArgumentDefinition(node(F.getArg(I)), FW.thisFunction(), I));
    }
  }
  void visitStoreInst(StoreInst &I) {
    addStoreEdge(I, I.getPointerOperand(), I.getValueOperand());
  }
  void visitLoadInst(LoadInst &I) { addLoadEdge(I, &I, I.getPointerOperand()); }
  void visitAtomicCmpXchgInst(AtomicCmpXchgInst &I) {
    addLoadEdge(I, &I, I.getPointerOperand());
    addStoreEdge(I, I.getPointerOperand(), I.getNewValOperand());
  }
  void visitAtomicRMWInst(AtomicRMWInst &I) {
    addLoadEdge(I, &I, I.getPointerOperand());
    addStoreEdge(I, I.getPointerOperand(), I.getValOperand());
  }
  void visitGetElementPtrInst(GetElementPtrInst &I) {
    addAssignEdge(I, &I, I.getPointerOperand());
  }
  void visitPHINode(PHINode &I) {
    for (const auto &V : I.incoming_values()) {
      addAssignEdge(I, &I, V);
    }
  }
  void visitPtrToIntInst(PtrToIntInst &I) {
    addAssignEdge(I, &I, I.getPointerOperand());
  }
  void visitIntToPtrInst(IntToPtrInst &I) {
    addAssignEdge(I, &I, I.getOperand(0));
  }
  void visitBitCastInst(BitCastInst &I) {
    addAssignEdge(I, &I, I.getOperand(0));
  }
  void visitAddrSpaceCastInst(AddrSpaceCastInst &I) {
    addAssignEdge(I, &I, I.getOperand(0));
  }
  void visitSelectInst(SelectInst &I) {
    addAssignEdge(I, &I, I.getTrueValue());
    addAssignEdge(I, &I, I.getFalseValue());
  }
  // TODO: implement va_arg
  void visitVAArgInst(VAArgInst &I) {
    // va_arg is not used by the clang or clang++ frontend
    // so we should never see this...
    assert(false);
  }
  void visitExtractElementInst(ExtractElementInst &I) {
    addAssignEdge(I, &I, I.getOperand(0));
  }
  void visitInsertElementInst(InsertElementInst &I) {
    addAssignEdge(I, &I, I.getOperand(0));
    addAssignEdge(I, &I, I.getOperand(1));
  }
  void visitShuffleVectorInst(ShuffleVectorInst &I) {
    addAssignEdge(I, &I, I.getOperand(0));
    addAssignEdge(I, &I, I.getOperand(1));
  }
  void visitExtractValueInst(ExtractValueInst &I) {
    addAssignEdge(I, &I, I.getOperand(0));
  }
  void visitInsertValueInst(InsertValueInst &I) {
    addAssignEdge(I, &I, I.getOperand(0));
    addAssignEdge(I, &I, I.getOperand(1));
  }
  void visitFreezeInst(FreezeInst &I) { addAssignEdge(I, &I, I.getOperand(0)); }
  void visitReturnInst(ReturnInst &I) {
    if (!allocas_to_free.empty()) {
      assert(BraceletTraceMode == TraceMode::full);
      IRBuilder b(&I);
      for (Value *to_free : allocas_to_free) {
        b.CreateCall(GR.RuntimeStructs.braceletTraceAllocaFree(), {to_free});
      }
    }
    if (I.getNumOperands() > 0) {
      auto N = node(I.getOperand(0));
      if (N)
        FW.addEdge(edges::Return(FW.getGraphWriter().symbol(F), N));
    }
  }
  void visitCastInst(CastInst &I) { addAssignEdge(I, &I, I.getOperand(0)); }
  void visitUnaryOperator(UnaryOperator &I) {
    addAssignEdge(I, &I, I.getOperand(0));
  }
  void visitBinaryOperator(BinaryOperator &I) {
    addAssignEdge(I, &I, I.getOperand(0));
    addAssignEdge(I, &I, I.getOperand(1));
  }
  void visitCallBase(CallBase &I) {
    if (I.isInlineAsm()) {
      // TODO: handle inline assembly.
      return;
    }
    if (BraceletTraceMode != TraceMode::off &&
        (I.getCalledFunction() ==
             GR.RuntimeStructs.braceletTraceTagAllocation() ||
         I.getCalledFunction() == GR.RuntimeStructs.braceletTraceWord() ||
         I.getCalledFunction() ==
             GR.RuntimeStructs.braceletTraceAllocaAllocate() ||
         I.getCalledFunction() == GR.RuntimeStructs.braceletTraceAllocaFree()))
      return;
    switch (I.getIntrinsicID()) {
    case Intrinsic::not_intrinsic:
      break;
    case Intrinsic::vastart: {
      assert(VarArgs);
      // vastart is passed a pointer to the 'real' __va_list_tag
      // Copy the storage area from the model into the real list.
      // This should work as expected without a deep copy because
      // the analysis is flow-insensitive.
      auto Tmp = FW.freshLocal("vastart() tmp");
      FW.addEdge(edges::Load(Tmp, VarArgs));
      FW.addEdge(edges::Store(node(I.getArgOperand(0)), Tmp));
      return;
    }
    case Intrinsic::vacopy: {
      // The first argument is the destination. The second is the source.
      auto *Src = I.getArgOperand(1);
      auto *Dst = I.getArgOperand(0);
      auto Tmp = FW.freshLocal("va_copy() tmp");
      FW.addEdge(edges::Load(Tmp, node(Src)));
      FW.addEdge(edges::Store(node(Dst), Tmp));
      return;
    }
    case Intrinsic::vaend:
      // We don't need to handle this intrinsic.
      return;
    case Intrinsic::memmove:
    case Intrinsic::memcpy: {
      auto *Src = I.getArgOperand(1);
      auto *Dst = I.getArgOperand(0);
      auto Tmp = FW.freshLocal("memcpy tmp");
      FW.addEdge(edges::Load(Tmp, node(Src)));
      FW.addEdge(edges::Store(node(Dst), Tmp));
    }
    default:
      // TODO: handle more intrinsics
      return;
    }
    Value *Callee = I.getCalledOperand();
    while (auto *A = dyn_cast<GlobalAlias>(Callee)) {
      Callee = A->getAliaseeObject();
    }
    auto *FTy = I.getFunctionType();
    edges::Node INode;
    if (FTy->getReturnType()->isVoidTy()) { // NOLINT(bugprone-branch-clone)
      INode = FW.freshLocal(nameWithLocation(
          I, std::string(formatv("VoidCall_{0}", CallVoidNumber++))));
    } else {
      INode = node(&I);
    }

    // Annotate the call instruction with the node ID
    llvm::DebugLoc loc = I.getDebugLoc();
    llvm::DILabel *lab = nullptr;
    std::string encodedNode =
        std::string(bracelet::edges::encoding::BraceletCallsiteDwarfLabelPrefix +
                    std::to_string(*INode.local_idx()));
    if (loc) {
      llvm::DILocalScope *scope = loc->getScope();
      lab = llvm::DILabel::get(I.getContext(), scope, encodedNode,
                               loc->getFile(), loc->getLine());
    } else if (I.getFunction()->getSubprogram()) {
      auto subprog = I.getFunction()->getSubprogram();
      lab = llvm::DILabel::get(I.getContext(), subprog, encodedNode,
                               subprog->getFile(), 0);
    } else {
      // Oof we are giving up and logging, that we failed on a callsite
      llvm::errs() << "Warning failed to annotate call base: ";
      I.print(llvm::errs(), true);
      llvm::errs() << "\n";
    }
    if (lab) {
#ifndef BRACELET_UPSTREAM_LLVM
      I.setMetadata(llvm::GaloisCallMetadatakey, lab);
#endif
    }

    if (I.getCalledFunction() && I.getCalledFunction()->getName() == "dlsym") {
      auto &M = *F.getParent();
      auto &C = M.getContext();
      auto *OpaquePtr = PointerType::getUnqual(C);
      auto *PagePtr =
          new GlobalVariable(M, OpaquePtr, false, GlobalValue::InternalLinkage,
                             ConstantPointerNull::get(OpaquePtr));
      PagePtr->setAlignment(Align(8)); // TODO: don't hard-code ptr alignment
      CallInst::Create(GR.dlsymPageInsert(), {&I, PagePtr})->insertAfter(&I);
      FW.addEdge(edges::DlsymPagePointer(INode, node(PagePtr)));
      return;
    }
    // In C code the prototype f() gets interpreted as f(...), but only in
    // _some_ places. To avoid this issue, we only treat functions as var args
    // if there's at least one argument.
    bool isVarArg = FTy->isVarArg() && I.arg_size() > 0;
    for (uint32_t Idx = 0; Idx < I.arg_size(); Idx++) {
      uint32_t EdgeIdx = Idx;
      if (isVarArg) {
        // Stick all the variadic arguments into index=number of arguments
        // (one greater than the last slot used for the non-variadic args).
        EdgeIdx = std::min(EdgeIdx, static_cast<uint32_t>(FTy->getNumParams()));
      } else {
        assert(EdgeIdx < FTy->getNumParams());
      }
      auto ArgNode = node(I.getArgOperand(Idx));
      if (ArgNode)
        FW.addEdge(edges::ArgumentSupply(INode, ArgNode, EdgeIdx));
    }
    FW.addEdge(edges::Call(INode, node(Callee),
                           isVarArg ? (FTy->getNumParams() + 1)
                                    : FTy->getNumParams()));
    if (BraceletTraceMode != TraceMode::off) {
      if (I.isIndirectCall() || BraceletTraceMode == TraceMode::full) {
        // Trace the callee
        traceValue(Callee, &I);
      }
      if (BraceletTraceMode == TraceMode::full) {
        if (TRACE_ALLOC_FUNCTION_NAMES.contains(Callee->getName())) {
          // Trace an allocation.
          // We don't say that the result of the allocation points to
          // something. Instead we just tag the allocation, itself.
          auto insert_point = I.getInsertionPointAfterDef();
          assert(insert_point);
          IRBuilder b(I.getParent(), *insert_point);
          // NOTE: we'll end up visiting this call, but that's okay since we
          // ignore calls to braceletTraceTagAllocation()
          b.CreateCall(GR.RuntimeStructs.braceletTraceTagAllocation(),
                       {traceSite(node(&I)), &I});
        } else {
          // TODO: tracing return values is hard because inserting
          // instructions _after_ the currently visited instruction will cause
          // us to visit those instructons, and so we'll generate edges for
          // our tracing code. We can skip the calls to our runtime functions,
          // but we might also generate alloca + store instructions which are
          // less easily skipped. We could add them to a set to skip, but that
          // seems sketchy.
          //
          // if (!FTy->getReturnType()->isVoidTy()) {
          //   traceValue(&I, I.getInsertionPointAfterDef());
          // }
          for (uint32_t idx = 0; idx < I.arg_size(); idx++) {
            traceValue(I.getArgOperand(idx), &I);
          }
        }
      }
    }
  }

  void preprocessVisitAllocaInst(AllocaInst &I) {
    // TODO: deal with non-static alloca
    std::string alloca_name = formatv("ALLOCA_{0}", I.getName());
    auto N = FW.freshLocal(alloca_name);
    assert(N.local_idx());
    assert(N.local_idx() == Locals.size());
    if (BraceletTraceMode == TraceMode::full && I.isStaticAlloca()) {
      auto alloc_size = I.getAllocationSize(F.getDataLayout());
      assert(alloc_size);
      auto *allocation = CallInst::Create(
          GR.RuntimeStructs.braceletTraceAllocaAllocate(),
          {traceSite(N),
           ConstantInt::get(IntegerType::get(F.getContext(), 64), *alloc_size)},
          alloca_name);
      allocation->insertInto(&F.getEntryBlock(),
                             F.getEntryBlock().getFirstInsertionPt());
      alloca_replacements.emplace_back(&I, allocation);
      allocas_to_free.push_back(allocation);
      Locals.insert(std::make_pair(allocation, N));
    } else {
      Locals.insert(std::make_pair(&I, N));
    }
  }

  void finish() { FW.finish(NumAllocas); }

private:
  struct PreProcessAllocas : public InstVisitor<PreProcessAllocas> {
    explicit PreProcessAllocas(EdgeVisitor &edge_visitor)
        : edge_visitor(edge_visitor) {}

    void visitAllocaInst(AllocaInst &I) {
      edge_visitor.preprocessVisitAllocaInst(I);
    }

  private:
    EdgeVisitor &edge_visitor;
  };

  GlobalVariable *traceSite(edges::Node n) {
    assert(n);
    GlobalVariable **ptr = &trace_sites[n];
    if (*ptr == nullptr) {
      const auto &DL = F.getDataLayout();
      auto local_idx = n.local_idx();
      assert(local_idx);
      auto *site = new GlobalVariable(
          *F.getParent(), GR.RuntimeStructs.BraceletTraceSite, false,
          GlobalValue::InternalLinkage,
          runtime_format_llvm::ConstantBraceletTraceSite{
              .function = &F,
              .local_idx = ConstantInt::get(
                  GR.RuntimeStructs.fields.BraceletTraceSite.local_idx,
                  *local_idx)}
              .get(GR.RuntimeStructs));
      site->setAlignment(DL.getABITypeAlign(GR.RuntimeStructs.BraceletTraceSite));
      site->setSection(GR.SectionNames.trace_site);
      *ptr = site;
    }
    return *ptr;
  }
  void traceValue(Value *value, Instruction *insert_before) {
    assert(BraceletTraceMode != TraceMode::off);
    auto n = node(value);
    if (!n || !n.local_idx()) {
      // If the value isn't a local or is a boring integer (e.g. not ptr2int)
      // we don't even bother.
      return;
    }
    IRBuilder b(insert_before);
    auto *ty = value->getType();
    auto *site = traceSite(n);
    const auto &data_layout = F.getDataLayout();
    auto size_bytes = data_layout.getTypeStoreSize(ty);
    auto ptr_size = data_layout.getPointerSize();
    if (size_bytes < ptr_size) {
      // We're only tracing pointers, so it doesn't make sense to even try to
      // trace something that's less than a pointer's-width.
      return;
    }
    if (ty->isPointerTy()) {
      auto value_as_int = b.CreatePtrToInt(
          value, IntegerType::get(F.getContext(), size_bytes * 8));
      b.CreateCall(GR.RuntimeStructs.braceletTraceWord(), {site, value_as_int});
    } else if (ty->isIntegerTy(ptr_size * 8)) {
      b.CreateCall(GR.RuntimeStructs.braceletTraceWord(), {site, value});
    } else {
      auto *alloc = b.CreateAlloca(ty);
      b.CreateStore(value, alloc);
      b.CreateCall(
          GR.RuntimeStructs.braceletTraceBuffer(),
          {site, alloc,
           ConstantInt::get(IntegerType::get(F.getContext(), 64), size_bytes)});
    }
  }
  void addStoreEdge(Instruction &I, Value *To, Value *From) {
    auto FromN = node(From);
    auto ToN = node(To);
    if (FromN && ToN) {
      FW.addEdge(edges::Store(ToN, FromN));
      if (BraceletTraceMode == TraceMode::full) {
        traceValue(To, &I);
        traceValue(From, &I);
      }
    }
  }
  void addLoadEdge(Instruction &I, Value *To, Value *From) {
    auto FromN = node(From);
    if (!FromN) {
      errs() << "WARN(BraceletReachability): In function '" << F.getName()
             << "' UB with instruciton " << I << "\n";
      return;
    }
    FW.addEdge(edges::Load(node(To), FromN));
  }
  void addAssignEdge(Instruction &I, Value *To, Value *From) {
    auto FromN = node(From);
    if (FromN)
      FW.addEdge(edges::Assign(node(To), FromN));
  }

  edges::Node node(Value *V) {
    if (auto *ConstantValue = dyn_cast<Constant>(V)) {
      return constantNode(ConstantValue, FW);
    }
    if (isa<Instruction>(V) || isa<Argument>(V)) {
      auto Iter = Locals.find(V);
      if (Iter != Locals.end()) {
        return Iter->second;
      }
      auto N = freshLocalForValue(V, FW);
      Locals.insert(std::make_pair(V, N));
      return N;
    }
    if (isa<InlineAsm>(V)) {
      // TODO: handle this
      return edges::Node();
    }
    errs() << "cannot handle value ";
    V->print(errs(), true);
    errs() << "\n";
    abort();
  }

  Function &F;
  edges::FunctionWriter &FW;
  DenseMap<Value *, edges::Node> Locals;
  absl::flat_hash_map<edges::Node, GlobalVariable *> trace_sites;
  edges::Node VarArgs;
  uint32_t CallVoidNumber = 0;
  uint32_t NumAllocas;
  BraceletRuntime &GR;
  std::vector<Value *> allocas_to_free;
  std::vector<std::tuple<Instruction *, Instruction *>> alloca_replacements;
};

std::unordered_map<std::string, bracelet::edges::encoding::SBOMInformation>
getSbomMap() {
  std::unordered_map<std::string, bracelet::edges::encoding::SBOMInformation> mp;
  if (!BraceletFileTable.empty()) {
    auto fl = llvm::MemoryBuffer::getFile(BraceletFileTable);
    if (auto Err = fl.getError()) {
      llvm::errs() << "Failed to read " << BraceletFileTable;
      return mp;
    }
    auto val = llvm::json::parse(fl.get()->getBuffer());

    if (auto Err = val.takeError()) {
      llvm::errs() << "Failed to parse " << BraceletFileTable;
      return mp;
    }

    for (auto &[k, v] : *val->getAsObject()) {
      auto pname = *(*v.getAsObject())["port-name"].getAsString();
      auto vnam = *(*v.getAsObject())["port-version"].getAsString();
      mp[k.str()] = {pname.str(), vnam.str()};
    }
  }

  return mp;
}

std::optional<bracelet::edges::encoding::SBOMInformation> getSbomInfo() {
  if (!BraceletComponentName.empty() && !BraceletVersion.empty()) {
    return bracelet::edges::encoding::SBOMInformation{BraceletComponentName,
                                                    BraceletVersion};
  }

  return std::nullopt;
}

bool runBraceletReachability(Module &M) {
  if (!BraceletEmitEdges)
    return false;
  constexpr StringLiteral GlobalValueFunctionPrefix(
      "bracelet_reachability_globals");
  // Snapshot the global values before we start adding out own.
  std::vector<GlobalValue *> GlobalValues;
  for (GlobalValue &GValue : M.global_values()) {
    GlobalValues.push_back(&GValue);
  }

  auto SbomInfo = getSbomInfo();
  auto SbomMap = getSbomMap();

  DenseMap<Comdat *, std::unique_ptr<edges::GraphWriter>> GraphWriters;
  auto GraphWriterForComdat = [&](Comdat *C) -> edges::GraphWriter & {
    if (!GraphWriters.contains(C)) {
      GraphWriters.insert(
          std::make_pair(C, std::make_unique<edges::GraphWriter>(
                                C, SbomInfo, SbomMap, !NoIncludeDebugData)));
    }
    return *GraphWriters[C];
  };
  DenseMap<Comdat *, std::unique_ptr<edges::FunctionWriter>> GlobalWriters;
  IRBuilder<> IRB(M.getContext());
  // Emit edges for global value initializers by adding them as edges in a new
  // function.
  for (GlobalValue *GValue : GlobalValues) {
    if (auto *GVar = dyn_cast<GlobalVariable>(GValue)) {
      if (!GVar->isExternallyInitialized() && GVar->hasInitializer() &&
          !GVar->getName().starts_with("llvm.")) {
        auto *C = GVar->getComdat();
        if (!GlobalWriters.contains(C)) {
          Function *F = Function::Create(
              FunctionType::get(Type::getVoidTy(M.getContext()), false),
              Function::InternalLinkage,
              Twine(GlobalValueFunctionPrefix) + "_" + M.getName() + "_" +
                  (C ? C->getName() : ""),
              M);
          BasicBlock *BB = BasicBlock::Create(M.getContext(), "entry", F);
          IRB.SetInsertPoint(BB);
          IRB.CreateRetVoid();
          GlobalWriters.insert(std::make_pair(
              C, std::make_unique<edges::FunctionWriter>(
                     GraphWriterForComdat(C), *F, !NoIncludeDebugData)));
        }
        edges::FunctionWriter &FW = *GlobalWriters[C];
        auto Value = constantNode(GVar->getInitializer(), FW);
        if (Value)
          FW.addEdge(edges::Store(constantNode(GVar, FW), Value));
      }
    }
  }
  for (auto &[_, FW] : GlobalWriters) {
    FW->finish(0);
  }
  BraceletRuntime GR(M);
  std::vector<Function *> FunctionsToEdit;
  // We'll be inserting new functions when we link in the runtimes,
  // so we first create a list of functions to iterate through, so we're not
  // iterating through a list we're inserting into.
  for (Function &F : M.functions())
    FunctionsToEdit.push_back(&F);
  for (Function *F : FunctionsToEdit) {
    if (F->isIntrinsic() || F->isDeclaration() ||
        F->getName().starts_with(GlobalValueFunctionPrefix) ||
        F->getName().starts_with("__liballocs"))
      continue;
    edges::FunctionWriter FW(GraphWriterForComdat(F->getComdat()), *F,
                             !NoIncludeDebugData);
    EdgeVisitor Ev(*F, FW, GR);
    Ev.visit(F);
    Ev.finish();
  }
  for (auto &[_, GW] : GraphWriters) {
    GW->writeTo(M);
  }
  return true;
}

struct LegacyBraceletReachability : public ModulePass {
  static char ID;
  LegacyBraceletReachability() : ModulePass(ID) {}
  bool runOnModule(Module &M) override { return runBraceletReachability(M); }
};

struct BraceletReachability : PassInfoMixin<BraceletReachability> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    if (!runBraceletReachability(M))
      return PreservedAnalyses::all();
    return PreservedAnalyses::none();
  } // namespace
};
} // namespace

char LegacyBraceletReachability::ID = 0;

static RegisterPass<LegacyBraceletReachability>
    X("braceletreachability", "Good BraceletReachability World Pass",
      false /* Only looks at CFG */, false /* Analysis Pass */);

/* New PM Registration */
// This registration entry point requires external linkage.
llvm::PassPluginLibraryInfo getBraceletReachabilityPluginInfo() { // NOLINT(misc-use-internal-linkage)
  return {LLVM_PLUGIN_API_VERSION, "BraceletReachability", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerOptimizerLastEPCallback(
                [](llvm::ModulePassManager &PM, OptimizationLevel,
                   ThinOrFullLTOPhase) { PM.addPass(BraceletReachability()); });
            PB.registerPipelineParsingCallback(
                [](StringRef Name, llvm::ModulePassManager &PM,
                   ArrayRef<llvm::PassBuilder::PipelineElement>) {
                  if (Name == "braceletreachability") {
                    PM.addPass(BraceletReachability());
                    return true;
                  }
                  return false;
                });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getBraceletReachabilityPluginInfo();
}
