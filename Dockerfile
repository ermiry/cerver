FROM gcc as builder
# RUN apt-get install -y libssl-dev
COPY . .
RUN make -j4
RUN make examples

FROM ubuntu
WORKDIR /home/cerver
# RUN apt-get update && apt-get install -y libssl-dev
COPY --from=builder ./bin/libcerver.so .
COPY --from=builder ./examples/bin/test .

ENV LD_LIBRARY_PATH=/home/cerver/
ENTRYPOINT ["./test"]