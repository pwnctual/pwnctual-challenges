FROM mcr.microsoft.com/devcontainers/base:ubuntu

COPY --chown=root:root challenge /challenge

RUN chmod 755 /challenge \
    && chmod 4755 /challenge/run \
    && echo cHduY3R1YWx7NjVhMjQ2NjQ1NGZmZDA2YX0= | base64 -d > /flag \
    && chown root:root /flag \
    && chmod 400 /flag

# The reader gets a normal user without sudo.
# sudo refuses to uninstall while root has no password unless forced
RUN SUDO_FORCE_REMOVE=yes apt-get purge -y sudo \
    && rm -rf /etc/sudoers /etc/sudoers.d \
    && (deluser vscode sudo || true)
