FROM alpine:3.19 AS builder

RUN apk add --no-cache gcc musl-dev make sqlite-dev

WORKDIR /build

COPY services/shared/ /tmp/shared/
RUN cd /tmp/shared && make clean && make

COPY services/query/ /build/

RUN gcc -Wall -Wextra -O2 -std=c99 -D_POSIX_C_SOURCE=200809L     -I/tmp/shared/include     -o query query.c     -lsqlite3 -L/tmp/shared/lib -lweather

FROM alpine:3.19

RUN apk add --no-cache sqlite-libs &&     addgroup -g 1000 weather &&     adduser -u 1000 -G weather -s /bin/sh -D weather

COPY --from=builder /build/query /usr/local/bin/query
COPY --from=builder /build/query.ini /etc/weather-station/query.ini

RUN mkdir -p /data && chown -R weather:weather /data

USER weather

EXPOSE 8080

VOLUME ["/data"]

ENTRYPOINT ["query"]
CMD ["--config", "/etc/weather-station/query.ini"]
