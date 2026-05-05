FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libcurl4-openssl-dev \
    locales \
    && locale-gen en_US.UTF-8 \
    && rm -rf /var/lib/apt/lists/*

ENV LANG=en_US.UTF-8
ENV LC_ALL=en_US.UTF-8

WORKDIR /app
COPY . .

WORKDIR /app/build
RUN cmake .. && make

RUN useradd -m -s /bin/bash trader && chown -R trader:trader /app
USER trader

WORKDIR /app
VOLUME ["/app/certs"]
CMD ["./build/main"]
