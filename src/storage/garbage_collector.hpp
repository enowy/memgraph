#pragma once

#include "data_structures/concurrent/skiplist.hpp"
#include "logging/loggable.hpp"
#include "mvcc/id.hpp"
#include "mvcc/version_list.hpp"
#include "transactions/engine.hpp"

/**
  @template T type of underlying record in mvcc
 */
template <typename T>
class GarbageCollector : public Loggable {
 public:
  GarbageCollector(SkipList<mvcc::VersionList<T> *> *skiplist,
                   tx::Engine *engine)
      : Loggable("MvccGc"), skiplist_(skiplist), engine_(engine) {
    permanent_assert(skiplist != nullptr, "Skiplist can't be nullptr.");
    permanent_assert(engine != nullptr, "Engine can't be nullptr.");
  };

  /**
   *@brief - Runs garbage collector.
   */
  void Run() {
    auto accessor = this->skiplist_->access();
    uint64_t count = 0;
    // Acquire id of either the oldest active transaction, or the id of a
    // transaction that will be assigned next. We should make sure that we
    // get count before we ask for active transactions since some
    // transaction could possibly increase the count while we ask for
    // oldest_active transaction.
    const auto next_id = engine_->count() + 1;
    const auto id = this->engine_->oldest_active().get_or(next_id);
    if (logger.Initialized())
      logger.trace("Gc started cleaning everything deleted before {}", id);
    for (auto x : accessor) {
      // If the mvcc is empty, i.e. there is nothing else to be read from it
      // we can delete it.
      if (x->GcDeleted(id)) count += accessor.remove(x);
    }
    if (logger.Initialized()) logger.trace("Destroyed: {}", count);
  }

 private:
  SkipList<mvcc::VersionList<T> *> *skiplist_{nullptr};  // Not owned.
  tx::Engine *engine_{nullptr};                          // Not owned.
};
