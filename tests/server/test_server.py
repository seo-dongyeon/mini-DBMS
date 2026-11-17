"""Phase 3 기능 테스트: 실행 중인 서버에 HTTP로 SQL을 보내 결과를 확인한다.

서버가 먼저 켜져 있어야 한다 (컨테이너 안에서 dbms_server). 그다음 윈도우에서:
    python tests/server/test_server.py
다른 주소면 DBMS_URL 환경 변수로 바꾼다. 테이블이 비어 있지 않아도 되게 짰다.
"""

import concurrent.futures
import unittest
import uuid

from dbms_client import query, send_request


def unique_name(prefix="user"):
    """다른 테스트나 이전 실행과 겹치지 않는 이름을 만든다."""
    return f"{prefix}_{uuid.uuid4().hex[:12]}"


def insert_user(name, age):
    """사용자 한 명을 넣고 받은 id를 돌려준다."""
    status, body = query(f"INSERT INTO users VALUES ('{name}', {age});")
    assert status == 200, body
    return body["results"][0]["id"]


class BasicApiTest(unittest.TestCase):
    def test_health(self):
        self.assertEqual(send_request("GET", "/health"), (200, {"ok": True}))

    def test_insert_then_select_by_id_uses_index(self):
        name = unique_name()
        new_id = insert_user(name, 25)

        status, body = query(f"SELECT * FROM users WHERE id = {new_id};")
        self.assertEqual(status, 200)
        result = body["results"][0]
        self.assertEqual(result["columns"], ["id", "name", "age"])
        self.assertEqual(result["rows"], [[new_id, name, 25]])   # INT는 숫자, TEXT는 문자열
        self.assertTrue(result["usedIndex"])

    def test_select_by_other_column_scans(self):
        name = unique_name()
        new_id = insert_user(name, 41)
        status, body = query(f"SELECT id FROM users WHERE name = '{name}';")
        self.assertEqual(status, 200)
        self.assertEqual(body["results"][0]["rows"], [[new_id]])
        self.assertFalse(body["results"][0]["usedIndex"])

    def test_missing_id_returns_no_rows(self):
        status, body = query("SELECT * FROM users WHERE id = -1;")
        self.assertEqual(status, 200)
        self.assertEqual(body["results"][0]["rowCount"], 0)

    def test_special_characters_round_trip(self):
        tricky = "쉼표, 역슬래시\\ 따옴표'' 세미콜론; \"큰따옴표\"\n둘째 줄"
        status, body = query(f"INSERT INTO users VALUES ('{tricky}', -7);")
        self.assertEqual(status, 200, body)
        new_id = body["results"][0]["id"]
        _, body = query(f"SELECT name, age FROM users WHERE id = {new_id};")
        expected = tricky.replace("''", "'")   # SQL에서 ''는 따옴표 하나
        self.assertEqual(body["results"][0]["rows"], [[expected, -7]])

    def test_several_statements_in_one_request(self):
        name = unique_name()
        sql = f"INSERT INTO users VALUES ('{name}', 1);\nSELECT id FROM users WHERE name = '{name}';"
        status, body = query(sql)
        self.assertEqual(status, 200)
        self.assertTrue(body["ok"])
        inserted, selected = body["results"]
        self.assertEqual(selected["rows"], [[inserted["id"]]])
        self.assertEqual([r["line"] for r in body["results"]], [1, 2])


