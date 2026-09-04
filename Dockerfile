# syntax=docker/dockerfile:1.7
FROM --platform=$TARGETPLATFORM debian:bookworm-slim AS build

ARG BUILDARCH
ARG TARGETARCH
RUN apt-get update && apt-get install -y --no-install-recommends cmake g++ make \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /build --parallel \
    && if [ "$BUILDARCH" = "$TARGETARCH" ]; then \
         ctest --test-dir /build --output-on-failure; \
       else \
         /build/dlinject_tests --unit; \
       fi

FROM --platform=$TARGETPLATFORM debian:bookworm-slim
COPY --from=build /build/dlinject /usr/local/bin/dlinject
COPY --from=build /build/dlinject-target /opt/dlinject/dlinject-target
COPY --from=build /build/dlinject-example.so /opt/dlinject/dlinject-example.so
COPY demo/container.sh /opt/dlinject/demo
CMD ["/opt/dlinject/demo"]
