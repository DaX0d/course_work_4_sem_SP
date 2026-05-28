# Курсовая работа по системному программированию (2025–2026)

Реализация встроенной СУБД с SQL-подобным интерфейсом. Вариант 1 — индекс **B-дерево**.

## Что реализовано

### Основная часть

- Два режима работы:
  - **Интерактивный** — `./prog` (REPL, команды вводятся построчно)
  - **Скрипт** — `./prog script.sql` (выполнение файла)
- Управление базами данных: `CREATE DATABASE`, `DROP DATABASE`, `USE`
- DDL: `CREATE TABLE`, `DROP TABLE` (типы колонок: `INT`, `STRING`)
- Ограничения колонок: `NOT_NULL`, `INDEXED`
- DML: `INSERT INTO … VALUE`, `UPDATE … SET … WHERE`, `DELETE FROM … WHERE`, `SELECT … FROM … WHERE`
- Условия `WHERE`: операторы `==`, `!=`, `<`, `>`, `<=`, `>=`, логика `AND`/`OR`, `BETWEEN … AND`, `LIKE` (регулярные выражения)
- Данные хранятся **на диске** — B-дерево сериализуется в JSON-файл при каждом изменении и загружается при открытии таблицы

### Дополнительные задания

| № | Задание | Реализация |
|---|---|---|
| **2** | Интернирование строк (String Interning) | Все строковые значения хранятся в пуле (`strings.json`) один раз; в строках таблиц используются числовые идентификаторы пула |
| **7** | Журнал доступа (Access Logs) | Каждая операция записывается в `access.log`: время, БД, таблица, тип операции, результат |
| **10** | Значения по умолчанию (DEFAULT) | В `CREATE TABLE` для колонки можно указать `DEFAULT <value>`; при `INSERT` недостающие значения подставляются автоматически |
| **12** | Агрегатные функции | `SELECT SUM(col), COUNT(*), AVG(col) FROM …` — поддержка функций в любом сочетании |

## Инструменты и зависимости

| Инструмент | Назначение |
|---|---|
| **C++20** | Язык реализации |
| **GCC 14** (MinGW) | Компилятор |
| **CMake ≥ 3.20** | Система сборки |
| **vcpkg** | Менеджер пакетов |
| **Boost.Container** | `static_vector` для узлов B-дерева |
| **nlohmann/json** | Сериализация таблиц и строкового пула на диск |

## Структура проекта

```
include/
  b_tree.h              — B-дерево (предоставлено)
  associative_container.h / pp_allocator.h — вспомогательные (предоставлены)
  types.h               — базовые типы (ColumnValue, Row)
  schema.h              — схема таблицы
  table.h / database.h / storage_manager.h — слои хранилища
  string_pool.h         — интернирование строк (задание 2)
  access_log.h          — журнал доступа (задание 7)
  lexer.h / ast.h / parser.h / executor.h — SQL-интерпретатор
src/
  main.cpp              — точка входа
  table.cpp             — B-дерево + вторичные индексы + сериализация
  executor.cpp          — выполнение SQL-запросов
  ...
data/                   — директория с файлами БД (создаётся автоматически)
```

## Сборка

```bash
cmake -B build -G "Unix Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE=C:/Users/<user>/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic \
  -DCMAKE_CXX_COMPILER="C:/Program Files/gcc/bin/x86_64-w64-mingw32-g++.exe" \
  -DCMAKE_MAKE_PROGRAM="C:/Program Files/gcc/bin/make.exe"

"C:/Program Files/gcc/bin/make.exe" -C build -j4
```

## Пример использования

```sql
CREATE DATABASE shop;
USE shop;

CREATE TABLE products (
    id     INT    NOT_NULL INDEXED,
    name   STRING NOT_NULL,
    price  INT    NOT_NULL DEFAULT 0
);

INSERT INTO products (id, name, price) VALUE (1, "Apple", 50), (2, "Banana", 30);

SELECT * FROM products WHERE price >= 40;
SELECT COUNT(*), AVG(price) FROM products;

UPDATE products SET price = 35 WHERE name LIKE "Ban.*";
DELETE FROM products WHERE id == 1;
```
