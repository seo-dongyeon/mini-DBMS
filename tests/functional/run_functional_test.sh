#!/usr/bin/env bash
# 기능 테스트: 빈 임시 폴더에서 sqlproc을 돌리고 출력을 기대값과 비교한다.
# 사용: run_functional_test.sh <sqlproc 경로> <이 폴더 경로>
set -u

sqlproc="$1"
test_dir="$2"

work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT

cp "$test_dir"/*.schema "$work_dir"/

# 에러 메시지에 임시 폴더 이름이 섞이지 않게, 그 폴더 안에서 "--data ."로 실행한다.
cd "$work_dir"
"$sqlproc" --data . "$test_dir/basic.sql" > actual.txt 2>&1
echo "exit=$?" >> actual.txt

diff -u "$test_dir/expected.txt" actual.txt
