"""Phase 3 성능 측정: 서버에 HTTP로 검색을 보내 시간을 잰다.

레코드가 많은 데이터 폴더로 서버를 켠 뒤(예: 벤치마크가 만든 /data/bench) 윈도우에서:
    python tests/server/bench_server.py [레코드 수, 기본 1000000]

재는 것
1. id 검색(B+ 트리)과 name 검색(선형 탐색)의 응답 시간. 서버는 인덱스를 켤 때 한 번만 만든다.
2. 무거운 검색(선형 탐색) 8개를 하나씩 보낼 때와 동시에 보낼 때. 동시에 빨라지면 스레드 풀이 진짜 병렬로 돈 것이다.
"""

import concurrent.futures
import random
import sys
import time

from dbms_client import query, send_request


def timed_query(sql):
    """SQL 하나를 보내고 걸린 시간(ms)을 돌려준다. 실패하면 멈춘다."""
    start = time.perf_counter()
    status, body = query(sql)
    elapsed = (time.perf_counter() - start) * 1000
    assert status == 200, body
    return elapsed


def average_ms(sqls):
    """SQL들을 하나씩 보내고 평균 응답 시간(ms)을 돌려준다."""
    return sum(timed_query(sql) for sql in sqls) / len(sqls)


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 1_000_000
    assert send_request("GET", "/health")[0] == 200, "서버가 켜져 있지 않다"

    # 첫 요청에서 테이블을 열며 인덱스를 만든다. 측정에서 빼려고 먼저 한 번 보낸다.
    first = timed_query("SELECT id FROM users WHERE id = 1;")
    print(f"첫 요청 (테이블 열기 + 인덱스 만들기 포함): {first:.1f} ms")

    random.seed(1)
    id_sqls = [f"SELECT * FROM users WHERE id = {random.randint(1, count)};" for _ in range(500)]
    name_sqls = [f"SELECT * FROM users WHERE name = 'user{random.randint(1, count)}';" for _ in range(10)]

    id_ms = average_ms(id_sqls)
    name_ms = average_ms(name_sqls)
    print(f"id 검색   (B+ 트리)   평균 {id_ms:8.2f} ms  ({len(id_sqls)}번)")
    print(f"name 검색 (선형 탐색) 평균 {name_ms:8.2f} ms  ({len(name_sqls)}번)")
    print(f"→ 약 {name_ms / id_ms:.0f}배 (HTTP 왕복 시간 포함)")

    heavy = name_sqls[:8]
    start = time.perf_counter()
    for sql in heavy:
        timed_query(sql)
    one_by_one = (time.perf_counter() - start) * 1000

    start = time.perf_counter()
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(heavy)) as executor:
        list(executor.map(timed_query, heavy))
    together = (time.perf_counter() - start) * 1000

    print(f"선형 탐색 {len(heavy)}개: 하나씩 {one_by_one:.0f} ms, 동시에 {together:.0f} ms "
          f"→ {one_by_one / together:.1f}배 빠름 (SELECT끼리는 읽기 열쇠를 같이 잡는다)")


if __name__ == "__main__":
    main()
