# OpenZiti: dark admin services

[OpenZiti](https://github.com/JebetrexxMC/ziti) makes a service reachable only for enrolled identities, with no open inbound port. Use it for the parts of an MP box that players never need:

| service | box port | who |
|---|---|---|
| `mp-ssh` | 22 | you |
| `mp-mqtt-admin` | 1883 (Mosquitto / EMQX dashboard 18083) | you |
| `mp-status` | mpnetd :8090, Scanopy :60072 | team |

The game ports (7777 UDP, the crossplay port) stay public. Players connect with the normal client, which has no Ziti inside.

`services.sh` creates these services on a Ziti controller. The controller and the router come from the fork's quickstart (`quickstart/docker/docker-compose.yml`).

```
# controller + router (quickstart)
curl -fsSLO https://raw.githubusercontent.com/JebetrexxMC/ziti/main/quickstart/docker/simplified-docker-compose.yml
docker compose -f simplified-docker-compose.yml up -d
# then, with the ziti CLI logged in:
bash net/ziti/services.sh <router-name> <your-identity-name>
```

`services.sh` prints an enrolment JWT for your identity. Enrol it in the Ziti Desktop Edge or Mobile Edge app, and `mp-ssh.ziti` resolves on your PC or phone only.
