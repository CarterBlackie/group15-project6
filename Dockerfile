FROM ubuntu:22.04

RUN apt-get update && apt-get install -y python3

WORKDIR /app
COPY . .

EXPOSE 8080
CMD ["python3", "-m", "http.server", "8080"]
