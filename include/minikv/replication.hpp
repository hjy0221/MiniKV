#pragma once
#include "minikv/engine.hpp"
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace minikv {
class ReplicatedStore {
 public:
  ReplicatedStore(std::shared_ptr<StorageEngine> primary,
                  std::vector<std::shared_ptr<StorageEngine>> replicas);
  ~ReplicatedStore();
  void put(const std::string& key, const std::string& value);
  bool erase(const std::string& key);
  std::optional<std::string> get(const std::string& key);
  void wait_for_replication();

 private:
  void run();
  std::shared_ptr<StorageEngine> primary_;
  std::vector<std::shared_ptr<StorageEngine>> replicas_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::condition_variable drained_;
  std::deque<Record> queue_;
  bool stopping_ = false;
  bool applying_ = false;
  std::thread worker_;
};
}  // namespace minikv

