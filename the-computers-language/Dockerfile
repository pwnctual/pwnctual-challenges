FROM debian:bookworm-slim

COPY --chown=root:root challenge /challenge

RUN chmod 755 /challenge \
    && chmod 4755 /challenge/run \
    && echo cHduY3R1YWx7NjVhMjQ2NjQ1NGZmZDA2YX0= | base64 -d > /flag \
    && chown root:root /flag \
    && chmod 400 /flag \
    && useradd -m -s /bin/bash hacker

USER hacker
WORKDIR /home/hacker
CMD ["bash"]
