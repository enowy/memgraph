#include "memgraph_init.hpp"

#include <glog/logging.h>

#include "config.hpp"
#include "glue/auth.hpp"
#include "glue/communication.hpp"
#include "query/exceptions.hpp"
#include "requests/requests.hpp"
#include "stats/stats.hpp"
#include "utils/signals.hpp"
#include "utils/sysinfo/memory.hpp"
#include "utils/terminate_handler.hpp"
#include "version.hpp"

DEFINE_string(log_file, "", "Path to where the log should be stored.");
DEFINE_HIDDEN_string(
    log_link_basename, "",
    "Basename used for symlink creation to the last log file.");
DEFINE_uint64(memory_warning_threshold, 1024,
              "Memory warning threshold, in MB. If Memgraph detects there is "
              "less available RAM it will log a warning. Set to 0 to "
              "disable.");

BoltSession::BoltSession(SessionData *data, const io::network::Endpoint &,
                         communication::InputStream *input_stream,
                         communication::OutputStream *output_stream)
    : communication::bolt::Session<communication::InputStream,
                                   communication::OutputStream>(input_stream,
                                                                output_stream),
      transaction_engine_(data->db, data->interpreter),
      auth_(&data->auth) {}

using TEncoder =
    communication::bolt::Session<communication::InputStream,
                                 communication::OutputStream>::TEncoder;

std::vector<std::string> BoltSession::Interpret(
    const std::string &query,
    const std::map<std::string, communication::bolt::Value> &params) {
  std::map<std::string, PropertyValue> params_pv;
  for (const auto &kv : params)
    params_pv.emplace(kv.first, glue::ToPropertyValue(kv.second));
  try {
    auto result = transaction_engine_.Interpret(query, params_pv);
    if (user_) {
      const auto &permissions = user_->GetPermissions();
      for (const auto &privilege : result.second) {
        if (permissions.Has(glue::PrivilegeToPermission(privilege)) !=
            auth::PermissionLevel::GRANT) {
          transaction_engine_.Abort();
          throw communication::bolt::ClientError(
              "You are not authorized to execute this query! Please contact "
              "your database administrator.");
        }
      }
    }
    return result.first;

  } catch (const query::QueryException &e) {
    // Wrap QueryException into ClientError, because we want to allow the
    // client to fix their query.
    throw communication::bolt::ClientError(e.what());
  }
}

std::map<std::string, communication::bolt::Value> BoltSession::PullAll(
    TEncoder *encoder) {
  try {
    TypedValueResultStream stream(encoder);
    const auto &summary = transaction_engine_.PullAll(&stream);
    std::map<std::string, communication::bolt::Value> decoded_summary;
    for (const auto &kv : summary) {
      decoded_summary.emplace(kv.first, glue::ToBoltValue(kv.second));
    }
    return decoded_summary;
  } catch (const query::QueryException &e) {
    // Wrap QueryException into ClientError, because we want to allow the
    // client to fix their query.
    throw communication::bolt::ClientError(e.what());
  }
}

void BoltSession::Abort() { transaction_engine_.Abort(); }

bool BoltSession::Authenticate(const std::string &username,
                               const std::string &password) {
  if (!auth_->HasUsers()) return true;
  user_ = auth_->Authenticate(username, password);
  return !!user_;
}

BoltSession::TypedValueResultStream::TypedValueResultStream(TEncoder *encoder)
    : encoder_(encoder) {}

void BoltSession::TypedValueResultStream::Result(
    const std::vector<query::TypedValue> &values) {
  std::vector<communication::bolt::Value> decoded_values;
  decoded_values.reserve(values.size());
  for (const auto &v : values) {
    decoded_values.push_back(glue::ToBoltValue(v));
  }
  encoder_->MessageRecord(decoded_values);
}

