FROM mcr.microsoft.com/devcontainers/base:ubuntu

RUN apt-get update \
    && apt-get install -y --no-install-recommends gcc libc6-dev \
    && rm -rf /var/lib/apt/lists/*

COPY challenges/the-computers-language/run.c /tmp/run.c

RUN mkdir /challenge \
    && gcc -O2 -o /challenge/run /tmp/run.c \
    && rm /tmp/run.c \
    && chown -R root:root /challenge \
    && chmod 755 /challenge \
    && chmod 4755 /challenge/run \
    && echo cHduY3R1YWx7NjVhMjQ2NjQ1NGZmZDA2YX0= | base64 -d > /flag \
    && chown root:root /flag \
    && chmod 400 /flag

# The reader gets a normal user without sudo
RUN apt-get purge -y sudo \
    && rm -rf /etc/sudoers /etc/sudoers.d \
    && (deluser vscode sudo || true)
