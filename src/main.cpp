#include "crow_all.h"
#include "repository/Database.h"

#include <sqlite3.h>
#include <string>

static crow::response json_error(int code, const std::string& msg) {
    crow::json::wvalue out;
    out["error"] = msg;
    crow::response res(code);
    res.set_header("Content-Type", "application/json");
    res.write(out.dump());
    return res;
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

int main() {
    sqlite3* db = Database::init("db/users.db");
    if (!db) {
        return 1;
    }

    crow::SimpleApp app;

    // Health check
    CROW_ROUTE(app, "/health")([] {
        return crow::response(200, "OK");
    });

    // GET /users -> returns all users (sorted by lastName then firstName)
    CROW_ROUTE(app, "/users").methods(crow::HTTPMethod::GET)([db] {
        const char* sql =
            "SELECT id, firstName, lastName, email, createdAt, updatedAt "
            "FROM users ORDER BY lastName ASC, firstName ASC;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return json_error(500, "Failed to prepare query");
        }

        crow::json::wvalue result;
        result["users"] = crow::json::wvalue::list();

        int i = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            crow::json::wvalue u;

            u["id"] = sqlite3_column_int(stmt, 0);
            u["firstName"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            u["lastName"]  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            u["email"]     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            u["createdAt"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            u["updatedAt"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));

            // Crow compatibility: list by index
            result["users"][i++] = std::move(u);
        }

        sqlite3_finalize(stmt);

        crow::response res(200);
        res.set_header("Content-Type", "application/json");
        res.write(result.dump());
        return res;
    });

    // POST /users -> create a user (password stored as passwordHash for now)
    CROW_ROUTE(app, "/users").methods(crow::HTTPMethod::POST)([db](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) {
            return json_error(400, "Invalid JSON");
        }

        if (!body.has("firstName") || !body.has("lastName") || !body.has("email") || !body.has("password")) {
            return json_error(400, "Missing required fields: firstName, lastName, email, password");
        }

        std::string firstName = body["firstName"].s();
        std::string lastName  = body["lastName"].s();
        std::string email     = body["email"].s();
        std::string password  = body["password"].s();

        if (firstName.empty() || lastName.empty() || email.empty() || password.empty()) {
            return json_error(400, "Fields cannot be empty");
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
            if (rc == SQLITE_CONSTRAINT) {
                return json_error(409, "Email already exists");
            }
            return json_error(500, "Failed to create user");
        }

        int newId = static_cast<int>(sqlite3_last_insert_rowid(db));

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

    // GET /users/:id -> return a single user by ID
    CROW_ROUTE(app, "/users/<int>").methods(crow::HTTPMethod::GET)
    ([db](int userId) {
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
    ([db](int userId) {
        if (!user_exists(db, userId)) {
            return json_error(404, "User not found");
        }

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

    // POST /users/:id/accounts -> create an account for a user
    CROW_ROUTE(app, "/users/<int>/accounts").methods(crow::HTTPMethod::POST)
    ([db](const crow::request& req, int userId) {
        if (!user_exists(db, userId)) {
            return json_error(404, "User not found");
        }

        auto body = crow::json::load(req.body);
        if (!body) {
            return json_error(400, "Invalid JSON");
        }

        if (!body.has("type")) {
            return json_error(400, "Missing required field: type");
        }

        std::string type = body["type"].s();
        if (type.empty()) {
            return json_error(400, "type cannot be empty");
        }

        std::string status = "active";
        if (body.has("status")) {
            status = body["status"].s();
            if (status.empty()) status = "active";
        }

        double balance = 0.0;
        if (body.has("balance")) {
            balance = body["balance"].d();
            if (balance < 0) {
                return json_error(400, "balance cannot be negative");
            }
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

        if (rc != SQLITE_DONE) {
            return json_error(500, "Failed to create account");
        }

        int newId = static_cast<int>(sqlite3_last_insert_rowid(db));

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

    app.port(8080).multithreaded().run();

    sqlite3_close(db);
    return 0;
}
