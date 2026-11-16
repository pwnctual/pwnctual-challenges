FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends gcc libc6-dev make nano \
    && rm -rf /var/lib/apt/lists/*

COPY --chown=root:root challenge /challenge

RUN chmod 755 /challenge \
    && chmod 4755 /challenge/run \
    && echo cHduY3R1YWx7MDlmZTQ2YTZiY2MxMmUyN30= | base64 -d > /flag \
    && chown root:root /flag \
    && chmod 400 /flag \
    && mkdir -p /var/lib/pwnctual \
    && chmod 700 /var/lib/pwnctual \
    && useradd -m -s /bin/bash hacker

USER hacker
WORKDIR /home/hacker
CMD ["bash"]
