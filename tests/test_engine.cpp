#include "minikv/engine.hpp"
#include "minikv/replication.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#define CHECK(x) do { if (!(x)) throw std::runtime_error("CHECK failed: " #x); } while (0)

int main() {
  namespace fs = std::filesystem;
  auto dir = fs::temp_directory_path() / "minikv-tests";
  fs::remove_all(dir); fs::create_directories(dir);
  try {
    {
      minikv::StorageEngine db((dir / "basic.log").string(), 2);
      db.put("a", "one"); db.put("b", "two"); db.put("a", "updated");
      CHECK(db.get("a") == "updated"); CHECK(db.erase("b")); CHECK(!db.get("b"));
      CHECK(!db.erase("missing")); CHECK(db.size() == 1);
    }
    {
      minikv::StorageEngine db((dir / "basic.log").string());
      CHECK(db.get("a") == "updated"); CHECK(!db.get("b")); CHECK(db.stats().recovered_records == 4);
    }
    {
      auto path = dir / "torn.log";
      { minikv::StorageEngine db(path.string()); db.put("safe", "value"); db.flush(); }
      { std::ofstream out(path, std::ios::binary | std::ios::app); out << "partial"; }
      minikv::StorageEngine db(path.string()); CHECK(db.get("safe") == "value");
    }
    {
      minikv::StorageEngine db((dir / "threads.log").string());
      std::vector<std::thread> threads;
      for (int t = 0; t < 4; ++t) threads.emplace_back([&, t] {
        for (int i = 0; i < 250; ++i) db.put(std::to_string(t) + ":" + std::to_string(i), "v");
      });
      for (auto& t : threads) t.join();
      CHECK(db.size() == 1000);
    }
    {
      auto p = std::make_shared<minikv::StorageEngine>((dir / "primary.log").string());
      auto r1 = std::make_shared<minikv::StorageEngine>((dir / "replica1.log").string());
      auto r2 = std::make_shared<minikv::StorageEngine>((dir / "replica2.log").string());
      minikv::ReplicatedStore cluster(p, {r1, r2});
      cluster.put("replicated", "yes"); cluster.put("gone", "soon"); cluster.erase("gone");
      cluster.wait_for_replication();
      CHECK(r1->get("replicated") == "yes"); CHECK(r2->get("replicated") == "yes");
      CHECK(!r1->get("gone")); CHECK(p->sequence() == r1->sequence());
    }
    fs::remove_all(dir);
    std::cout << "all MiniKV tests passed\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
