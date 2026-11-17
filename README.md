# mini-DBMS

Krafton Jungle SW-AI Lab Week11~12 mini-DBMS project

C++17로 만든 작은 DBMS다. SQL을 읽어 파일에 저장하고(Phase 1), B+ 트리 인덱스로 빠르게 찾고(Phase 2), 여러 사람이 동시에 쓸 수 있는 HTTP API 서버로 감쌌다(Phase 3).

```
Phase 1  SQL 처리기       sqlproc query.sql → INSERT / SELECT → 텍스트 파일
Phase 2  B+ 트리 인덱스    WHERE id = ? 를 트리로 찾는다. 100만 건에서 선형 탐색보다 약 1만 배 빠름
Phase 3  API 서버         POST /query 로 SQL을 보내면 JSON으로 답한다. 직접 만든 스레드 풀이 병렬 처리
```

자세한 동작 명세는 [docs/SPEC.md](docs/SPEC.md)에 있다.

---

## 1. 기초 개념

### DBMS

데이터를 저장하고 꺼내 주는 프로그램이다(DataBase Management System). SQLite, MySQL이 여기에 들어간다. 이 프로젝트는 그중 SQL을 쓰는 종류를 아주 작게 만든 것이다.

### SQL

DB에 "무엇을" 원하는지 말하는 언어다. 어떻게 찾을지는 DBMS가 정한다.

```sql
INSERT INTO users VALUES ('Minsu', 30);     -- 표에 한 줄 넣기
SELECT * FROM users WHERE id = 1;           -- 조건에 맞는 줄 꺼내기
```

데이터는 **표(테이블)** 로 저장한다. 엑셀 시트처럼 줄(레코드)과 칸(컬럼)으로 이루어진다.

```
users 표
| id | name  | age |
|----|-------|-----|
| 1  | Minsu | 30  |
```

### SQL 처리기

SQL은 그냥 글자다. 이 글자를 알아듣고 실행하는 프로그램이 SQL 처리기다. 파이썬 코드를 `python.exe`가 읽고 실행하는 것과 같다.

```
"SELECT * FROM users WHERE id = 1;"
   ↓ 토큰화: 글자를 단어 조각으로      [SELECT] [*] [FROM] [users] [WHERE] [id] [=] [1] [;]
   ↓ 파싱:   조각을 틀에 맞춰 정리     명령=SELECT, 표=users, 조건=id=1
   ↓ 실행:   파일을 읽어 조건에 맞는 줄 고르기
결과
```

토큰화는 "어디까지가 한 단어인지"(띄어쓰기, 따옴표)를, 파싱은 "단어 순서가 문법에 맞는지"를 본다. 맡는 일이 달라서 나눴다.

### 스키마

표의 설계도다. 어떤 컬럼이 어떤 순서로 있고 각각 숫자인지 글자인지 적는다. 과제가 CREATE TABLE(표 만들기)을 요구하지 않아서, 스키마 파일을 손으로 미리 만들어 둔다.

```
data/users.schema
id INT
name TEXT
age INT
```

### 인덱스와 B+ 트리

**인덱스**는 책 뒤 색인처럼 전부 뒤지지 않고 데이터 위치를 바로 알려 주는 구조다. 인덱스가 없으면 100만 줄을 처음부터 다 읽어야 한다.

**B+ 트리**는 가장 많이 쓰는 인덱스다. 정렬된 키를 층으로 나눠 담는다.

```
                    [ 10 | 20 ]                    ← 이정표: "10 미만은 왼쪽, 10~20은 가운데, 20 이상은 오른쪽"
                  /      |      \
       [ 3 | 7 ]     [ 12 | 15 ]     [ 25 | 30 ]   ← 잎: 키(id) + 데이터 파일에서의 위치
           →              →               →        ← 잎끼리 옆으로 연결
```

- 찾을 때: 층마다 노드 안에서 **이진 탐색**으로 자식 하나만 골라 내려간다. 노드 하나에 키가 최대 128개라서 100만 건도 4층이면 된다.
- 넣을 때: 들어갈 잎에 넣고, 넘치면 반으로 **쪼갠다**. 트리는 위로만 자라서 모든 잎의 깊이가 같다.
- 이 프로젝트의 트리는 **메모리에만** 있다(과제의 "메모리 기반 단순화"). 프로그램을 켤 때 데이터 파일을 읽어 다시 만든다.

### 스레드 풀

**스레드**는 프로그램 안의 일꾼이다. **스레드 풀**은 일꾼을 미리 정해진 수만큼 만들어 두고, 요청이 오면 놀고 있는 일꾼에게 맡기는 구조다.

