@echo off
rem Same-machine test service: signalling on 127.0.0.1:27200, STUN/TURN on 127.0.0.1:27210.
python "%~dp0coopserver.py" --listen 127.0.0.1:27200 --builtin-turn 127.0.0.1:27210 --relay-ports 27211-27240 --allow-private-peers --local-stats %*
