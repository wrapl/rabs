FROM i386/debian:sid-slim AS build

RUN apt-get update && apt-get install -y \
	build-essential \
	libgc-dev \
	git

RUN apt-get clean

ADD . /tmp/rabs

WORKDIR /tmp/rabs

RUN git submodule init

RUN make clean && make && make install

WORKDIR /

RUN rm -rf /tmp/rabs
