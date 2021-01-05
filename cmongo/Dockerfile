FROM gcc as builder

RUN apt-get update && apt-get install -y libssl-dev

# build cerver with production flags
WORKDIR /opt/cerver
COPY ./src .
COPY ./include .
COPY makefile .
RUN make TYPE=production -j4

############
FROM ermiry/cmongo:latest

# cerver files
COPY --from=builder /opt/cerver/bin/libcerver.so /usr/local/lib/
COPY --from=builder /opt/cerver/include/cerver /usr/local/include/cerver

RUN ldconfig

CMD ["/bin/bash"]