class ErrorTest(unittest.TestCase):
    def test_partial_failure_is_400_with_per_statement_results(self):
        name = unique_name()
        sql = f"INSERT INTO users VALUES ('{name}', 1);\nSELECT * FROM nothing;\nSELECT nope FROM users;"
        status, body = query(sql)
        self.assertEqual(status, 400)
        self.assertFalse(body["ok"])
        first, second, third = body["results"]
        self.assertTrue(first["ok"])               # 실패한 문장이 있어도 앞 문장은 실행됐다
        self.assertFalse(second["ok"])
        self.assertIn("테이블이 없다", second["error"])
        self.assertEqual(second["line"], 2)
        self.assertIn("컬럼이 없다", third["error"])

    def test_wrong_value_count_and_type(self):
        _, body = query("INSERT INTO users VALUES (7, 'a', 1);")
        self.assertIn("id는 자동", body["results"][0]["error"])
        _, body = query("INSERT INTO users VALUES ('a', '스물');")
        self.assertIn("타입이 맞지 않는다", body["results"][0]["error"])

    def test_syntax_error(self):
        status, body = query("SELECT * users;")
        self.assertEqual(status, 400)
        self.assertIn("FROM", body["results"][0]["error"])

    def test_unclosed_quote(self):
        status, body = query("INSERT INTO users VALUES ('안 닫힘, 1);")
        self.assertEqual(status, 400)
        self.assertIn("따옴표", body["results"][-1]["error"])

    def test_empty_body(self):
        for sql in ["", "   \n", "-- 주석만\n", ";;"]:
            status, body = query(sql)
            self.assertEqual(status, 400, repr(sql))
            self.assertFalse(body["ok"])

    def test_unsupported_command(self):
        _, body = query("DELETE FROM users;")
        self.assertIn("지원하지 않는 명령", body["results"][0]["error"])

    def test_unknown_path_and_method(self):
        status, body = send_request("GET", "/nothing")
        self.assertEqual(status, 404)
        self.assertFalse(body["ok"])
        status, body = send_request("GET", "/query")   # /query는 POST만 있다
        self.assertEqual(status, 404)
        self.assertIn("POST /query", body["error"])

    def test_too_large_body_is_rejected(self):
        big = "-- " + "x" * (9 * 1024 * 1024)   # 8MB 제한을 넘는다
        status, body = query(big)
        self.assertEqual(status, 413, body)
        self.assertIn("본문이 너무 크다", body["error"])

    def test_server_still_works_after_errors(self):
        self.assertEqual(send_request("GET", "/health")[0], 200)


class ConcurrencyTest(unittest.TestCase):
    def test_parallel_inserts_get_unique_contiguous_ids(self):
        clients, per_client = 32, 25
        prefix = unique_name("par")

        def insert_many(client):
            ids = []
            for i in range(per_client):
                ids.append((insert_user(f"{prefix}_{client}_{i}", client), f"{prefix}_{client}_{i}"))
            return ids

        with concurrent.futures.ThreadPoolExecutor(max_workers=clients) as executor:
            results = [pair for chunk in executor.map(insert_many, range(clients)) for pair in chunk]

        ids = [new_id for new_id, _ in results]
        total = clients * per_client
        self.assertEqual(len(set(ids)), total, "id가 겹쳤다")
        # 이 테스트가 도는 동안 다른 INSERT가 없으므로 번호가 빈틈없이 이어져야 한다.
        self.assertEqual(max(ids) - min(ids) + 1, total)

        # 동시에 다시 읽어서 id마다 자기가 넣은 이름이 나오는지 확인한다.
        def check(pair):
            new_id, name = pair
            _, body = query(f"SELECT name FROM users WHERE id = {new_id};")
            return body["results"][0]["rows"] == [[name]]

        with concurrent.futures.ThreadPoolExecutor(max_workers=16) as executor:
            self.assertTrue(all(executor.map(check, results)))

    def test_reads_and_writes_at_the_same_time(self):
        seed_id = insert_user(unique_name("seed"), 5)
        prefix = unique_name("mix")

        def work(i):
            if i % 3 == 0:
                status, body = query(f"INSERT INTO users VALUES ('{prefix}_{i}', 5);")
                return status == 200
            if i % 3 == 1:
                _, body = query(f"SELECT * FROM users WHERE id = {seed_id};")
                return body["results"][0]["rowCount"] == 1
            _, body = query("SELECT id FROM users WHERE age = 5;")
            return body["ok"] and body["results"][0]["rowCount"] >= 1

        with concurrent.futures.ThreadPoolExecutor(max_workers=24) as executor:
            self.assertTrue(all(executor.map(work, range(300))))

        _, body = query(f"SELECT id FROM users WHERE age = 5;")
        names_inserted = sum(1 for i in range(300) if i % 3 == 0)
        self.assertGreaterEqual(body["results"][0]["rowCount"], names_inserted + 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
