#include "lob/policy/table.hpp"

#include <cstdio>
#include <cstring>

namespace lob::policy {
namespace {
constexpr char kMagic[8] = {'L', 'O', 'B', 'P', 'O', 'L', '\0', '\0'};

TableHeader make_header(std::uint64_t param_hash, double discount, double residual,
                        std::uint64_t sweeps, std::int64_t queue_scale, double dt_s) {
  TableHeader h{};
  std::memcpy(h.magic, kMagic, sizeof kMagic);
  h.schema        = kTableSchema;
  h.num_states    = kNumStates;
  h.num_actions   = kNumActions;
  h.max_inventory = kMaxInventory;
  h.queue_buckets = kQueueBuckets;
  h.quote_levels  = kQuoteLevels;
  h.imb_buckets   = kImbBuckets;
  h.param_hash    = param_hash;
  h.discount      = discount;
  h.residual      = residual;
  h.sweeps        = sweeps;
  h.queue_scale   = queue_scale;
  h.dt_s          = dt_s;
  return h;
}
}  // namespace

bool PolicyTable::save(const std::string& path, const std::vector<std::uint8_t>& policy,
                       const std::vector<double>& value, std::uint64_t param_hash,
                       double discount, double residual, std::uint64_t sweeps,
                       std::int64_t queue_scale, double dt_s, std::string* why) {
  auto fail = [&](const char* m) { if (why) *why = m; return false; };
  if (policy.size() != kNumStates || value.size() != kNumStates)
    return fail("policy/value length does not match the state space");

  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return fail("cannot open the table for writing");
  const TableHeader h = make_header(param_hash, discount, residual, sweeps, queue_scale, dt_s);
  bool ok = std::fwrite(&h, sizeof h, 1, f) == 1
         && std::fwrite(policy.data(), 1, policy.size(), f) == policy.size()
         && std::fwrite(value.data(), sizeof(double), value.size(), f) == value.size();
  ok = (std::fclose(f) == 0) && ok;
  return ok ? true : fail("short write");
}

bool PolicyTable::load(const std::string& path, std::string* why) {
  // Clears EVERYTHING. This used to clear policy_ only, so a truncated body
  // left value_ holding kNumStates entries of whatever had been read plus
  // zeros, and header_ describing the previous table. A caller that checks the
  // return value is fine; one that inspects value_ afterwards reads a vector
  // that looks valid. Every other check in this file exists to stop a wrong
  // table answering a lookup, and this one left half a table behind.
  auto fail = [&](const std::string& m) {
    if (why) *why = m;
    policy_.clear();
    value_.clear();
    header_ = TableHeader{};
    return false;
  };

  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return fail("cannot open " + path);
  TableHeader h{};
  if (std::fread(&h, sizeof h, 1, f) != 1) { std::fclose(f); return fail("truncated header"); }

  if (std::memcmp(h.magic, kMagic, sizeof kMagic) != 0) { std::fclose(f); return fail("not a policy table"); }
  if (h.schema != kTableSchema) {
    std::fclose(f);
    return fail("table schema " + std::to_string(h.schema) + " but this build expects "
                + std::to_string(kTableSchema));
  }
  // The discretisation itself, not merely a version stamp. A table solved for a
  // different state space would otherwise answer every lookup with a real
  // action computed for a different question.
  if (h.num_states != kNumStates || h.num_actions != kNumActions ||
      h.max_inventory != kMaxInventory || h.queue_buckets != kQueueBuckets ||
      h.quote_levels != kQuoteLevels || h.imb_buckets != kImbBuckets) {
    std::fclose(f);
    return fail("table was solved over a different discretisation than this build uses");
  }

  policy_.assign(h.num_states, 0);
  value_.assign(h.num_states, 0.0);
  const bool ok = std::fread(policy_.data(), 1, policy_.size(), f) == policy_.size()
               && std::fread(value_.data(), sizeof(double), value_.size(), f) == value_.size();
  std::fclose(f);
  if (!ok) return fail("truncated body");
  for (std::uint8_t a : policy_)
    if (a >= kNumActions) return fail("table contains an action outside the action set");
  header_ = h;
  return true;
}

}  // namespace lob::policy
