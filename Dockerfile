FROM ubuntu:22.04

# Install build + runtime deps
RUN apt-get update && apt-get install -y \
    g++ \
    git \
    python3 \
    cmake \
    libboost-all-dev \
    libasio-dev \
    sqlite3 \
    libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy project into the container
COPY . .

# ---- Generate Crow single header (crow_all.h) ----
# Remove your empty header if it exists
RUN rm -f src/include/crow_all.h

# Clone Crow and generate crow_all.h into your src/include folder
RUN git clone https://github.com/CrowCpp/Crow.git /tmp/Crow \
    && python3 /tmp/Crow/scripts/merge_all.py /tmp/Crow/include src/include/crow_all.h \
    && rm -rf /tmp/Crow

# Sanity check: show the header size (should be large, not 0 bytes)
RUN ls -lh src/include/crow_all.h

# ---- Build server ----
RUN g++ -std=c++17 \
    src/main.cpp \
    src/repository/Database.cpp \
    -o server \
    -I./src \
    -I./src/include \
    -lsqlite3 \
    -lpthread

EXPOSE 8080
CMD ["./server"]
