#include "ObjectParsing.h"
#include "Edges/Edges.h"
#include "Edges/Encoding.h"
#include "Result/Result.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/DWARF/DWARFAddressRange.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <vector>

using namespace bracelet;
using namespace bracelet::object_parsing;

namespace {

struct LabelResult {
  uint64_t functionAddr;
  CallsiteEdge edge;
};

Result<edges::Node> ParseNodeIDFromName(uint64_t symbol, llvm::StringRef name) {
  auto nodeIdStr = name.substr(
      bracelet::edges::encoding::BraceletCallsiteDwarfLabelPrefix.length());
  uint64_t out;
  BRACELET_ENSURE(!nodeIdStr.getAsInteger(10, out),
                "Should be able to parse: %s as integer", nodeIdStr);
  return bracelet::edges::Node::local(symbol, out);
}

Result<std::string> DwarfAsString(llvm::DWARFFormValue val) {
  auto R = val.getAsCString();
  if (auto E = R.takeError()) {
    return bracelet::Error("Could not convert DWARF from to string " +
                         llvm::toString(std::move(E)));
  }

  return std::string(*R);
}

Result<std::string> RecursivelyGetLabelName(llvm::DWARFDie &die) {
  auto nm = die.findRecursively(llvm::dwarf::DW_AT_name);
  BRACELET_ENSURE(nm.has_value(), "DIE should have a DW_AT_name");
  return DwarfAsString(*nm);
}

Result<std::optional<LabelResult>>
DwarfLabelDIEToRecord(llvm::DWARFUnit &cu, llvm::DWARFDie &dwDIE,
                      Object::AddressTranslation addrTranslate) {
  auto decodedName = BRACELET_TRY(RecursivelyGetLabelName(dwDIE));

  auto val = dwDIE.find(llvm::dwarf::DW_AT_low_pc);
  // If we dont have a low pc then we are abstract
  if (!val) {
    return std::nullopt;
  }

  auto callsiteAddr = *llvm::dwarf::toAddress(val);

  llvm::SmallVector<llvm::DWARFDie, 10> InlineChain;

  // The last inline chain is where we were actually inlined,
  // subprog for address is the first in chain
  cu.getInlinedChainForAddress(callsiteAddr, InlineChain);
  BRACELET_ENSURE(
      !InlineChain.empty(),
      "Expected a DWARF subprocedure to contain the callsite address %x",
      callsiteAddr);
  auto subRoutine = InlineChain.back();

  auto subLowAttr = subRoutine.findRecursively(llvm::dwarf::DW_AT_low_pc);
  BRACELET_ENSURE(subLowAttr.has_value(),
                "Subroutine for callsite %x should have low pc", callsiteAddr);
  auto subsectionAddress = *llvm::dwarf::toAddress(subLowAttr);

  auto translatedSectionAddressFunc =
      BRACELET_TRY(addrTranslate(subsectionAddress));

  auto translatedCallsiteAddr = BRACELET_TRY(addrTranslate(callsiteAddr));
  auto parsedNodeID = BRACELET_TRY(
      ParseNodeIDFromName(translatedSectionAddressFunc, decodedName));
  return LabelResult{translatedSectionAddressFunc,
                     {translatedCallsiteAddr, parsedNodeID}};
}

Result<Object::CUCallsites>
CollectCULabels(llvm::DWARFUnit &cu, Object::AddressTranslation addrTranslate) {
  Object::CUCallsites res;
  for (auto &die : cu.dies()) {
    if (die.getTag() == llvm::dwarf::DW_TAG_label) {
      auto dwDIE = llvm::DWARFDie(&cu, &die);
      // a label should always eventually have a name
      auto decodedName = BRACELET_TRY(RecursivelyGetLabelName(dwDIE));
      if (decodedName.rfind(
              bracelet::edges::encoding::BraceletCallsiteDwarfLabelPrefix, 0) ==
          0) {
        auto record =
            BRACELET_TRY(DwarfLabelDIEToRecord(cu, dwDIE, addrTranslate));
        if (record.has_value()) {
          res.insert({record->functionAddr, record->edge});
        }
      }
    }
  }
  return res;
}

Result<Object::CUInlineEdges>
CollectCUInlineEdges(llvm::DWARFUnit &cu,
                     Object::AddressTranslation addrTranslate) {
  Object::CUInlineEdges res;
  for (auto &die : cu.dies()) {
    if (die.getTag() == llvm::dwarf::DW_TAG_inlined_subroutine) {
      auto dwDIE = llvm::DWARFDie(&cu, &die);

      auto origin = dwDIE.find(llvm::dwarf::DW_AT_abstract_origin);
      // If we don't have an abstract_origin ignore this node
      if (!origin)
        continue;

      const char *nameVal = dwDIE.getName(llvm::DINameKind::LinkageName);
      if (nameVal == NULL)
        continue;
      std::string_view name = nameVal;

      // Find where we're inlined
      while (!dwDIE.isSubprogramDIE()) {
        dwDIE = dwDIE.getParent();
      }

      // This also checks low pc high pc
      auto ranges = dwDIE.getAddressRanges();
      if (auto E = ranges.takeError()) {
        llvm::errs() << "WARN: failed to get address range for inlinee " << E;
        continue;
      }
      std::vector<uint64_t> lowPcs;
      std::transform(ranges->begin(), ranges->end(), std::back_inserter(lowPcs),
                     [](llvm::DWARFAddressRange rng) { return rng.LowPC; });
      if (lowPcs.empty()) {
        llvm::errs() << "WARN: inlinee has empty range at offset "
                     << dwDIE.getOffset();
        continue;
      }
      auto lowpcInlined = *std::min_element(lowPcs.begin(), lowPcs.end());
      auto translatedInlinerAddressFunc =
          BRACELET_TRY(addrTranslate(lowpcInlined));

      InlineEdge edge{edges::Node::symbol(translatedInlinerAddressFunc), name};
      res.insert({translatedInlinerAddressFunc, edge});
    }
  }
  return res;
}

} // namespace

Result<void> Object::visitCUAddressMaps(
    std::function<Result<void>(const CUCallsites &)> visit) {
  return this->visitDwarfContexts(
      [visit](auto dw_context,
              AddressTranslation addrTranslate) -> Result<void> {
        for (std::unique_ptr<llvm::DWARFUnit> &cu :
             dw_context->compile_units()) {
          // TODO: Fetch CU name
          auto labs = BRACELET_TRY(CollectCULabels(*cu, addrTranslate));
          BRACELET_TRY(visit(labs));
        }
        return bracelet::ok();
      });
}

Result<void> Object::visitCUInlineEdges(
    std::function<Result<void>(const CUInlineEdges &)> visit) {
  return this->visitDwarfContexts(
      [visit](auto dw_context,
              AddressTranslation addrTranslate) -> Result<void> {
        for (std::unique_ptr<llvm::DWARFUnit> &cu :
             dw_context->compile_units()) {
          auto edges = BRACELET_TRY(CollectCUInlineEdges(*cu, addrTranslate));
          BRACELET_TRY(visit(edges));
        }
        return bracelet::ok();
      });
}
