#include "minikv/engine.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
  const int operations = argc > 1 ? std::stoi(argv[1]) : 100000;
  std::cout << "threads,operations,seconds,ops_per_second\n";
  for (int count : {1, 2, 4, 8}) {
    auto path = std::filesystem::temp_directory_path() / ("minikv-bench-" + std::to_string(count) + ".log");
    std::filesystem::remove(path);
    {
    minikv::StorageEngine db(path.string(), 4096, false);
    std::atomic<int> next{0};
    auto start = std::chrono::steady_clock::now();
    std::vector<std::thread> workers;
    for (int t = 0; t < count; ++t) workers.emplace_back([&] {
      while (true) {
        int i = next.fetch_add(1, std::memory_order_relaxed); if (i >= operations) break;
        std::string key = "key-" + std::to_string(i % 10000);
        if (i % 10 < 6) db.put(key, "value-" + std::to_string(i));
        else if (i % 10 < 9) (void)db.get(key);
        else (void)db.erase(key);
      }
    });
    for (auto& worker : workers) worker.join();
    db.flush();
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << count << ',' << operations << ',' << std::fixed << std::setprecision(4) << seconds
              << ',' << static_cast<long long>(operations / seconds) << '\n';
    }
    std::filesystem::remove(path);
  }
}
