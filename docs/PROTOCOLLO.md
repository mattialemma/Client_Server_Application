# Protocollo applicativo

Il protocollo e testuale, orientato a righe e trasportato su TCP.

## Framing

Ogni messaggio e una riga ASCII terminata da `\n`.

```txt
TIPO argomento1 argomento2 ...
```

I token non contengono spazi. Nickname e password accettano lettere, cifre, `_` e `-`, con lunghezza massima 31 caratteri.

Le mappe sono codificate come celle separate da `,` e righe separate da `/`. Esempio 3x2:

```txt
P0,.,#/.,P1,.
```

## Client verso server

```txt
C2S_REGISTER <nickname> <password>
C2S_LOGIN <nickname> <password>
C2S_MOVE UP|DOWN|LEFT|RIGHT
C2S_LIST_USERS
C2S_LOCAL_MAP
C2S_GLOBAL_MAP
C2S_QUIT
```

## Server verso client

```txt
S2C_OK <dettaglio>
S2C_ERR <codice>
S2C_LOCAL_MAP <w> <h> <mappa_locale>
S2C_GLOBAL_UPDATE <w> <h> <mappa_proprieta> <posizioni>
S2C_USERS <posizioni>
S2C_GAME_OVER <winner> <score> <punteggi>
```

Dettagli `S2C_OK` usati dal server:

```txt
S2C_OK CONNECTED
S2C_OK REGISTERED
S2C_OK LOGGED_IN <nickname> <player_id> <x> <y>
S2C_OK MOVED <x> <y>
S2C_OK BYE
```

## Codici errore principali

```txt
SERVER_FULL
BAD_SYNTAX
INVALID_CREDENTIALS
USER_EXISTS
USER_DB_FULL
AUTH_FAILED
ALREADY_AUTHENTICATED
USER_ALREADY_ONLINE
GAME_FULL
NOT_AUTHENTICATED
BAD_DIRECTION
OUT_OF_BOUNDS
WALL
OCCUPIED
MOVE_FAILED
UNKNOWN_COMMAND
LINE_TOO_LONG
```

## Semantica mappe

Mappa locale:

- `@`: posizione del giocatore.
- `#`: muro scoperto dal giocatore.
- `.`: cella libera senza proprietario noto.
- `P<n>`: proprietario della cella. Il server assegna un identificatore stabile a ogni slot giocatore.
- rappresenta una finestra centrata sul giocatore, attualmente 11x11.
- le celle fuori dai confini della griglia sono codificate come `~`, per evitare spazi nel payload.

Mappa globale:

- contiene solo informazioni pubbliche di proprieta.
- gli ostacoli non sono mostrati.

Posizioni:

```txt
nickname:player_id:x:y,nickname:player_id:x:y
```

Punteggi:

```txt
nickname:celle,nickname:celle
```
