FROM alpine:3.20

# git for syncing, ca-certificates for HTTPS clone, busybox provides httpd.
RUN apk add --no-cache git ca-certificates busybox

COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

EXPOSE 8080
ENTRYPOINT ["/entrypoint.sh"]
