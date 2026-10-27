# MiniKV

MiniKV는 외부 런타임 의존성이 없는 C++17 미니 스토리지 엔진이다. 메모리 기반
key-value map에서 출발해 파일 영속성, 비정상 종료 복구, 캐시, 동시성, 비동기
복제까지 단계적으로 구현했다.

## 주요 기능

- `PUT`, `GET`, `DELETE` 대화형 CLI
- append-only 바이너리 로그가 뒷받침하는 메모리 해시 인덱스
- 체크섬이 포함된 write-ahead 레코드와 단조 증가 sequence 번호
- 불완전한 마지막 레코드를 안전하게 무시하는 crash recovery
- hit/miss 통계를 제공하는 크기 제한 O(1) LRU 캐시
- thread-safe 접근과 재현 가능한 1/2/4/8 스레드 벤치마크
- 하나 이상의 영속 replica로 보내는 순서 보장 비동기 복제
- 서드파티 라이브러리 없는 구현

로그 형식과 내구성 모델, 잠금 및 트레이드오프는
[`docs/DESIGN.md`](docs/DESIGN.md)에 정리했다.

## 빌드와 테스트

Windows PowerShell과 GCC/MSYS2 환경:

```powershell
.\build.ps1
.\build\minikv_tests.exe
.\build\minikv.exe data\store.log
```

Linux/macOS GNU Make 환경:

```bash
make
make test
./build/minikv data/store.log
```

CMake도 지원한다.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

실행 예시:

```text
> PUT language C++
OK
> GET language
C++
> DELETE language
1
> STATS
keys=0 seq=2 puts=1 gets=1 deletes=1 cache_hits=1 cache_misses=0 recovered=0
```

## 벤치마크

벤치마크는 10,000개 키의 working set에 쓰기 60%, 읽기 30%, 삭제 10%의
결정적 workload를 실행한다. 결과는 CSV 형식으로 출력한다.

```powershell
.\build\minikv_bench.exe 200000
```

측정값은 컴퓨터, 파일시스템, 컴파일러와 백그라운드 부하에 따라 달라진다. 핵심은
숫자 자체가 아니라 확장 곡선이다. 현재 구현은 변경과 통계 갱신을 하나의 엔진
잠금으로 직렬화하므로 스레드가 증가하면 lock contention 때문에 처리량이 정체될
것으로 예상된다. 이를 실제로 측정한 뒤 인덱스/WAL sharding 또는 읽기 통계 경로
분리를 다음 최적화 실험으로 삼을 수 있다.

실행 환경에서 측정한 원본 결과는 [`benchmark-results.csv`](benchmark-results.csv)에
저장한다.

### 현재 측정 결과

2026-09-08, GCC `-O2`, 200,000개 연산으로 측정한 결과다.

| 스레드 | 처리량(ops/s) | 1스레드 대비 |
|---:|---:|---:|
| 1 | 423,168 | 1.00x |
| 2 | 212,513 | 0.50x |
| 4 | 210,552 | 0.50x |
| 8 | 143,273 | 0.34x |

스레드 수를 늘려도 처리량이 오르지 않고 오히려 감소했다. 파일 append와 메모리
인덱스 변경이 하나의 배타 잠금 안에 있고, 읽기도 통계와 LRU 순서 갱신 때문에 같은
잠금을 사용하기 때문이다. 스레드가 늘수록 실제 작업보다 lock 대기와 context switch
비용이 커진다. 다음 개선 실험은 읽기 통계를 atomic으로 분리한 뒤 read path를 공유
잠금으로 바꾸는 것이며, 그 다음 단계는 WAL batching과 index sharding이다.

## 프로젝트 구조

```text
include/minikv/        공개 엔진, 캐시, 복제 API
src/                   엔진 구현과 CLI
tests/                 영속성, 부분 쓰기 복구, 동시성, 복제 테스트
benchmarks/            재현 가능한 혼합 workload 벤치마크
docs/                  설계와 트레이드오프 문서
```

## 범위와 한계

복제 계층은 한 프로세스 안에서 연산 순서, fan-out, replica 영속성과 확인 장벽을
보여주기 위한 구현이다. 합의 프로토콜은 아니다. 코어 레코드 및 적용 모델을 바꾸지
않고 TCP 전송과 Raft quorum을 추가할 수 있도록 경계를 분리했다.

---

## English

MiniKV is a C++17 mini storage engine with no external runtime dependencies. It grows from an in-memory key-value map to include file persistence, crash recovery, caching, concurrency, and asynchronous replication.

### Features

- Interactive `PUT`, `GET`, and `DELETE` CLI; in-memory hash index backed by an append-only binary log.
- Checksummed write-ahead records with monotonically increasing sequence numbers and recovery that safely ignores an incomplete final record.
- Bounded O(1) LRU cache with hit/miss statistics, thread-safe access, and reproducible 1/2/4/8-thread benchmarks.
- Ordered asynchronous replication to one or more persistent replicas, without third-party libraries.

See [the design document](docs/DESIGN.md) for the log format, durability model, locking, and tradeoffs.

### Build and test

On Windows with PowerShell and GCC/MSYS2, run `build.ps1`, `build/minikv_tests.exe`, and `build/minikv.exe data/store.log`. On Linux/macOS, run `make`, `make test`, and `./build/minikv data/store.log`. CMake is also supported: `cmake -S . -B build`, `cmake --build build`, then `ctest --test-dir build`. The Korean section above contains complete commands and a CLI example.

### Benchmark

The deterministic workload uses a 10,000-key working set with 60% writes, 30% reads, and 10% deletes, producing CSV output. Run `build/minikv_bench.exe 200000` on Windows. Measurements depend on the machine, filesystem, compiler, and background load; compare scaling rather than the absolute numbers. Raw results are stored in [benchmark-results.csv](benchmark-results.csv).

On 2026-09-08, GCC `-O2`, 200,000 operations yielded 423,168, 212,513, 210,552, and 143,273 ops/s for 1, 2, 4, and 8 threads respectively (1.00x, 0.50x, 0.50x, 0.34x). Throughput declined because file append, index changes, statistics, and LRU updates share an exclusive engine lock. Potential experiments include atomic read statistics and a shared read lock, followed by WAL batching and index sharding.

### Structure and limitations

`include/minikv/` exposes engine, cache, and replication APIs; `src/` contains the implementation and CLI; `tests/` covers persistence, partial-write recovery, concurrency, and replication; `benchmarks/` contains the mixed workload; and `docs/` describes design tradeoffs. Replication demonstrates ordering, fan-out, replica durability, and acknowledgment barriers within one process; it is not a consensus protocol. The boundaries allow future TCP transport and Raft quorum support without changing the core record and apply model.
