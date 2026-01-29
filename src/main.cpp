#include "crow_all.h"

int main() {
    crow::SimpleApp app;

    CROW_ROUTE(app, "/health")([] {
        return crow::response(200, "OK");
    });

    app.port(8080).multithreaded().run();
    return 0;
}
