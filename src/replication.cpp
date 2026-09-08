#include "minikv/replication.hpp"

namespace minikv {
ReplicatedStore::ReplicatedStore(std::shared_ptr<StorageEngine> primary,
    std::vector<std::shared_ptr<StorageEngine>> replicas)
    : primary_(std::move(primary)), replicas_(std::move(replicas)), worker_(&ReplicatedStore::run, this) {}

ReplicatedStore::~ReplicatedStore() {
  wait_for_replication();
  { std::lock_guard<std::mutex> lock(mu_); stopping_ = true; }
  cv_.notify_one();
  worker_.join();
}

void ReplicatedStore::put(const std::string& key, const std::string& value) {
  primary_->put(key, value);
  Record r{Operation::Put, key, value, primary_->sequence()};
  { std::lock_guard<std::mutex> lock(mu_); queue_.push_back(std::move(r)); }
  cv_.notify_one();
}

bool ReplicatedStore::erase(const std::string& key) {
  if (!primary_->erase(key)) return false;
  Record r{Operation::Delete, key, {}, primary_->sequence()};
  { std::lock_guard<std::mutex> lock(mu_); queue_.push_back(std::move(r)); }
  cv_.notify_one();
  return true;
}

std::optional<std::string> ReplicatedStore::get(const std::string& key) { return primary_->get(key); }

void ReplicatedStore::wait_for_replication() {
  std::unique_lock<std::mutex> lock(mu_);
  drained_.wait(lock, [this] { return queue_.empty() && !applying_; });
}

void ReplicatedStore::run() {
  while (true) {
    Record record{};
    {
      std::unique_lock<std::mutex> lock(mu_);
      cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (stopping_ && queue_.empty()) return;
      record = std::move(queue_.front()); queue_.pop_front(); applying_ = true;
    }
    for (auto& replica : replicas_) replica->apply(record);
    {
      std::lock_guard<std::mutex> lock(mu_); applying_ = false;
      if (queue_.empty()) drained_.notify_all();
    }
  }
}
}  // namespace minikv

