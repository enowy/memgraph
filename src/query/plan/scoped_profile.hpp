#pragma once

#include <cstdint>

#include "query/plan/profile.hpp"
#include "utils/tsc.hpp"

namespace query {

namespace plan {

/**
 * A RAII class used for profiling logical operators. Instances of this class
 * update the profiling data stored within the `ExecutionContext` object and build
 * up a tree of `ProfilingStats` instances. The structure of the `ProfilingStats`
 * tree depends on the `LogicalOperator`s that were executed.
 */
class ScopedProfile {
 public:
  ScopedProfile(uint64_t key, const char *name,
                query::ExecutionContext *context) noexcept
      : context_(context) {
    if (UNLIKELY(context_->is_profile_query)) {
      root_ = context_->stats_root;

      // Are we the root logical operator?
      if (!root_) {
        stats_ = &context_->stats;
        stats_->key = key;
        stats_->name = name;
      } else {
        stats_ = nullptr;

        // Was this logical operator already hit on one of the previous pulls?
        auto it = std::find_if(root_->children.begin(), root_->children.end(),
                               [key](auto &stats) { return stats.key == key; });

        if (it == root_->children.end()) {
          root_->children.emplace_back();
          stats_ = &root_->children.back();
          stats_->key = key;
          stats_->name = name;
        } else {
          stats_ = &(*it);
        }
      }

      context_->stats_root = stats_;
      stats_->actual_hits++;
      start_time_ = utils::ReadTSC();
    }
  }

  ~ScopedProfile() noexcept {
    if (UNLIKELY(context_->is_profile_query)) {
      stats_->num_cycles += utils::ReadTSC() - start_time_;

      // Restore the old root ("pop")
      context_->stats_root = root_;
    }
  }

 private:
  query::ExecutionContext *context_;
  ProfilingStats *root_;
  ProfilingStats *stats_;
  unsigned long long start_time_;
};

}  // namespace plan
}  // namespace query