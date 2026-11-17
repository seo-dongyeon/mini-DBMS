-- 기본 동작: 넣고 읽기
INSERT INTO users VALUES ('민수', 30);
insert into users values ('서 동연', 25);
INSERT INTO users VALUES ('쉼표,역슬래시\ 세미콜론; ''따옴표''', -1);

SELECT * FROM users;
SELECT name, id FROM users WHERE id = 2;
SELECT id FROM users WHERE name = '민수';
SELECT * FROM users WHERE age = 99;

-- 틀린 문장이 있어도 다음 문장은 실행된다
SELECT * FROM nothing;
INSERT INTO users VALUES (7, '동연', 25);
INSERT INTO users VALUES ('나이가 글자', '스물');
SELECT nope FROM users;
DELETE FROM users;
SELECT * FROM users WHERE name = 1;

-- 실패한 INSERT는 id를 쓰지 않는다. 다음 id는 4다.
INSERT INTO users VALUES ('지수', 22);
SELECT id, name FROM users WHERE id = 4;

-- 마지막 문장에 ; 가 없다
SELECT * FROM users WHERE id = 1
