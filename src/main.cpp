#include "crow_all.h"
#include "repository/Database.h"
#include <sqlite3.h>

int main() {
    // Initialize database
    sqlite3* db = Database::init("users.db");
    if (!db) {
        return 1;
    }

    crow::SimpleApp app;

    CROW_ROUTE(app, "/health")([] {
        return crow::response(200, "OK");
    });

    app.port(8080).multithreaded().run();

    sqlite3_close(db);
    return 0;
}
