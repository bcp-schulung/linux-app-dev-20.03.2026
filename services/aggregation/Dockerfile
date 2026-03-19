FROM alpine:3.19 AS builder

RUN apk add --no-cache \
    gcc \
    musl-dev \
    make \
    sqlite-dev \
    curl-dev

WORKDIR /build

COPY services/shared/ /tmp/shared/
RUN cd /tmp/shared && make clean && make

COPY services/aggregation/ /build/

RUN gcc -Wall -Wextra -O2 -std=c99 -D_POSIX_C_SOURCE=200809L \
    -I/tmp/shared/include \
    -o aggregation aggregation.c \
    -lsqlite3 -lcurl -L/tmp/shared/lib -lweather

FROM alpine:3.19

RUN apk add --no-cache \
    sqlite-libs \
    curl && \
    addgroup -g 1000 weather && \
    adduser -u 1000 -G weather -s /bin/sh -D weather

COPY --from=builder /build/aggregation /usr/local/bin/aggregation
COPY --from=builder /build/aggregation.ini /etc/weather-station/aggregation.ini

RUN mkdir -p /data && chown -R weather:weather /data

USER weather

EXPOSE 8080

VOLUME ["/data"]

ENTRYPOINT ["aggregation"]
CMD ["--config", "/etc/weather-station/aggregation.ini"]
