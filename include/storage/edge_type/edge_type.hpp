#pragma once

#include <ostream>
#include <stdint.h>

#include "storage/edge.hpp"
#include "storage/edge_accessor.hpp"
#include "storage/indexes/impl/nonunique_unordered_index.hpp"
#include "storage/type_group_edge.hpp"
#include "utils/char_str.hpp"
#include "utils/reference_wrapper.hpp"
#include "utils/total_ordering.hpp"

using EdgeTypeIndexRecord = IndexRecord<TypeGroupEdge, std::nullptr_t>;

class EdgeType : public TotalOrdering<EdgeType>
{
public:
    using type_index_t = NonUniqueUnorderedIndex<TypeGroupEdge, std::nullptr_t>;

    EdgeType() = delete;

    EdgeType(const std::string &id);
    EdgeType(const char *id);
    EdgeType(std::string &&id);

    EdgeType(const EdgeType &) = delete;
    EdgeType(EdgeType &&other) = default;

    friend bool operator<(const EdgeType &lhs, const EdgeType &rhs);

    friend bool operator==(const EdgeType &lhs, const EdgeType &rhs);

    friend std::ostream &operator<<(std::ostream &stream, const EdgeType &type);

    operator const std::string &() const;

    std::string const &str() const { return id; }

    CharStr char_str() { return CharStr(&id[0]); }

    type_index_t &index() const;

private:
    std::string id;
    std::unique_ptr<type_index_t> index_v;
};

using edge_type_ref_t = ReferenceWrapper<const EdgeType>;
