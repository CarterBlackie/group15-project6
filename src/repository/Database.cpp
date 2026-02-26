#include "Database.h"
#include <iostream>
#include <mutex>

static void apply_pragmas(sqlite3* db) {
    // Better concurrency + fewer stalls under load
    // WAL allows reads while writes happen
    // busy_timeout prevents "database is locked" style stalls from killing requests
    sqlite3_busy_timeout(db, 5000);

    char* errMsg = nullptr;
    const char* pragmas =
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=NORMAL;"
        "PRAGMA foreign_keys=ON;";

    if (sqlite3_exec(db, pragmas, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::cerr << "Failed to apply pragmas: " << (errMsg ? errMsg : "") << "\n";
        sqlite3_free(errMsg);
    }
}

static bool ensure_schema_once(sqlite3* db) {
    static std::once_flag once;

    bool ok = true;

    std::call_once(once, [&]() {
        const char* schema = R"(
            CREATE TABLE IF NOT EXISTS users (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                firstName TEXT NOT NULL,
                lastName TEXT NOT NULL,
                email TEXT NOT NULL UNIQUE,
                passwordHash TEXT NOT NULL,
                createdAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                updatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            );

            CREATE TABLE IF NOT EXISTS accounts (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                userId INTEGER NOT NULL,
                type TEXT NOT NULL,
                status TEXT NOT NULL,
                balance REAL NOT NULL DEFAULT 0,
                createdAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                updatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                FOREIGN KEY (userId) REFERENCES users(id)
            );
        )";

        char* errMsg = nullptr;
        if (sqlite3_exec(db, schema, nullptr, nullptr, &errMsg) != SQLITE_OK) {
            std::cerr << "Failed to create tables: " << (errMsg ? errMsg : "") << "\n";
            sqlite3_free(errMsg);
            ok = false;
        }
    });

    return ok;
}

sqlite3* Database::init(const std::string& dbPath) {
    sqlite3* db = nullptr;

    // FULLMUTEX makes a single connection safe if accidentally shared
    // (we still do per-request connections in main.cpp)
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;

    if (sqlite3_open_v2(dbPath.c_str(), &db, flags, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to open database: " << (db ? sqlite3_errmsg(db) : "unknown") << "\n";
        if (db) sqlite3_close(db);
        return nullptr;
    }

    apply_pragmas(db);

    if (!ensure_schema_once(db)) {
        sqlite3_close(db);
        return nullptr;
    }

    // IMPORTANT: no per-request console spam
    return db;
}