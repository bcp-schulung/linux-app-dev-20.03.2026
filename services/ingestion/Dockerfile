FROM alpine:3.19 AS builder

RUN apk add --no-cache gcc musl-dev make sqlite-dev

WORKDIR /build

COPY services/shared/ /tmp/shared/
RUN cd /tmp/shared && make clean && make

COPY services/ingestion/ /build/

RUN gcc -Wall -Wextra -O2 -std=c99 -D_POSIX_C_SOURCE=200809L \
    -I/tmp/shared/include \
    -o ingestion ingestion.c \
    -lsqlite3 -L/tmp/shared/lib -lweather

FROM alpine:3.19

RUN apk add --no-cache sqlite-libs && \
    addgroup -g 1000 weather && \
    adduser -u 1000 -G weather -s /bin/sh -D weather

COPY --from=builder /build/ingestion /usr/local/bin/ingestion
COPY --from=builder /build/ingestion.ini /etc/weather-station/ingestion.ini

# Create both data and csv directories
RUN mkdir -p /data/csv /data/processed && \
    chown -R weather:weather /data

USER weather

EXPOSE 8080

VOLUME ["/data"]

ENTRYPOINT ["ingestion"]
CMD ["--config", "/etc/weather-station/ingestion.ini"]
