// The policy table: what the offline solve ships to the execution path.
//
// A binary artefact, not a model. Reading it is a bounds check and one array
// index — no solving, no branching on anything the model knows about, nothing
// that can take a different amount of time depending on the state it is asked
// about. That is the contract docs/00 draws between the layers, and this file
// is the seam.
//
// The header carries the discretisation it was solved over, not just a version
// number. A bare version number only catches the case where someone remembers
// to bump it; storing the shape means a table solved before kMaxInventory
// changed is REJECTED by a build that changed it, which is the failure that
// would otherwise be silent and total — every lookup returning a real action
// computed for a different state.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lob/policy/state.hpp"

namespace lob::policy {

inline constexpr std::uint32_t kTableSchema = 2;

struct TableHeader {
  char          magic[8];          // "LOBPOL\0"
  std::uint32_t schema;
  std::uint32_t num_states;
  std::uint32_t num_actions;
  std::uint32_t max_inventory;
  std::uint32_t queue_buckets;
  std::uint32_t quote_levels;
  std::uint32_t imb_buckets;
  std::uint32_t reserved;
  std::uint64_t param_hash;        // of the MdpParams that produced this
  double        discount;
  double        residual;          // max-norm at the last sweep
  std::uint64_t sweeps;
  // The reference depth queue_bucket() divides by. It travels with the table
  // so the executor cannot bucket on a different scale than the solver did —
  // which would put every lookup in the wrong row while looking fine.
  std::int64_t  queue_scale;
  // The decision epoch the process was measured over, in seconds. Every
  // probability in the model is per epoch, so a policy consulted twice as often
  // as it was solved for is answering a question about twice as much time as has
  // actually passed. Nothing about that looks wrong from either side, which is
  // why the number travels with the table rather than living in two places.
  double        dt_s;
};
static_assert(sizeof(TableHeader) == 88);

class PolicyTable {
 public:
  // Writes policy and value side by side. The value function ships because the
  // roadmap asks that a decision be explainable: "you can point at a state and
  // say why the policy quotes what it quotes" needs the numbers it compared.
  [[nodiscard]] static bool save(const std::string& path, const std::vector<std::uint8_t>& policy,
                                 const std::vector<double>& value, std::uint64_t param_hash,
                                 double discount, double residual, std::uint64_t sweeps,
                                 std::int64_t queue_scale, double dt_s, std::string* why);

  [[nodiscard]] bool load(const std::string& path, std::string* why);

  // The hot path. Out-of-range yields "quote nothing" rather than reading past
  // the end: a policy that cannot answer must decline, never guess.
  [[nodiscard]] std::uint8_t action_for(std::uint32_t state) const noexcept {
    return state < policy_.size() ? policy_[state] : std::uint8_t{0};
  }
  [[nodiscard]] double value_of(std::uint32_t state) const noexcept {
    return state < value_.size() ? value_[state] : 0.0;
  }
  [[nodiscard]] bool loaded() const noexcept { return !policy_.empty(); }
  [[nodiscard]] const TableHeader& header() const noexcept { return header_; }

 private:
  TableHeader               header_{};
  std::vector<std::uint8_t> policy_;
  std::vector<double>       value_;
};

}  // namespace lob::policy
