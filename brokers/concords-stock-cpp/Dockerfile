FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

WORKDIR /app/build
RUN cmake .. && make

RUN useradd -m -s /bin/bash trader && chown -R trader:trader /app
USER trader

WORKDIR /app
VOLUME ["/app/certs"]
CMD ["./build/main"]
