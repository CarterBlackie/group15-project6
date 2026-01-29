#pragma once
#include <sqlite3.h>
#include <string>

class Database {
public:
    static sqlite3* init(const std::string& dbPath);
};
