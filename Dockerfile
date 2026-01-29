FROM ubuntu:22.04

# ---- Install dependencies ----
RUN apt-get update && apt-get install -y \
    g++ \
    git \
    python3 \
    libboost-all-dev \
    libasio-dev \
    sqlite3 \
    libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

# ---- Set working directory ----
WORKDIR /app

RUN mkdir -p /app/db

# ---- Copy project files ----
COPY . .

# ---- Generate Crow single-header into your include folder ----
# Remove any empty/old header first
RUN rm -f src/include/crow_all.h

# Clone Crow and merge into a single header at src/include/crow_all.h
RUN git clone https://github.com/CrowCpp/Crow.git /tmp/Crow \
    && python3 /tmp/Crow/scripts/merge_all.py /tmp/Crow/include src/include/crow_all.h \
    && rm -rf /tmp/Crow

# Sanity check: header must be non-empty
RUN test -s src/include/crow_all.h

# ---- Build server ----
RUN g++ -std=c++17 \
    src/main.cpp \
    src/repository/Database.cpp \
    -o server \
    -I./src \
    -I./src/include \
    -lsqlite3 \
    -lpthread

# ---- SAFETY CHECK: fail build if accounts routes are not in the binary ----
RUN strings ./server | grep -i "users/<int>/accounts"

EXPOSE 8080
CMD ["./server"]
