FROM alpine:latest

RUN apk add --no-cache alpine-sdk gc-dev sqlite-dev

ADD . /tmp/rabs

WORKDIR /tmp/rabs

RUN git submodule init

RUN make

RUN make install

WORKDIR /

RUN rm -rf /tmp/rabs
