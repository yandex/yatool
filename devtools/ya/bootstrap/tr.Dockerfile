FROM ubuntu:noble

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    wget \
    lsb-release \
    software-properties-common \
    gnupg \
    ca-certificates \
    python3 \
    git \
    make \
    && rm -rf /var/lib/apt/lists/*

ARG LLVM_VERSION=20

RUN set -ex \
    && wget https://apt.llvm.org/llvm.sh \
    && chmod +x llvm.sh \
    && ./llvm.sh ${LLVM_VERSION} \
    && rm llvm.sh \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

ENV PATH="/usr/lib/llvm-${LLVM_VERSION}/bin:${PATH}"

WORKDIR /app

COPY tr.sh /app/tr.sh

RUN chmod +x /app/tr.sh

CMD [ "/app/tr.sh" ]