```
요청 A, B, C, D, E → [작업 대기열] → 일꾼 1, 2, 3, 4 가 하나씩 꺼내 처리
                                    E는 누가 끝날 때까지 대기열에서 기다린다
```

요청마다 스레드를 새로 만들면 느리고, 요청이 몰리면 스레드가 끝없이 늘어 서버가 죽는다. 풀은 일꾼 수가 정해져 있어서 안전하다.

### 동시성 문제와 락

여러 일꾼이 같은 데이터를 동시에 건드리면 결과가 조용히 틀어진다.

```
INSERT A: 다음 id 읽음 → 5
INSERT B: 다음 id 읽음 → 5     ← 둘 다 5를 받아 id가 겹친다
```

**락**은 데이터를 쓰는 동안 다른 일꾼이 못 들어오게 거는 자물쇠다. 이 프로젝트는 테이블마다 **읽기-쓰기 락**을 건다.

```
SELECT ████          읽기 열쇠: 여럿이 같이 들어간다
SELECT ████
INSERT     ████      쓰기 열쇠: 혼자만. 읽는 사람이 다 나가야 받는다
SELECT         ████
```

### HTTP API

**API**는 프로그램끼리 주고받는 방법을 정한 약속이다. 이 서버는 웹 표준인 **HTTP**로 약속을 정했다. 그래서 파이썬, curl, Postman 같은 도구가 바로 쓸 수 있다.

```
클라이언트 ── POST /query  본문: SELECT * FROM users WHERE id = 1; ──→ 서버
클라이언트 ←── {"ok":true,"results":[{"rows":[[1,"Minsu",30]], ...}]} ── 서버
```

---

## 2. 구조

```
                    ┌────────── sqlproc (명령창) ──────────┐
SQL 파일 ──────────→│                                      │
                    │   runSql()  토큰화 → 파싱 → 실행       │──→ data/<표>.schema
HTTP 요청 ─→ httplib │      ↑                               │    data/<표>.db
       → 스레드 풀 ──┼──────┘   Database: 테이블, B+ 트리, 락  │    (텍스트 파일)
                    └────────── dbms_server (API 서버) ─────┘
```

- `sqlproc`과 `dbms_server`는 같은 엔진(`runSql()`)을 쓴다. Phase 1·2를 그대로 내부 엔진으로 쓴 것이다.
- HTTP 읽기·쓰기는 cpp-httplib가 하고, 스레드 풀과 작업 대기열은 직접 만들어 httplib의 스레드 풀 자리에 끼웠다.
- B+ 트리와 작업 대기열은 STL 그릇(`vector`, `queue` 등) 없이 직접 만들었다.

---

## 3. 시작하기

### 환경

팀 공용 Docker 컨테이너(Ubuntu 24.04, g++ 13.3, CMake, GoogleTest, cpp-httplib)에서 빌드한다. 윈도우에는 Docker Desktop만 있으면 된다.

1. Docker Desktop을 켠다.
2. VS Code에 Dev Containers 확장을 깔고, 이 폴더를 열어 **Reopen in Container**를 누른다.
3. VS Code 터미널이 컨테이너 안이다.

### 빌드와 테스트 (컨테이너 안)

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

기본 빌드는 메모리 실수를 잡는 AddressSanitizer가 켜져 있다. 속도를 잴 때는 `-DDBMS_SANITIZE=OFF`를 붙인다.

### sqlproc: SQL 파일 실행 (Phase 1, 2)

```bash
./build/sqlproc --data data query.sql
./build/sqlproc --data data --timing query.sql    # 문장마다 걸린 시간도 출력
```

```
INSERT OK (id=1)
id | name | age
1 | Minsu | 30
(1 rows)
```

### dbms_server: API 서버 (Phase 3)

컨테이너 안에서 켠다.

```bash
./build/dbms_server --data data --threads 8
```

윈도우 PowerShell에서 요청을 보낸다. PowerShell의 `curl`은 다른 명령의 별칭이라 `curl.exe`로 쓴다.

```powershell
curl.exe http://localhost:8080/health
curl.exe -X POST http://localhost:8080/query --data "INSERT INTO users VALUES ('Minsu', 30);"
curl.exe -X POST http://localhost:8080/query --data "SELECT * FROM users WHERE id = 1;"
```

끌 때는 서버 터미널에서 Ctrl+C. 처리 중인 요청을 마치고 종료한다.

---

## 4. 사용법 요약

### SQL

```
INSERT INTO 표 VALUES ( 값, 값, ... );            id는 자동으로 붙으니 적지 않는다
SELECT * FROM 표 [ WHERE 컬럼 = 값 ];
SELECT 컬럼, 컬럼 FROM 표 [ WHERE 컬럼 = 값 ];
```

