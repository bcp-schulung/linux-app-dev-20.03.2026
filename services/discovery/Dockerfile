FROM alpine:3.19 AS builder

RUN apk add --no-cache gcc musl-dev make sqlite-dev

WORKDIR /build

COPY services/shared/ /tmp/shared/
RUN cd /tmp/shared && make clean && make

COPY services/discovery/ /build/

RUN gcc -Wall -Wextra -O2 -std=c99 -D_POSIX_C_SOURCE=200809L     -I/tmp/shared/include     -o discovery discovery.c     -lsqlite3 -L/tmp/shared/lib -lweather

FROM alpine:3.19

RUN apk add --no-cache sqlite-libs &&     addgroup -g 1000 weather &&     adduser -u 1000 -G weather -s /bin/sh -D weather

COPY --from=builder /build/discovery /usr/local/bin/discovery
COPY --from=builder /build/discovery.ini /etc/weather-station/discovery.ini

RUN mkdir -p /data && chown -R weather:weather /data

USER weather

EXPOSE 8080

VOLUME ["/data"]

ENTRYPOINT ["discovery"]
CMD ["--config", "/etc/weather-station/discovery.ini"]
