#include "lexer.h"
#include "parser.h"
#include "executor.h"
#include "storage_manager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

using namespace db;

// Pretty-print a QueryResult to stdout
static void print_result(const QueryResult& res) {
    if (!res.success) {
        std::cerr << "ERROR: " << res.message << '\n';
        return;
    }

    if (!res.message.empty() && res.headers.empty()) {
        std::cout << res.message << '\n';
        return;
    }

    if (res.headers.empty()) return;

    // Compute column widths
    std::vector<size_t> widths(res.headers.size(), 0);
    for (size_t i = 0; i < res.headers.size(); ++i)
        widths[i] = res.headers[i].size();
    for (const auto& row : res.rows)
        for (size_t i = 0; i < row.size() && i < widths.size(); ++i)
            widths[i] = std::max(widths[i], row[i].size());

    auto print_sep = [&] {
        std::cout << '+';
        for (size_t w : widths) std::cout << std::string(w + 2, '-') << '+';
        std::cout << '\n';
    };
    auto print_row = [&](const std::vector<std::string>& r) {
        std::cout << '|';
        for (size_t i = 0; i < widths.size(); ++i) {
            const std::string& cell = (i < r.size()) ? r[i] : "";
            std::cout << ' ' << cell
                      << std::string(widths[i] - cell.size(), ' ') << " |";
        }
        std::cout << '\n';
    };

    print_sep();
    print_row(res.headers);
    print_sep();
    for (const auto& row : res.rows) print_row(row);
    print_sep();
    std::cout << res.rows.size() << " row(s)\n";
}

// Execute all SQL statements in `input` string; return false on any error
static bool run_input(const std::string& input, Executor& exec, bool interactive) {
    bool ok = true;
    try {
        Lexer lex(input);
        auto tokens = lex.tokenize();
        Parser parser(std::move(tokens));
        auto stmts  = parser.parse_all();

        for (const auto& stmt : stmts) {
            QueryResult res = exec.execute(stmt);
            print_result(res);
            if (!res.success) ok = false;
        }
    } catch (const LexError& e) {
        std::cerr << "Lexer error: " << e.what() << '\n';
        ok = false;
    } catch (const ParseError& e) {
        std::cerr << "Parse error: " << e.what() << '\n';
        ok = false;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        ok = false;
    }
    return ok;
}

int main(int argc, char* argv[]) {
    StorageManager sm("./data");
    Executor exec(sm);

    if (argc >= 2) {
        // Script mode: ./prog script.txt
        std::ifstream f(argv[1]);
        if (!f) {
            std::cerr << "Cannot open file: " << argv[1] << '\n';
            return 1;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        return run_input(ss.str(), exec, false) ? 0 : 1;
    }

    // Interactive mode: read statements terminated by ';'
    std::cout << "Mini-DBMS  (type SQL statements, end with ';', Ctrl-D to quit)\n";
    std::string line, buffer;
    while (true) {
        std::cout << (buffer.empty() ? "db> " : "  > ") << std::flush;
        if (!std::getline(std::cin, line)) break; // EOF / Ctrl-D
        buffer += line + '\n';
        // Execute when we see a complete statement (ends with ';')
        if (buffer.find(';') != std::string::npos) {
            run_input(buffer, exec, true);
            buffer.clear();
        }
    }
    std::cout << "\nBye.\n";
    return 0;
}
