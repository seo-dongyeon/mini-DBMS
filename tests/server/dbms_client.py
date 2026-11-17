"""mini-DBMS 서버에 SQL을 보내는 작은 클라이언트. 표준 라이브러리만 쓴다."""

import json
import os
import urllib.error
import urllib.request

BASE_URL = os.environ.get("DBMS_URL", "http://localhost:8080")


def send_request(method, path, body=None, timeout=30):
    """HTTP 요청을 보내고 (상태 코드, JSON 응답)을 돌려준다. 4xx·5xx도 예외 없이 돌려준다."""
    data = body.encode("utf-8") if isinstance(body, str) else body
    request = urllib.request.Request(BASE_URL + path, data=data, method=method)
    if data is not None:
        request.add_header("Content-Type", "text/plain; charset=utf-8")
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.status, json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        return error.code, json.loads(error.read().decode("utf-8"))


def query(sql):
    """SQL을 POST /query로 보낸다."""
    return send_request("POST", "/query", sql)
