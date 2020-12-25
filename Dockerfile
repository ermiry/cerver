ARG CERVER_VERSION=1.7

FROM gcc as builder

RUN apt-get update && apt-get install -y cmake libssl-dev

# cerver
ARG CERVER_VERSION
RUN mkdir /opt/cerver && cd /opt/cerver \
    && wget -q --no-check-certificate https://github.com/ermiry/cerver/archive/${CERVER_VERSION}.zip \
    && unzip ${CERVER_VERSION}.zip \
    && cd cerver-${CERVER_VERSION} \
    && make DEVELOPMENT='' -j4

############
FROM ubuntu:bionic

ARG RUNTIME_DEPS='libssl-dev'

RUN apt-get update && apt-get install -y ${RUNTIME_DEPS} && apt-get clean

# cerver
ARG CERVER_VERSION
COPY --from=builder /opt/cerver/cerver-${CERVER_VERSION}/bin/libcerver.so /usr/local/lib/
COPY --from=builder /opt/cerver/cerver-${CERVER_VERSION}/include/cerver /usr/local/include/cerver

RUN ldconfig

CMD ["/bin/bash"]