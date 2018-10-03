FROM alpine:latest

RUN apk add --no-cache alpine-sdk gc-dev sqlite-dev

RUN git clone --recursive -b dev https://github.com/wrapl/rabs /tmp/rabs

WORKDIR /tmp/rabs

RUN ls -lah

RUN make

RUN make install

RUN rm -rf /tmp/rabs
