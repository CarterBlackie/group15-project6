#include "crow_all.h"
#include "repository/Database.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>   // getenv
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static crow::response json_error(int code, const std::string& msg) {
    crow::json::wvalue out;
    out["error"] = msg;
    crow::response res(code);
    res.set_header("Content-Type", "application/json");
    res.write(out.dump());
    return res;
}

// Trim leading/trailing whitespace
static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    size_t end   = s.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

// Very basic email check
static bool is_valid_email(const std::string& email) {
    size_t at = email.find('@');
    size_t dot = email.find('.', at == std::string::npos ? 0 : at);
    return at != std::string::npos &&
           dot != std::string::npos &&
           at > 0 &&
           dot > at + 1 &&
           dot < email.length() - 1;
}

static bool is_allowed_account_type(const std::string& type) {
    return type == "checking" || type == "savings";
}

static bool is_allowed_account_status(const std::string& status) {
    return status == "active" || status == "locked";
}

static bool user_exists(sqlite3* db, int userId) {
    const char* sql = "SELECT 1 FROM users WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int(stmt, 1, userId);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_ROW);
}

static bool account_exists(sqlite3* db, int accountId) {
    const char* sql = "SELECT 1 FROM accounts WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int(stmt, 1, accountId);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_ROW);
}

static bool user_has_accounts(sqlite3* db, int userId) {
    const char* sql = "SELECT 1 FROM accounts WHERE userId = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int(stmt, 1, userId);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_ROW);
}

static const char* method_to_string(crow::HTTPMethod method) {
    switch (method) {
        case crow::HTTPMethod::GET:     return "GET";
        case crow::HTTPMethod::POST:    return "POST";
        case crow::HTTPMethod::PUT:     return "PUT";
        case crow::HTTPMethod::PATCH:   return "PATCH";
        case crow::HTTPMethod::DELETE:  return "DELETE";
        case crow::HTTPMethod::OPTIONS: return "OPTIONS";
        default:                        return "UNKNOWN";
    }
}

struct RequestLogger {
    struct context {
        std::chrono::steady_clock::time_point start;
    };

    void before_handle(crow::request&, crow::response&, context& ctx) {
        ctx.start = std::chrono::steady_clock::now();
    }

    void after_handle(crow::request& req, crow::response& res, context& ctx) {
        auto end = std::chrono::steady_clock::now();
        auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - ctx.start).count();

        std::time_t now = std::time(nullptr);

        std::cout
            << "[" << std::ctime(&now) << "] "
            << method_to_string(req.method) << " "
            << req.url << " "
            << res.code << " "
            << duration << "ms"
            << std::endl;
    }
};

static crow::response serve_file(const std::string& path, const std::string& contentType) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return crow::response(404, "File not found: " + path);
    }

    std::ostringstream ss;
    ss << file.rdbuf();

    crow::response res(200);
    res.set_header("Content-Type", contentType);
    res.write(ss.str());
    return res;
}

// Simple RAII DB wrapper (Fix A)
struct DbConn {
    sqlite3* db = nullptr;

    explicit DbConn(const std::string& dbPath) {
        db = Database::init(dbPath.c_str());
    }

    ~DbConn() {
        if (db) sqlite3_close(db);
    }

    DbConn(const DbConn&) = delete;
    DbConn& operator=(const DbConn&) = delete;
};