void KafkaStreamWriter(
    SessionData &session_data, const std::string &query,
    const std::map<std::string, communication::bolt::Value> &params) {
  auto dba = session_data.db->Access();
  KafkaResultStream stream;
  std::map<std::string, PropertyValue> params_pv;
  for (const auto &kv : params)
    params_pv.emplace(kv.first, glue::ToPropertyValue(kv.second));
  try {
    (*session_data.interpreter)(query, *dba, params_pv, false).PullAll(stream);
    dba->Commit();
  } catch (const query::QueryException &e) {
    LOG(WARNING) << "[Kafka] query execution failed with an exception: "
                 << e.what();
    dba->Abort();
  }
};

// Needed to correctly handle memgraph destruction from a signal handler.
// Without having some sort of a flag, it is possible that a signal is handled
// when we are exiting main, inside destructors of database::GraphDb and
// similar. The signal handler may then initiate another shutdown on memgraph
// which is in half destructed state, causing invalid memory access and crash.
volatile sig_atomic_t is_shutting_down = 0;

void InitSignalHandlers(const std::function<void()> &shutdown_fun) {
  // Prevent handling shutdown inside a shutdown. For example, SIGINT handler
  // being interrupted by SIGTERM before is_shutting_down is set, thus causing
  // double shutdown.
  sigset_t block_shutdown_signals;
  sigemptyset(&block_shutdown_signals);
  sigaddset(&block_shutdown_signals, SIGTERM);
  sigaddset(&block_shutdown_signals, SIGINT);

  // Wrap the shutdown function in a safe way to prevent recursive shutdown.
  auto shutdown = [shutdown_fun]() {
    if (is_shutting_down) return;
    is_shutting_down = 1;
    shutdown_fun();
  };

  CHECK(utils::SignalHandler::RegisterHandler(utils::Signal::Terminate,
                                              shutdown, block_shutdown_signals))
      << "Unable to register SIGTERM handler!";
  CHECK(utils::SignalHandler::RegisterHandler(utils::Signal::Interupt, shutdown,
                                              block_shutdown_signals))
      << "Unable to register SIGINT handler!";

  // Setup SIGUSR1 to be used for reopening log files, when e.g. logrotate
  // rotates our logs.
  CHECK(utils::SignalHandler::RegisterHandler(utils::Signal::User1, []() {
    google::CloseLogDestination(google::INFO);
  })) << "Unable to register SIGUSR1 handler!";
}

int WithInit(int argc, char **argv,
             const std::function<std::string()> &get_stats_prefix,
             const std::function<void()> &memgraph_main) {
  gflags::SetVersionString(version_string);

  // Load config before parsing arguments, so that flags from the command line
  // overwrite the config.
  LoadConfig();
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  google::InitGoogleLogging(argv[0]);
  google::SetLogDestination(google::INFO, FLAGS_log_file.c_str());
  google::SetLogSymlink(google::INFO, FLAGS_log_link_basename.c_str());

  // Unhandled exception handler init.
  std::set_terminate(&utils::TerminateHandler);

  stats::InitStatsLogging(get_stats_prefix());
  utils::OnScopeExit stop_stats([] { stats::StopStatsLogging(); });

  // Initialize the communication library.
  communication::Init();

  // Start memory warning logger.
  utils::Scheduler mem_log_scheduler;
  if (FLAGS_memory_warning_threshold > 0) {
    auto free_ram = utils::sysinfo::AvailableMemoryKilobytes();
    if (free_ram) {
      mem_log_scheduler.Run("Memory warning", std::chrono::seconds(3), [] {
        auto free_ram = utils::sysinfo::AvailableMemoryKilobytes();
        if (free_ram && *free_ram / 1024 < FLAGS_memory_warning_threshold)
          LOG(WARNING) << "Running out of available RAM, only "
                       << *free_ram / 1024 << " MB left.";
      });
    } else {
      // Kernel version for the `MemAvailable` value is from: man procfs
      LOG(WARNING) << "You have an older kernel version (<3.14) or the /proc "
                      "filesystem isn't available so remaining memory warnings "
                      "won't be available.";
    }
  }
  requests::Init();

  memgraph_main();
  return 0;
}