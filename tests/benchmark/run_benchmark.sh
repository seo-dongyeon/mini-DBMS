#!/usr/bin/env bash
# Phase 2 성능 테스트.
# 레코드 N건을 SQL INSERT로 넣은 뒤, id 검색(B+ 트리)과 다른 컬럼 검색(선형 탐색)의 시간을 비교한다.
# 컨테이너 안에서 실행: bash tests/benchmark/run_benchmark.sh [건수, 기본 1000000]
set -euo pipefail

count="${1:-1000000}"
chunk=100000                 # SQL 파일 하나에 넣을 INSERT 수. 토큰화가 파일 전체를 메모리에 올려서 나눠 넣는다
repo="$(cd "$(dirname "$0")/../.." && pwd)"
build_dir="$repo/build/release"
bench_dir="/data/bench"      # 벤치마크는 빠른 Docker 볼륨(/data)에서 돌린다. /work는 윈도우 폴더라 느리다

# 두 시각(초) 사이를 소수점 한 자리로 계산한다.
elapsed() { awk -v a="$1" -v b="$2" 'BEGIN { printf "%.1f", b - a }'; }

echo "== 1. 속도 측정용 빌드 (메모리 검사 끔, 최적화 켬)"
cmake -S "$repo" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DDBMS_SANITIZE=OFF > /dev/null
cmake --build "$build_dir" --target sqlproc > /dev/null
sqlproc="$build_dir/sqlproc"

rm -rf "$bench_dir"
mkdir -p "$bench_dir"
cp "$repo/data/users.schema" "$bench_dir/"

echo "== 2. INSERT ${count}건 (SQL 파일 ${chunk}건씩 sqlproc에 넣음)"
start=$(date +%s.%N)
for ((from = 1; from <= count; from += chunk)); do
    to=$((from + chunk - 1))
    if ((to > count)); then to=$count; fi
    awk -v from="$from" -v to="$to" \
        'BEGIN { for (i = from; i <= to; i++) printf "INSERT INTO users VALUES (\x27user%d\x27, %d);\n", i, i % 100 }' \
        > "$bench_dir/insert.sql"
    "$sqlproc" --data "$bench_dir" "$bench_dir/insert.sql" > /dev/null
done
end=$(date +%s.%N)
echo "  걸린 시간: $(elapsed "$start" "$end")초 (파일마다 프로그램을 새로 켜서 인덱스를 다시 만드는 시간 포함)"
echo "  저장된 줄 수: $(wc -l < "$bench_dir/users.db")"
echo "  파일 크기: $(du -h "$bench_dir/users.db" | cut -f1)"

middle=$((count / 2))
cat > "$bench_dir/query.sql" <<EOF
SELECT * FROM users WHERE id = 1;
SELECT * FROM users WHERE id = $middle;
SELECT * FROM users WHERE id = $count;
SELECT * FROM users WHERE id = $((count + 1));
SELECT * FROM users WHERE name = 'user1';
SELECT * FROM users WHERE name = 'user$middle';
SELECT * FROM users WHERE name = 'user$count';
SELECT * FROM users WHERE name = 'nobody';
EOF

echo "== 3. 검색 시간 비교 (1~4번째 줄: id로 찾기 = B+ 트리, 5~8번째 줄: name으로 찾기 = 선형 탐색)"
"$sqlproc" --timing --data "$bench_dir" "$bench_dir/query.sql" 2>&1 >/dev/null
