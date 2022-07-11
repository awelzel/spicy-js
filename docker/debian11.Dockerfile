FROM debian:11
RUN apt-get update
RUN apt-get install -y --no-install-recommends \
  ca-certificates \
  curl \
  g++ \
  gpg \
  make \
  node-gyp \
  nodejs \
  npm \
  zlib1g-dev

ENV OS=linux_debian11
ENV SPICY_VERSION=1.5.0
WORKDIR /opt/spicy
RUN curl -L -vf -o spicy_v${SPICY_VERSION}.deb https://github.com/zeek/spicy/releases/download/v${SPICY_VERSION}/spicy_${OS}.deb
# Would be nice to verify a SHA256
RUN dpkg -i spicy_v${SPICY_VERSION}.deb
ENV PATH=/opt/spicy/bin:$PATH

# https://github.com/nodesource/distributions/blob/master/README.md#manual-installation
ENV NODE_VERSION=18.x
ENV KEYRING=/usr/share/keyrings/nodesource.gpg
ENV DISTRO=bullseye
RUN curl -fsSL https://deb.nodesource.com/gpgkey/nodesource.gpg.key | gpg --dearmor | tee "$KEYRING" >/dev/null
RUN echo "deb [signed-by=$KEYRING] https://deb.nodesource.com/node_${NODE_VERSION} ${DISTRO} main" | tee /etc/apt/sources.list.d/nodesource.list
RUN echo "deb-src [signed-by=$KEYRING] https://deb.nodesource.com/node_${NODE_VERSION} ${DISTRO} main" | tee -a /etc/apt/sources.list.d/nodesource.list

RUN apt-get update && apt-get install -y --no-install-recommends nodejs

# Can not use the one from Debian, use npm...
RUN npm install -g node-gyp@v9.0.0

# Up until here is a test image, next parts are actually compiling,
# building and testing.

# Pre-compile Spicy parsers for nicer Dockerfile caching.
WORKDIR /src
COPY ./parsers ./parsers
WORKDIR /src/parsers
RUN spicyc -j http.spicy -o http.hlto

# Install dependencies and build spicy-js
WORKDIR /src
COPY . .
RUN npm --dev install

# Smoke test
RUN npx jasmine ./tests/test_http.js
