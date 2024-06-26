FROM amazonlinux:2

ARG TOOLCHAIN_VERSION

RUN yum -y update \
    && yum install -y wget git tar
# Do NOT be smart here and clean the cache because the container is used in the
# stateful context.

# Download and install toolchain
RUN wget -q https://s3-eu-west-1.amazonaws.com/deps.memgraph.io/toolchain-${TOOLCHAIN_VERSION}/toolchain-${TOOLCHAIN_VERSION}-binaries-amzn-2-x86_64.tar.gz \
    -O toolchain-${TOOLCHAIN_VERSION}-binaries-amzn-2-x86_64.tar.gz \
    && tar xzvf toolchain-${TOOLCHAIN_VERSION}-binaries-amzn-2-x86_64.tar.gz -C /opt \
    && rm toolchain-${TOOLCHAIN_VERSION}-binaries-amzn-2-x86_64.tar.gz

# Install toolchain run deps and memgraph build deps
SHELL ["/bin/bash", "-c"]
RUN git clone https://github.com/memgraph/memgraph.git \
    && cd memgraph \
    && ./environment/os/amzn-2.sh install TOOLCHAIN_RUN_DEPS \
    && ./environment/os/amzn-2.sh install MEMGRAPH_BUILD_DEPS \
    && cd .. && rm -rf memgraph

# Add mgdeps-cache and bench-graph-api hostnames
RUN echo -e "10.42.16.10     mgdeps-cache\n10.42.16.10     bench-graph-api" >> /etc/hosts

# Create mg user and set as default
RUN useradd -m -s /bin/bash mg
USER mg

# Install rust
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y

# Fix node
RUN curl https://raw.githubusercontent.com/creationix/nvm/master/install.sh | bash

# Install PyYAML (only for amzn-2, centos-7, cento-9 and rocky-9)
RUN pip3 install --user PyYAML

ENTRYPOINT ["sleep", "infinity"]
