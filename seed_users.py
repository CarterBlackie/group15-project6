import sqlite3
import random
import string

DB_PATH = "db/users.db"
N_USERS = 5000
ACCOUNTS_PER_USER = 2

def rand_name(n=6):
    return "".join(random.choice(string.ascii_lowercase) for _ in range(n)).capitalize()

def main():
    conn = sqlite3.connect(DB_PATH)
    cur = conn.cursor()

    cur.execute("BEGIN TRANSACTION;")

    for i in range(1, N_USERS + 1):
        first = f"{rand_name()}_{i}"
        last = "LoadTest"
        email = f"user{i}@loadtest.com"
        pw = "password"

        cur.execute(
            "INSERT OR IGNORE INTO users (firstName, lastName, email, passwordHash) VALUES (?, ?, ?, ?);",
            (first, last, email, pw)
        )

        # get the user id (works even if user already existed)
        cur.execute("SELECT id FROM users WHERE email = ?;", (email,))
        row = cur.fetchone()
        if not row:
            continue
        user_id = row[0]

        # insert accounts
        for j in range(ACCOUNTS_PER_USER):
            acct_type = "checking" if j % 2 == 0 else "savings"
            status = "active"
            balance = float((i * 10) + j)

            cur.execute(
                "INSERT INTO accounts (userId, type, status, balance) VALUES (?, ?, ?, ?);",
                (user_id, acct_type, status, balance)
            )

    cur.execute("COMMIT;")
    conn.close()
    print("Seeding complete.")

if __name__ == "__main__":
    main()