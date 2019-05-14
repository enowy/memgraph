#pragma once

#include <ostream>

#include "database/graph_db_accessor.hpp"

namespace database {

/// Class which generates sequence of openCypher queries which can be used to
/// dump the database state.
///
/// Currently, only vertices and edges are being dumped, one-by-one in multiple
/// queries. Indices keys, constraints, roles, etc. are currently not dumped.
class CypherDumpGenerator {
 public:
  explicit CypherDumpGenerator(GraphDbAccessor *dba);

  bool NextQuery(std::ostream *os);

 private:
  // A helper class that keeps container and its iterators.
  template <typename TContainer>
  class ContainerState {
   public:
    explicit ContainerState(TContainer container)
        : container_(std::move(container)),
          current_(container_.begin()),
          end_(container_.end()),
          empty_(current_ == end_) {}

    auto GetCurrentAndAdvance() {
      auto to_be_returned = current_;
      if (current_ != end_) ++current_;
      return to_be_returned;
    }

    bool ReachedEnd() const { return current_ == end_; }

    // Returns true iff the container is empty.
    bool Empty() const { return empty_; }

   private:
    TContainer container_;

    using TIterator = decltype(container_.begin());
    TIterator current_;
    TIterator end_;

    bool empty_;
  };

  GraphDbAccessor *dba_;

  bool cleaned_internals_;

  std::optional<ContainerState<decltype(dba_->Vertices(false))>>
      vertices_state_;
  std::optional<ContainerState<decltype(dba_->Edges(false))>> edges_state_;
};

/// Dumps database state to output stream as openCypher queries.
///
/// Currently, it only dumps vertices and edges of the graph. In the future,
/// it should also dump indexes, constraints, roles, etc.
void DumpToCypher(std::ostream *os, GraphDbAccessor *dba);

}  // namespace database