- 키워드는 대소문자를 구분하지 않는다. 표·컬럼 이름은 구분한다.
- 글자는 작은따옴표로 감싼다. 안의 따옴표는 두 번 쓴다(`'it''s'`).
- `--` 뒤는 주석이다. 모든 문장은 `;`로 끝난다.
- 문장 하나가 틀려도 다음 문장은 실행한다.
- `WHERE id = 값`이면 B+ 트리를 쓰고, 다른 조건이면 파일 전체를 읽는다.

### 저장 형식

`data/<표>.db`에 레코드 하나가 한 줄이다. 칸은 쉼표로 나누고, 값 안의 `\` `,` 줄바꿈은 `\\` `\,` `\n`으로 적는다.

```
1,Minsu,30
2,Seoul\, Gangnam,25
```

텍스트 형식을 고른 이유: 줄 길이가 제각각이라 id만으로 위치를 계산할 수 없어서, B+ 트리가 실제로 필요해진다. B+ 트리는 `id → 그 줄이 시작하는 바이트`를 저장한다.

### API

| 요청 | 응답 |
|---|---|
| `GET /health` | `200 {"ok":true}` |
| `POST /query` (본문 = SQL) | 전부 성공 200, 실패한 문장이 있으면 400. 문장별 결과가 `results`에 |
| 없는 경로 | 404 |
| 본문 8MB 초과 | 413 |

```json
{"ok":true,"results":[
  {"line":1,"ok":true,"type":"insert","id":1},
  {"line":2,"ok":true,"type":"select","columns":["id","name","age"],
   "rows":[[1,"Minsu",30]],"rowCount":1,"usedIndex":true}
]}
```

---

## 5. 테스트와 성능

| 검사 | 결과 |
|---|---|
| 단위 테스트 89개 (GoogleTest) | AddressSanitizer, ThreadSanitizer 빌드 모두 통과 |
| sqlproc 기능 테스트 | SQL 파일 실행 결과를 기대값과 통째로 비교 |
| 서버 테스트 17개 (파이썬) | 기능, 에러, 동시 INSERT 800건의 id 중복 없음 |

100만 건에서 측정한 결과(자세한 표는 [SPEC 14절](docs/SPEC.md#14-성능-테스트-결과)):

| 검색 | 엔진 안 | 서버 경유 (윈도우 → 컨테이너) |
|---|---|---|
| `WHERE id = ?` (B+ 트리) | 약 0.01 ms | 약 10 ms |
| `WHERE name = ?` (선형 탐색) | 약 226 ms | 약 247 ms |

- 엔진 안에서는 약 1만 배 차이다. 서버 경유는 HTTP 왕복 시간이 대부분이라 24배로 줄어든다.
- 무거운 SELECT 8개를 동시에 보내면 하나씩 보낼 때보다 3.5배 빠르다. 읽기 열쇠를 같이 잡아 일꾼들이 실제로 동시에 일했다는 뜻이다.

테스트 실행:

```bash
bash tests/benchmark/run_benchmark.sh      # 컨테이너 안: 100만 건 넣고 인덱스·선형 비교
```

```powershell
python tests\server\test_server.py         # 윈도우: 서버를 켠 뒤 기능·동시성 테스트
python tests\server\bench_server.py        # 윈도우: 100만 건 서버 성능
```

---

## 6. 폴더 구조

```
src/
  main.cpp          sqlproc (명령창)
  server_main.cpp   dbms_server (스레드 풀을 httplib에 끼우기)
  http_api.*        GET /health, POST /query, JSON
  sql_runner.*      SQL 글자 → 토큰화·파싱·실행 (두 프로그램이 같이 씀)
  tokenizer.*       토큰화, 문장 나누기
  parser.*          파싱
  executor.*        INSERT·SELECT 실행, 인덱스/선형 분기
  database.*        테이블 열기, 인덱스 만들기, 다음 id, 락
  bplus_tree.*      B+ 트리
  thread_pool.*     작업 대기열, 스레드 풀
  schema.*          스키마 파일 읽기
  storage.*         레코드 한 줄 변환, 파일 읽기·쓰기
tests/              단위 테스트, 기능 테스트, 벤치마크, 서버 테스트
data/users.schema   예시 표
docs/SPEC.md        상세 사양
.devcontainer/      개발 환경 (Dockerfile, devcontainer.json)
```

---

## 7. 한계

- 지원하는 SQL은 INSERT, SELECT와 `WHERE 컬럼 = 값` 하나뿐이다.
- 인덱스는 id에만 있고 메모리에만 있다. sqlproc은 실행할 때마다 인덱스를 다시 만든다.
- 트랜잭션과 인증이 없다.
- 전체 목록은 [SPEC 17절](docs/SPEC.md#17-알려진-한계).
