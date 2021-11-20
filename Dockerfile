FROM debian:stable-slim
COPY . /
ENTRYPOINT [ "/usr/local/bin/housesprinkler" ]

