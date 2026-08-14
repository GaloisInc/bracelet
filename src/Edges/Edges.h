#pragma once
#include <assert.h>
#include <boost/operators.hpp>
#include <cstdint>
#include <optional>
#include <stdint.h>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>

#include "Result/Result.h"

namespace bracelet {
namespace edges {
// When writing symbol address will be the index in the symbol table.
// When reading, symbol addresses are bracelet::object_parsing::Address.
struct Node : public boost::totally_ordered<Node> {
  Node() : m_raw(0) {}

  // Construct a Node corresponding to the symbol at addr
  static Node symbol(uint64_t addr) {
    // We use 0 as sentinel values.
    addr++;
    assert((addr >> (64 - 16)) == 0);
    Node n;
    n.m_raw = addr << 16;
    return n;
  }
  // Construct a node corresponding to the local value of index local_idx for
  // the symbol at address symbol.
  static Node local(uint64_t symbol, uint64_t local_idx) {
    // We use 0 as sentinel values.
    symbol++;
    local_idx++;
    assert((symbol >> (64 - 16)) == 0);
    assert(local_idx < (1 << 16));
    Node n;
    n.m_raw = (symbol << 16) | local_idx;
    return n;
  }

  // Return the symbol component of the node
  uint64_t symbol() const {
    assert(*this);
    return (m_raw >> 16) - 1;
  }
  // Return the local index of the node if it corresponds to a local
  std::optional<uint16_t> local_idx() const {
    assert(*this);
    auto raw_local = m_raw & 0xffff;
    if (raw_local == 0)
      return std::nullopt;
    return raw_local - 1;
  }
  operator bool() const { return m_raw != 0; }

  bool operator==(const Node &other) const { return m_raw == other.m_raw; }
  bool operator<(const Node &other) const { return m_raw < other.m_raw; }

  uint64_t raw() const { return m_raw; }

  template <typename H> friend H AbslHashValue(H h, const Node &n) {
    return H::combine(std::move(h), n.raw());
  }
  template <typename Sink>
  friend void AbslStringify(Sink &sink, const Node &value) {
    if (!value)
      absl::Format(&sink, "Node()");
    else if (auto idx = value.local_idx())
      absl::Format(&sink, "Node::local(%x, %d)", value.symbol(), *idx);
    else
      absl::Format(&sink, "Node::symbol(%x)", value.symbol());
  }

private:
  uint64_t m_raw;
};

// An un-labelled edge between two nodes
struct SingletonEdge : public std::tuple<Node, Node> {
  SingletonEdge(Node to, Node from) : std::tuple<Node, Node>(to, from) {
    assert(to);
    assert(from);
  }
  static constexpr size_t NUM_COMPONENTS = 2;
};
// An edge between two nodes labelled with an index
struct IndexedEdge : public std::tuple<Node, Node, uint32_t> {
  IndexedEdge(Node to, Node from, uint32_t idx)
      : std::tuple<Node, Node, uint32_t>(to, from, idx) {
    assert(to);
    assert(from);
  }
  static constexpr size_t NUM_COMPONENTS = 3;
};

#define S(name, to, from)                                                      \
  struct name : public SingletonEdge {                                         \
    static constexpr std::string_view NAME = #name;                            \
    name(Node to, Node from) : SingletonEdge(to, from) {}                      \
    Node to() const { return std::get<0>(*this); }                             \
    Node from() const { return std::get<1>(*this); }                           \
    template <typename Sink>                                                   \
    friend void AbslStringify(Sink &sink, const name &value) {                 \
      absl::Format(&sink, "%s(%v, %v)", NAME, value.to(), value.from());       \
    }                                                                          \
  }
#define I(name, to, from, idx)                                                 \
  struct name : public IndexedEdge {                                           \
    static constexpr std::string_view NAME = #name;                            \
    name(Node to, Node from, uint32_t idx) : IndexedEdge(to, from, idx) {}     \
    Node to() const { return std::get<0>(*this); }                             \
    Node from() const { return std::get<1>(*this); }                           \
    uint32_t idx() const { return std::get<2>(*this); }                        \
    template <typename Sink>                                                   \
    friend void AbslStringify(Sink &sink, const name &value) {                 \
      absl::Format(&sink, "%s(%v, %v, %v)", NAME, value.to(), value.from(),    \
                   value.idx());                                               \
    }                                                                          \
  }
S(Assign, dest, source);
S(Load, into, addr);
S(Store, addr, value);
I(Call, callsite, callee, nargs);
S(Return, func, value);
I(ArgumentDefinition, value, func, arg_no);
I(ArgumentSupply, callsite, value, arg_no);
S(DlsymPagePointer, dlsym_output, page_ptr);
#undef I
#undef S
using AnyEdge =
    std::variant<Assign, Load, Store, Call, Return, ArgumentDefinition,
                 ArgumentSupply, DlsymPagePointer>;

template <template <typename Tx> typename T, typename... Xs>
struct EdgeTupleHelper;
template <template <typename Tx> typename T, typename... Xs>
struct EdgeTupleHelper<T, std::variant<Xs...>> {
  using Out = std::tuple<T<Xs>...>;
};
// EdgeTuple<T> = std::tuple<T<Assign>, T<Load>, T<Store>, ...>
template <template <typename Tx> typename T>
using EdgeTuple = typename EdgeTupleHelper<T, AnyEdge>::Out;

template <typename F, size_t N, typename... Ts> struct VisitTupleHelper {
  static bracelet::Result<void> run(std::tuple<Ts...> &X, F Cb) {
    BRACELET_TRY(Cb(std::get<sizeof...(Ts) - N>(X)));
    return VisitTupleHelper<F, N - 1, Ts...>::run(X, Cb);
  }
};
template <typename F, typename... T> struct VisitTupleHelper<F, 0, T...> {
  static bracelet::Result<void> run(std::tuple<T...> &, F) {
    return bracelet::ok();
  }
};

// Call BRACELET_TRY(cb(std::get<0>>(tuple))); BRACELET_TRY(cb(std::get<1>(tuple)));
// ...
template <typename F, typename... T>
bracelet::Result<void> visitTuple(std::tuple<T...> &Tuple, F Cb) {
  return VisitTupleHelper<F, sizeof...(T), T...>::run(Tuple, Cb);
}

} // namespace edges
} // namespace bracelet
