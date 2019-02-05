#include "query/plan/rewrite/index_lookup.hpp"

#include "utils/flag_validation.hpp"

DEFINE_VALIDATED_HIDDEN_int64(
    query_vertex_count_to_expand_existing, 10,
    "Maximum count of indexed vertices which provoke "
    "indexed lookup and then expand to existing, instead of "
    "a regular expand. Default is 10, to turn off use -1.",
    FLAG_IN_RANGE(-1, std::numeric_limits<std::int64_t>::max()));

namespace query::plan::impl {

Expression *RemoveAndExpressions(
    Expression *expr, const std::unordered_set<Expression *> &exprs_to_remove) {
  auto *and_op = utils::Downcast<AndOperator>(expr);
  if (!and_op) return expr;
  if (utils::Contains(exprs_to_remove, and_op)) {
    return nullptr;
  }
  if (utils::Contains(exprs_to_remove, and_op->expression1_)) {
    and_op->expression1_ = nullptr;
  }
  if (utils::Contains(exprs_to_remove, and_op->expression2_)) {
    and_op->expression2_ = nullptr;
  }
  and_op->expression1_ =
      RemoveAndExpressions(and_op->expression1_, exprs_to_remove);
  and_op->expression2_ =
      RemoveAndExpressions(and_op->expression2_, exprs_to_remove);
  if (!and_op->expression1_ && !and_op->expression2_) {
    return nullptr;
  }
  if (and_op->expression1_ && !and_op->expression2_) {
    return and_op->expression1_;
  }
  if (and_op->expression2_ && !and_op->expression1_) {
    return and_op->expression2_;
  }
  return and_op;
}

}  // namespace query::plan::impl
