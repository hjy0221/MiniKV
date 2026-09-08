# MiniKV 설계 문서

## 데이터 처리 흐름

```text
PUT / DELETE
     │
     ▼
엔진 배타 잠금 획득
     │
     ▼
체크섬 레코드를 WAL에 추가 ──► 선택적 flush
     │
     ▼
해시 인덱스와 LRU 캐시 갱신
     │
     └──► 불변 레코드 큐 ──► 복제 워커 ──► 복제본 WAL/인덱스
```

`GET`은 먼저 크기가 제한된 LRU 캐시를 확인하고, 없으면 메모리 해시 인덱스를
조회한다. 엔진과 캐시 잠금은 언제나 같은 순서로 획득하므로 오래된 값 반환과
잠금 순서 역전 문제를 방지한다.

## 로그 형식

각 레코드는 독립적으로 해석 가능한 append-only 형식이다.

| 필드 | 크기 | 의미 |
|---|---:|---|
| magic | 4바이트 | `MKV1` 식별자 |
| operation | 1바이트 | PUT=1, DELETE=2 |
| reserved | 3바이트 | 향후 플래그와 버전 확장 영역 |
| sequence | 8바이트 | 단조 증가 연산 번호 |
| key length | 4바이트 | 키의 바이트 수 |
| value length | 4바이트 | 값의 바이트 수 |
| checksum | 4바이트 | 헤더 일부와 데이터에 대한 FNV-1a |
| key + value | 가변 | 문자열 원시 바이트 |

시작할 때 유효한 레코드를 순서대로 재생한다. 마지막 레코드가 불완전하거나
손상되었다면 비정상 종료 중 발생한 부분 쓰기로 간주하고 무시한다. 따라서 마지막
정상 상태를 보존한다. 삭제 연산은 로그에 tombstone으로 남고 복구 과정에서 해당
키를 인덱스에서 제거한다.

## 일관성과 내구성

- 인덱스를 변경하기 전에 레코드를 WAL에 기록한다.
- `sync_on_write=false`는 처리량을 우선하며 `flush()`가 명시적 내구성 경계가 된다.
- `sync_on_write=true`는 매 연산마다 C++ 스트림을 flush한다. 실제 운영용 엔진은
  OS별 `fsync` 또는 `FlushFileBuffers`도 호출해야 한다.
- 한 프로세스 안에서는 shared mutex로 쓰기를 직렬화한다. 여러 프로세스가 동시에
  같은 로그를 여는 방식은 지원하지 않는다.
- 복제는 연산 순서를 보장하는 비동기 방식이며 `wait_for_replication()`이 확인 장벽이다.
- 복제본은 sequence 번호를 사용해 동일 레코드의 중복 적용을 무시한다.

## 버전별 발전 과정

1. 해시 맵 기반 메모리 저장소 (`index_`)
2. 파일 기반 append-only 레코드
3. write-ahead 순서와 체크섬
4. 시작 시 로그 재생 및 잘린 마지막 레코드 복구
5. O(1) 연산의 용량 제한 LRU 캐시
6. thread-safe 연산과 멀티스레드 벤치마크
7. primary에서 여러 replica로 보내는 순서 보장 비동기 복제

## 의도적으로 남긴 다음 단계

- 덮어쓴 값과 tombstone을 회수하는 로그 compaction/SSTable
- `fsync` group commit과 batch write
- 원자적 통계 카운터를 사용한 진정한 병렬 읽기 경로
- TCP 전송, replica ACK, quorum commit, snapshot, Raft 리더 선출
- 인덱스가 메모리를 초과할 때 사용할 Bloom filter와 block cache

