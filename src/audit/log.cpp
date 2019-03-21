#include "audit/log.hpp"

#include <chrono>

#include <fmt/format.h>
#include <glog/logging.h>
#include <json/json.hpp>

#include "utils/string.hpp"

namespace audit {

// Helper function that converts a `PropertyValue` to `nlohmann::json`.
inline nlohmann::json PropertyValueToJson(const PropertyValue &pv) {
  nlohmann::json ret;
  switch (pv.type()) {
    case PropertyValue::Type::Null:
      break;
    case PropertyValue::Type::Bool:
      ret = pv.Value<bool>();
      break;
    case PropertyValue::Type::Int:
      ret = pv.Value<int64_t>();
      break;
    case PropertyValue::Type::Double:
      ret = pv.Value<double>();
      break;
    case PropertyValue::Type::String:
      ret = pv.Value<std::string>();
      break;
    case PropertyValue::Type::List: {
      ret = nlohmann::json::array();
      for (const auto &item : pv.Value<std::vector<PropertyValue>>()) {
        ret.push_back(PropertyValueToJson(item));
      }
      break;
    }
    case PropertyValue::Type::Map: {
      ret = nlohmann::json::object();
      for (const auto &item :
           pv.Value<std::map<std::string, PropertyValue>>()) {
        ret.push_back(nlohmann::json::object_t::value_type(
            item.first, PropertyValueToJson(item.second)));
      }
      break;
    }
  }
  return ret;
}

Log::Log(const std::experimental::filesystem::path &storage_directory,
         int32_t buffer_size, int32_t buffer_flush_interval_millis)
    : storage_directory_(storage_directory),
      buffer_size_(buffer_size),
      buffer_flush_interval_millis_(buffer_flush_interval_millis),
      started_(false) {}

void Log::Start() {
  CHECK(!started_) << "Trying to start an already started audit log!";

  utils::EnsureDirOrDie(storage_directory_);

  buffer_.emplace(buffer_size_);
  started_ = true;

  ReopenLog();
  scheduler_.Run("Audit",
                 std::chrono::milliseconds(buffer_flush_interval_millis_),
                 [&] { Flush(); });
}

Log::~Log() {
  if (!started_) return;

  started_ = false;
  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  scheduler_.Stop();
  Flush();
}

void Log::Record(const std::string &address, const std::string &username,
                 const std::string &query, const PropertyValue &params) {
  if (!started_.load(std::memory_order_relaxed)) return;
  auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  buffer_->emplace(Item{timestamp, address, username, query, params});
}

void Log::ReopenLog() {
  if (!started_.load(std::memory_order_relaxed)) return;
  std::lock_guard<std::mutex> guard(lock_);
  if (log_.IsOpen()) log_.Close();
  log_.Open(storage_directory_ / "audit.log");
}

void Log::Flush() {
  std::lock_guard<std::mutex> guard(lock_);
  for (uint64_t i = 0; i < buffer_size_; ++i) {
    auto item = buffer_->pop();
    if (!item) break;
    log_.Write(
        fmt::format("{}.{:06d},{},{},{},{}\n", item->timestamp / 1000000,
                    item->timestamp % 1000000, item->address, item->username,
                    utils::Escape(item->query),
                    utils::Escape(PropertyValueToJson(item->params).dump())));
  }
  log_.Sync();
}

}  // namespace audit