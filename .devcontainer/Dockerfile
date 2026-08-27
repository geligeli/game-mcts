FROM ubuntu:24.04

ENV PYTHONDONTWRITEBYTECODE=1
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update -y

RUN apt-get install -y locales python3 python3-pip \
  && localedef -i en_US -c -f UTF-8 -A /usr/share/locale/locale.alias en_US.UTF-8
ENV LANG=en_US.utf8

RUN apt install -y \
  ca-certificates \
  clang-format \
  clang-tidy \
  clangd \
  curl \
  gdb \
  git \
  gnupg \
  graphviz \
  linux-tools-common \
  linux-tools-generic \
  ripgrep \
  sudo \
  unzip
RUN curl -L https://github.com/bazelbuild/buildtools/releases/download/v8.2.1/buildifier-linux-amd64 -o /usr/local/bin/buildifier
RUN chmod +x /usr/local/bin/buildifier
RUN curl -L https://github.com/kiron1/bazel-compile-commands/releases/download/v0.20.1/bazel-compile-commands_0.20.1-linux_amd64.zip -o /tmp/bazel-compile-commands.zip
RUN cd / && unzip /tmp/bazel-compile-commands.zip
RUN curl -L https://github.com/bazelbuild/bazel/releases/download/8.7.0/bazel-8.7.0-linux-x86_64 -o /usr/local/bin/bazel
RUN chmod +x /usr/local/bin/bazel
RUN install -m 0755 -d /etc/apt/keyrings \
  && curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc \
  && chmod a+r /etc/apt/keyrings/docker.asc \
  && echo \
  "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/ubuntu \
  $(. /etc/os-release && echo "$VERSION_CODENAME") stable" | \
  tee /etc/apt/sources.list.d/docker.list > /dev/null \
  && apt-get update \
  && apt-get install -y docker-ce-cli docker-buildx-plugin docker-compose-plugin

RUN rm -rf /tmp/bazel-compile-commands.zip
RUN rm -rf /var/lib/apt/lists/*

RUN adduser ubuntu sudo
RUN echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

USER ubuntu

RUN /usr/local/bin/bazel help completion bash > /home/ubuntu/.bazel-complete.bash
RUN echo 'source /home/ubuntu/.bazel-complete.bash' >> /home/ubuntu/.bashrc
RUN curl -fsSL https://claude.ai/install.sh | bash
RUN curl -fsSL https://code.kimi.com/kimi-code/install.sh | bash
RUN curl -fsSL https://opencode.ai/install | bash
RUN mkdir -p /home/ubuntu/.config/opencode
COPY --chown=ubuntu:ubuntu opencode.json /home/ubuntu/.config/opencode/opencode.json
ENV PATH="/home/ubuntu/.local/bin:/home/ubuntu/.kimi-code/bin:/home/ubuntu/.opencode/bin:${PATH}"
