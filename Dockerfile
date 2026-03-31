FROM ubuntu:22.04

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    g++ \
    git \
    python3 \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

# Install Python packages
RUN pip3 install --no-cache-dir \
    numpy \
    scipy \
    matplotlib \
    pyyaml \
    pandas

# Copy the repository contents
COPY . /workspace/segfold

WORKDIR /workspace/segfold

# Build the C++ simulator
RUN cd csegfold \
    && mkdir -p build \
    && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_RAMULATOR2=OFF \
    && make -j$(nproc)

# Ensure the output directory exists
RUN mkdir -p /workspace/segfold/output

ENTRYPOINT ["/bin/bash"]

CMD ["-c", "echo 'SegFold Artifact Evaluation Container'; echo ''; echo 'Usage:'; echo '  Interactive shell:'; echo '    docker compose run artifact'; echo ''; echo '  Run all experiments:'; echo '    docker compose run artifact ./scripts/run_all.sh'; echo ''; echo '  Run specific experiment:'; echo '    docker compose run artifact ./scripts/run_synthetic.sh'; echo '    docker compose run artifact ./scripts/run_suitesparse.sh'; echo ''; echo '  Results will be written to the ./output/ directory on the host.'"]
