# kotor-coop join-code and signalling service. Standard library only.
FROM python:3.12-alpine
RUN adduser -D -H -u 10001 coop
WORKDIR /app
COPY joincode.py turnrelay.py coopserver.py ./
USER coop
ENV PYTHONUNBUFFERED=1 COOP_LISTEN=0.0.0.0:27200
EXPOSE 27200/tcp
HEALTHCHECK --interval=60s --timeout=5s CMD python -c "import socket,json;s=socket.create_connection(('127.0.0.1',27200),3);s.sendall(b'{\"op\":\"ping\"}\n');assert json.loads(s.makefile().readline())['op']=='pong'"
ENTRYPOINT ["python", "/app/coopserver.py"]
