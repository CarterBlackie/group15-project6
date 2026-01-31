# group15-project6

# Users & Accounts REST API

## Project Overview
This project implements a containerized RESTful web server for managing Users and their associated Accounts. The system is designed for performance testing and analysis, with a focus on measuring and improving server behavior under load.

The application runs inside a Docker container, exposes REST API endpoints using HTTP and JSON, and stores persistent data in a relational database.

---

## System Features

### Core Functionality
- Create, retrieve, update, and delete Users
- Create and manage Accounts linked to Users
- JSON-based request and response payloads
- Proper HTTP status codes and error handling
- CORS support using OPTIONS requests

### Performance Focus
- Supports CPU-intensive operations such as sorting, searching, and aggregation
- Load and stress tested using Apache JMeter
- Performance testing follows an iterative process: test, analyze, improve, retest

---

## Technology Stack
- Language: C++
- Web Framework: Crow
- Database: SQLite
- Containerization: Docker
- Performance Testing: Apache JMeter

---

## API Routes

### Users
- GET /users
- GET /users/:id
- POST /users
- PUT /users/:id
- DELETE /users/:id

### Accounts
- GET /users/:id/accounts
- POST /users/:id/accounts
- PATCH /accounts/:id
- DELETE /accounts/:id

### Other
- OPTIONS /*
- GET /health


### Build the Docker image
```bash
docker build -t users-api .

### Running the container
docker run -p 8080:8080 users-api
docker compose up --build

### View the website 
http://127.0.0.1:8080/health


checks for user list
curl http://localhost:8080/users
checks for specific user
curl http://localhost:8080/users/1
check for the accounts for the user
curl http://localhost:8080/users/1/accounts

