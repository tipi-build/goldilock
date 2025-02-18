FROM alpine:3.21
COPY goldilock /usr/bin/goldilock
COPY support_app_append_to_file /usr/bin/support_app_append_to_file
COPY support_app_deleter /usr/bin/support_app_deleter

# used for a test case
RUN apk add parallel
ENTRYPOINT ["/bin/sh", "-c"]