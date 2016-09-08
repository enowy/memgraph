#include "transactions/transaction.hpp"

#include <chrono> // std::chrono::seconds

#include <thread> // std::this_thread::sleep_for

#include "transactions/engine.hpp"

namespace tx
{

Transaction::Transaction(const Id &id, const Snapshot<Id> &snapshot,
                         Engine &engine)
    : TransactionId(id, snapshot, engine)
{
}

// Returns copy of transaction_id
TransactionId Transaction::transaction_id()
{
    TransactionId const &t = *this;
    return t;
}

void Transaction::wait_for_active()
{
    while (snapshot.size() > 0) {
        auto id = snapshot.back();
        while (engine.clog.fetch_info(id).is_active()) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        snapshot.remove(id);
    }
}

void Transaction::take_lock(RecordLock &lock) { locks.take(&lock, id); }

void Transaction::commit() { engine.commit(*this); }

void Transaction::abort() { engine.abort(*this); }
}