int main() {
    std::string dbPath = "db/users.db";
    if (const char* envDb = std::getenv("DB_PATH")) {
        dbPath = envDb;
    }

    // Fail fast if DB cannot be opened at startup
    {
        DbConn test(dbPath);
        if (!test.db) return 1;
    }

    crow::App<RequestLogger> app;

    // ---- UI (served from same origin) ----
    CROW_ROUTE(app, "/")([] {
        return serve_file("UI/index.html", "text/html; charset=utf-8");
    });

    CROW_ROUTE(app, "/index.html")([] {
        return serve_file("UI/index.html", "text/html; charset=utf-8");
    });

    CROW_ROUTE(app, "/users.html")([] {
        return serve_file("UI/users.html", "text/html; charset=utf-8");
    });

    CROW_ROUTE(app, "/user.html")([] {
        return serve_file("UI/user.html", "text/html; charset=utf-8");
    });

    CROW_ROUTE(app, "/app.js")([] {
        return serve_file("UI/app.js", "application/javascript; charset=utf-8");
    });

    CROW_ROUTE(app, "/styles.css")([] {
        return serve_file("UI/styles.css", "text/css; charset=utf-8");
    });

    // CORS preflight
    CROW_ROUTE(app, "/<path>")
        .methods(crow::HTTPMethod::OPTIONS)
        ([](const crow::request&, crow::response& res, std::string) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type");
            res.code = 204;
            res.end();
        });

    // Health check
    CROW_ROUTE(app, "/health")([] {
        return crow::response(200, "OK");
    });

    // GET /users -> DB-side sorted + paginated user listing (fast)
    CROW_ROUTE(app, "/users").methods(crow::HTTPMethod::GET)
    ([dbPath](const crow::request& req) {

        DbConn conn(dbPath);
        if (!conn.db) return json_error(500, "Database connection failed");
        sqlite3* db = conn.db;

        // ---- Defaults ----
        std::string sort  = "lastName";
        std::string order = "asc";
        int page  = 1;
        int limit = 10;

        if (req.url_params.get("sort"))  sort  = req.url_params.get("sort");
        if (req.url_params.get("order")) order = req.url_params.get("order");
        if (req.url_params.get("page"))  page  = std::stoi(req.url_params.get("page"));
        if (req.url_params.get("limit")) limit = std::stoi(req.url_params.get("limit"));

        if (order != "asc" && order != "desc") return json_error(400, "Invalid order (asc|desc)");
        if (page < 1) return json_error(400, "page must be >= 1");
        if (limit < 1 || limit > 100) return json_error(400, "limit must be between 1 and 100");

        // ---- Whitelist columns (prevents SQL injection) ----
        std::string sortCol;
        if (sort == "firstName") sortCol = "firstName";
        else if (sort == "lastName") sortCol = "lastName";
        else if (sort == "email") sortCol = "email";
        else if (sort == "createdAt") sortCol = "createdAt";
        else return json_error(400, "Invalid sort field");

        int offset = (page - 1) * limit;

        // ---- Total count ----
        int total = 0;
        {
            const char* countSql = "SELECT COUNT(*) FROM users;";
            sqlite3_stmt* st = nullptr;
            if (sqlite3_prepare_v2(db, countSql, -1, &st, nullptr) != SQLITE_OK) {
                return json_error(500, "Failed to prepare count query");
            }
            if (sqlite3_step(st) == SQLITE_ROW) total = sqlite3_column_int(st, 0);
            sqlite3_finalize(st);
        }

        // ---- Page query (DB does sort + pagination) ----
        std::string sql =
            "SELECT id, firstName, lastName, email, createdAt, updatedAt "
            "FROM users "
            "ORDER BY " + sortCol + " " + order + " "
            "LIMIT ? OFFSET ?;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            return json_error(500, "Failed to prepare page query");
        }

        sqlite3_bind_int(stmt, 1, limit);
        sqlite3_bind_int(stmt, 2, offset);

        crow::json::wvalue result;
        result["page"]  = page;
        result["limit"] = limit;
        result["total"] = total;
        result["users"] = crow::json::wvalue::list();

        int i = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            crow::json::wvalue u;
            u["id"]        = sqlite3_column_int(stmt, 0);
            u["firstName"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            u["lastName"]  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            u["email"]     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            u["createdAt"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            u["updatedAt"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            result["users"][i++] = std::move(u);
        }

        sqlite3_finalize(stmt);

        crow::response res(200);
        res.set_header("Content-Type", "application/json");
        res.write(result.dump());
        return res;
    });

    // POST /users -> create a user
    CROW_ROUTE(app, "/users").methods(crow::HTTPMethod::POST)
    ([dbPath](const crow::request& req) {
        DbConn conn(dbPath);
        if (!conn.db) return json_error(500, "Database connection failed");
        sqlite3* db = conn.db;

        auto body = crow::json::load(req.body);
        if (!body) return json_error(400, "Invalid JSON");

        if (!body.has("firstName") || !body.has("lastName") || !body.has("email") || !body.has("password")) {
            return json_error(400, "Missing required fields: firstName, lastName, email, password");
        }

        std::string firstName = trim(body["firstName"].s());
        std::string lastName  = trim(body["lastName"].s());
        std::string email     = trim(body["email"].s());
        std::string password  = body["password"].s();

        if (firstName.empty() || lastName.empty() || email.empty() || password.empty()) {
            return json_error(400, "Fields cannot be empty");
        }

        if (firstName.length() > 100 || lastName.length() > 100) {
            return json_error(400, "First and last name must be at most 100 characters");
        }
        if (email.length() > 255) {
            return json_error(400, "Email must be at most 255 characters");
        }
        if (password.length() < 6) {
            return json_error(400, "Password must be at least 6 characters");
        }
        if (!is_valid_email(email)) {
            return json_error(400, "Invalid email format");
        }

        // NOTE: Replace with real hashing later
        std::string passwordHash = password;

        const char* sql =
            "INSERT INTO users (firstName, lastName, email, passwordHash) "
            "VALUES (?, ?, ?, ?);";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return json_error(500, "Failed to prepare insert");
        }

        sqlite3_bind_text(stmt, 1, firstName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, lastName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, email.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, passwordHash.c_str(), -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            if (rc == SQLITE_CONSTRAINT) return json_error(409, "Email already exists");
            return json_error(500, "Failed to create user");
        }

        int newId = (int)sqlite3_last_insert_rowid(db);

        crow::json::wvalue out;
        out["id"] = newId;
        out["firstName"] = firstName;
        out["lastName"] = lastName;
        out["email"] = email;

        crow::response res(201);
        res.set_header("Content-Type", "application/json");
        res.write(out.dump());
        return res;
    });

    // POST /login -> authenticate user
    CROW_ROUTE(app, "/login").methods(crow::HTTPMethod::POST)
    ([dbPath](const crow::request& req) {
        DbConn conn(dbPath);
        if (!conn.db) return json_error(500, "Database connection failed");
        sqlite3* db = conn.db;

        auto body = crow::json::load(req.body);
        if (!body) return json_error(400, "Invalid JSON");

        if (!body.has("email") || !body.has("password")) {
            return json_error(400, "Missing required fields: email, password");
        }

        std::string email = trim(body["email"].s());
        std::string password = body["password"].s();

        if (email.empty() || password.empty()) {
            return json_error(400, "Email and password cannot be empty");
        }

        const char* sql = "SELECT id, passwordHash FROM users WHERE email = ?;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return json_error(500, "Failed to prepare query");
        }

        sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            return json_error(401, "Invalid email or password");
        }

        int userId = sqlite3_column_int(stmt, 0);
        std::string storedHash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        sqlite3_finalize(stmt);

        // NOTE: Plain-text comparison for now
        if (password != storedHash) {
            return json_error(401, "Invalid email or password");
        }

        crow::json::wvalue out;
        out["message"] = "Authentication successful";
        out["userId"] = userId;

        crow::response res(200);
        res.set_header("Content-Type", "application/json");
        res.write(out.dump());
        return res;
    });

    // GET /users/:id -> return a single user by ID
    CROW_ROUTE(app, "/users/<int>").methods(crow::HTTPMethod::GET)
    ([dbPath](int userId) {
        DbConn conn(dbPath);
        if (!conn.db) return json_error(500, "Database connection failed");
        sqlite3* db = conn.db;

        const char* sql =
            "SELECT id, firstName, lastName, email, createdAt, updatedAt "
            "FROM users WHERE id = ?;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return json_error(500, "Failed to prepare query");
        }

        sqlite3_bind_int(stmt, 1, userId);

        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            return json_error(404, "User not found");
        }

        crow::json::wvalue user;
        user["id"] = sqlite3_column_int(stmt, 0);
        user["firstName"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        user["lastName"]  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        user["email"]     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        user["createdAt"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        user["updatedAt"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        sqlite3_finalize(stmt);

        crow::response res(200);
        res.set_header("Content-Type", "application/json");
        res.write(user.dump());
        return res;
    });

    // GET /users/:id/accounts -> list accounts for a user
    CROW_ROUTE(app, "/users/<int>/accounts").methods(crow::HTTPMethod::GET)
    ([dbPath](int userId) {
        DbConn conn(dbPath);
        if (!conn.db) return json_error(500, "Database connection failed");
        sqlite3* db = conn.db;

        if (!user_exists(db, userId)) return json_error(404, "User not found");

        const char* sql =
            "SELECT id, userId, type, status, balance, createdAt, updatedAt "
            "FROM accounts WHERE userId = ? ORDER BY id ASC;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return json_error(500, "Failed to prepare query");
        }

        sqlite3_bind_int(stmt, 1, userId);

        crow::json::wvalue result;
        result["accounts"] = crow::json::wvalue::list();

        int i = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            crow::json::wvalue a;
            a["id"] = sqlite3_column_int(stmt, 0);
            a["userId"] = sqlite3_column_int(stmt, 1);
            a["type"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            a["status"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            a["balance"] = sqlite3_column_double(stmt, 4);
            a["createdAt"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            a["updatedAt"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            result["accounts"][i++] = std::move(a);
        }

        sqlite3_finalize(stmt);

        crow::response res(200);
        res.set_header("Content-Type", "application/json");
        res.write(result.dump());
        return res;
    });

    // PUT /users/:id -> fully replace a user
    CROW_ROUTE(app, "/users/<int>").methods(crow::HTTPMethod::PUT)
    ([dbPath](const crow::request& req, int userId) {
        DbConn conn(dbPath);
        if (!conn.db) return json_error(500, "Database connection failed");
        sqlite3* db = conn.db;

        if (!user_exists(db, userId)) return json_error(404, "User not found");

        auto body = crow::json::load(req.body);
        if (!body) return json_error(400, "Invalid JSON");

        if (!body.has("firstName") || !body.has("lastName") || !body.has("email")) {
            return json_error(400, "Missing required fields: firstName, lastName, email");
        }

        std::string firstName = trim(body["firstName"].s());
        std::string lastName  = trim(body["lastName"].s());
        std::string email     = trim(body["email"].s());

        if (firstName.empty() || lastName.empty() || email.empty()) {
            return json_error(400, "Fields cannot be empty");
        }
        if (!is_valid_email(email)) {
            return json_error(400, "Invalid email format");
        }

        const char* sql =
            "UPDATE users SET firstName = ?, lastName = ?, email = ?, updatedAt = CURRENT_TIMESTAMP "
            "WHERE id = ?;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return json_error(500, "Failed to prepare update");
        }

        sqlite3_bind_text(stmt, 1, firstName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, lastName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, email.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, userId);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) return json_error(500, "Failed to update user");

        crow::json::wvalue out;
        out["id"] = userId;
        out["firstName"] = firstName;
        out["lastName"] = lastName;
        out["email"] = email;

        crow::response res(200);
        res.set_header("Content-Type", "application/json");
        res.write(out.dump());
        return res;
    });

    // POST /users/:id/accounts -> create an account for a user
    CROW_ROUTE(app, "/users/<int>/accounts").methods(crow::HTTPMethod::POST)
    ([dbPath](const crow::request& req, int userId) {
        DbConn conn(dbPath);
        if (!conn.db) return json_error(500, "Database connection failed");
        sqlite3* db = conn.db;

        if (!user_exists(db, userId)) return json_error(404, "User not found");

        auto body = crow::json::load(req.body);
        if (!body) return json_error(400, "Invalid JSON");

        if (!body.has("type")) return json_error(400, "Missing required field: type");

        std::string type = trim(body["type"].s());
        if (type.empty()) return json_error(400, "type cannot be empty");
        if (!is_allowed_account_type(type)) {
            return json_error(400, "Invalid account type (allowed: checking, savings)");
        }

        std::string status = "active";
        if (body.has("status")) {
            status = trim(body["status"].s());
            if (status.empty()) return json_error(400, "status cannot be empty");
            if (!is_allowed_account_status(status)) {
                return json_error(400, "Invalid account status (allowed: active, locked)");
            }
        }

        double balance = 0.0;
        if (body.has("balance")) {
            if (body["balance"].t() != crow::json::type::Number) {
                return json_error(400, "balance must be a number");
            }
            balance = body["balance"].d();
            if (balance < 0) return json_error(400, "balance cannot be negative");
        }

        const char* sql =
            "INSERT INTO accounts (userId, type, status, balance) "
            "VALUES (?, ?, ?, ?);";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return json_error(500, "Failed to prepare insert");
        }

        sqlite3_bind_int(stmt, 1, userId);
        sqlite3_bind_text(stmt, 2, type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 4, balance);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) return json_error(500, "Failed to create account");

        int newId = (int)sqlite3_last_insert_rowid(db);

        crow::json::wvalue out;
        out["id"] = newId;
        out["userId"] = userId;
        out["type"] = type;
        out["status"] = status;
        out["balance"] = balance;

        crow::response res(201);
        res.set_header("Content-Type", "application/json");
        res.write(out.dump());
        return res;
    });

    // PATCH /accounts/:id -> partial update of an account
    CROW_ROUTE(app, "/accounts/<int>").methods(crow::HTTPMethod::PATCH)
    ([dbPath](const crow::request& req, int accountId) {
        DbConn conn(dbPath);
        if (!conn.db) return json_error(500, "Database connection failed");
        sqlite3* db = conn.db;

        if (!account_exists(db, accountId)) return json_error(404, "Account not found");

        // Fetch current account status
        std::string currentStatus;
        {
            const char* statusSql = "SELECT status FROM accounts WHERE id = ?;";
            sqlite3_stmt* statusStmt = nullptr;

            if (sqlite3_prepare_v2(db, statusSql, -1, &statusStmt, nullptr) != SQLITE_OK) {
                return json_error(500, "Failed to read account status");
            }

            sqlite3_bind_int(statusStmt, 1, accountId);

            int rc = sqlite3_step(statusStmt);
            if (rc != SQLITE_ROW) {
                sqlite3_finalize(statusStmt);
                return json_error(404, "Account not found");
            }

            currentStatus = reinterpret_cast<const char*>(sqlite3_column_text(statusStmt, 0));
            sqlite3_finalize(statusStmt);
        }

        auto body = crow::json::load(req.body);
        if (!body) return json_error(400, "Invalid JSON");

        bool hasType = body.has("type");
        bool hasStatus = body.has("status");
        bool hasBalance = body.has("balance");

        if (currentStatus == "locked" && hasBalance) {
            return json_error(400, "Cannot update balance on a locked account");
        }
        if (currentStatus == "locked" && hasStatus) {
            std::string newStatus = trim(body["status"].s());
            if (newStatus == "active") {
                return json_error(400, "Locked accounts cannot be reactivated");
            }
        }

        if (!hasType && !hasStatus && !hasBalance) {
            return json_error(400, "No valid fields to update (allowed: type, status, balance)");
        }

        for (const auto& kv : body) {
            std::string key = kv.key();
            if (key != "type" && key != "status" && key != "balance") {
                return json_error(400, "Unknown field: " + key);
            }
        }

        std::string type;
        std::string status;
        double balance = 0.0;

        if (hasType) {
            type = body["type"].s();
            if (type.empty()) return json_error(400, "type cannot be empty");
        }
        if (hasStatus) {
            status = body["status"].s();
            if (status.empty()) return json_error(400, "status cannot be empty");
        }
        if (hasBalance) {
            balance = body["balance"].d();
            if (balance < 0) return json_error(400, "balance cannot be negative");
        }

        std::string sql = "UPDATE accounts SET ";
        bool first = true;

        if (hasType) {
            sql += "type = ?";
            first = false;
        }
        if (hasStatus) {
            if (!first) sql += ", ";
            sql += "status = ?";
            first = false;
        }
        if (hasBalance) {
            if (!first) sql += ", ";
            sql += "balance = ?";
            first = false;
        }

        sql += ", updatedAt = CURRENT_TIMESTAMP WHERE id = ?;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            return json_error(500, "Failed to prepare update");
        }

        int idx = 1;
        if (hasType) sqlite3_bind_text(stmt, idx++, type.c_str(), -1, SQLITE_TRANSIENT);
        if (hasStatus) sqlite3_bind_text(stmt, idx++, status.c_str(), -1, SQLITE_TRANSIENT);
        if (hasBalance) sqlite3_bind_double(stmt, idx++, balance);
        sqlite3_bind_int(stmt, idx++, accountId);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) return json_error(500, "Failed to update account");

        const char* selectSql =
            "SELECT id, userId, type, status, balance, createdAt, updatedAt "
            "FROM accounts WHERE id = ?;";

        sqlite3_stmt* stmt2 = nullptr;
        if (sqlite3_prepare_v2(db, selectSql, -1, &stmt2, nullptr) != SQLITE_OK) {
            return json_error(500, "Failed to prepare query");
        }

        sqlite3_bind_int(stmt2, 1, accountId);

        int rc2 = sqlite3_step(stmt2);
        if (rc2 != SQLITE_ROW) {
            sqlite3_finalize(stmt2);
            return json_error(500, "Failed to read updated account");
        }

        crow::json::wvalue out;
        out["id"] = sqlite3_column_int(stmt2, 0);
        out["userId"] = sqlite3_column_int(stmt2, 1);
        out["type"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt2, 2));
        out["status"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt2, 3));
        out["balance"] = sqlite3_column_double(stmt2, 4);
        out["createdAt"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt2, 5));
        out["updatedAt"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt2, 6));
        sqlite3_finalize(stmt2);

        crow::response res(200);
        res.set_header("Content-Type", "application/json");
        res.write(out.dump());
        return res;
    });

    // DELETE /accounts/:id -> delete an account
    CROW_ROUTE(app, "/accounts/<int>").methods(crow::HTTPMethod::DELETE)
    ([dbPath](int accountId) {
        DbConn conn(dbPath);
        if (!conn.db) return json_error(500, "Database connection failed");
        sqlite3* db = conn.db;

        if (!account_exists(db, accountId)) return json_error(404, "Account not found");

        const char* sql = "DELETE FROM accounts WHERE id = ?;";
        sqlite3_stmt* stmt = nullptr;

        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return json_error(500, "Failed to prepare delete");
        }

        sqlite3_bind_int(stmt, 1, accountId);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) return json_error(500, "Failed to delete account");
        return crow::response(204);
    });

    // DELETE /users/:id -> delete a user (only if no accounts exist)
    CROW_ROUTE(app, "/users/<int>").methods(crow::HTTPMethod::DELETE)
    ([dbPath](int userId) {
        DbConn conn(dbPath);
        if (!conn.db) return json_error(500, "Database connection failed");
        sqlite3* db = conn.db;

        if (!user_exists(db, userId)) return json_error(404, "User not found");

        if (user_has_accounts(db, userId)) {
            return json_error(409, "Cannot delete user with existing accounts");
        }

        const char* sql = "DELETE FROM users WHERE id = ?;";
        sqlite3_stmt* stmt = nullptr;

        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return json_error(500, "Failed to prepare delete");
        }

        sqlite3_bind_int(stmt, 1, userId);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) return json_error(500, "Failed to delete user");
        return crow::response(204);
    });

    int port = 8080;
    if (const char* envPort = std::getenv("PORT")) {
        try { port = std::stoi(envPort); }
        catch (...) { std::cerr << "Invalid PORT value, using default 8080\n"; }
    }

    // You can keep multithreaded now that DB is per-request
    app.port(port).multithreaded().run();
    return 0;
}