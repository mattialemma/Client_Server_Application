# Territory Conquest TCP

Componenti del gruppo: Carmine Sgariglia.

## 1. Guida d'uso

Il progetto implementa un gioco multiutente di conquista territorio. Un server TCP gestisce la partita e piu client testuali si connettono da terminale.

Compilazione:

```sh
make
```

Avvio server:

```sh
./bin/server 4242 300 5
```

Avvio client:

```sh
./bin/client 127.0.0.1 4242
```

Il server accetta tre parametri: porta, durata della partita in secondi e periodo degli aggiornamenti globali. Il client richiede host e porta.

Comandi client:

```txt
register <nickname> <password>
login <nickname> <password>
move up|down|left|right
w a s d
users
local
global
help
quit
```

## 2. Requisiti e assunzioni

Requisiti funzionali:

- comunicazione TCP client-server;
- piu client concorrenti;
- registrazione e login;
- mappa a griglia con celle libere e muri;
- proprieta della cella assegnata all'ultimo giocatore che la attraversa;
- ostacoli nascosti ai client finche non sono scoperti personalmente;
- proprieta delle celle pubblica;
- invio locale dopo ogni movimento;
- invio globale periodico;
- lista utenti collegati;
- disconnessione;
- timeout globale;
- vincitore determinato dal numero di celle possedute.

Requisiti non funzionali:

- C/POSIX su Linux;
- nessuna libreria esterna;
- server senza uso di `stdin` e senza scritture su `stdout`;
- esecuzione nativa e Docker;
- codice modulare e documentato.

Assunzioni conservative:

- gli utenti sono mantenuti in memoria per la durata del processo;
- password in chiaro, scelta didattica per evitare dipendenze crittografiche non richieste;
- mappa 30x15 scelta casualmente fra layout statici compilati nel server;
- utenti, sessioni client e giocatori sono allocati dinamicamente, senza un limite applicativo prefissato;
- la mappa locale inviata al client e una finestra 11x11 centrata sul giocatore;
- spawn casuale su una cella libera e non occupata;
- gli slot giocatore restano associati al nickname, evitando che celle gia conquistate cambino proprietario quando uno slot viene riutilizzato;
- ogni giocatore riceve un identificatore stabile (`P0`, `P1`, ...) usato nelle mappe;
- utenti, sessioni e giocatori crescono con array dinamici; usando `select(2)`, i socket sono comunque vincolati da `FD_SETSIZE` e dai limiti del sistema operativo;
- la visibilita dei muri e personale;
- la mappa globale non rivela muri, ma solo proprieta;
- in caso di tentativo di movimento contro un muro, il giocatore resta fermo e il muro viene marcato come scoperto.

## 3. Architettura

La repository e divisa in tre aree:

- `src/common`: funzioni condivise di rete, protocollo e utilita;
- `src/server`: stato utenti, stato gioco e loop server;
- `src/client`: connessione, ciclo eventi e rendering testuale.

Il server e single-process e usa `select(2)` per multiplexare socket di ascolto e socket client. Questa scelta evita lock, race condition e sincronizzazione fra thread, ed e adatta a un progetto universitario basato su primitive POSIX classiche.

Il client usa anch'esso `select(2)` su socket e `stdin`, permettendo di ricevere aggiornamenti asincroni mentre l'utente digita comandi.

## 4. Protocollo applicativo

Il protocollo e testuale a righe terminate da `\n`. Ogni messaggio ha un tipo e zero o piu argomenti separati da spazi. I payload non usano spazi: mappe, posizioni e punteggi sono codificati con separatori `/`, `,` e `:`.

Messaggi client-server:

```txt
C2S_REGISTER <nickname> <password>
C2S_LOGIN <nickname> <password>
C2S_MOVE UP|DOWN|LEFT|RIGHT
C2S_LIST_USERS
C2S_LOCAL_MAP
C2S_GLOBAL_MAP
C2S_QUIT
```

Messaggi server-client:

```txt
S2C_OK <dettaglio>
S2C_ERR <codice>
S2C_LOCAL_MAP <w> <h> <mappa>
S2C_GLOBAL_UPDATE <w> <h> <mappa_proprieta> <posizioni>
S2C_USERS <posizioni>
S2C_GAME_OVER <winner> <score> <punteggi>
```

In caso di login riuscito, il dettaglio `S2C_OK LOGGED_IN <nickname> <player_id> <x> <y>` comunica al client il nickname autenticato, l'identificatore assegnato al giocatore e la posizione iniziale.

## 5. Dettagli implementativi rilevanti

Il server mantiene:

- database utenti in memoria con array dinamico;
- array dinamico di sessioni client;
- array dinamico di giocatori, indicizzati dalla mappa di proprieta;
- matrice dei muri;
- matrice globale della proprieta;
- matrice personale dei muri scoperti per ciascun giocatore;
- timer per timeout partita e aggiornamenti periodici.

La funzione `game_init` sceglie casualmente uno dei layout statici disponibili. La funzione `game_move` valida i confini, controlla i muri, aggiorna posizione e proprieta e richiama la rivelazione locale degli ostacoli adiacenti. La mappa locale inviata dopo login, movimento o richiesta esplicita e una finestra 11x11 centrata sul giocatore, contenente solo i muri scoperti da quel giocatore e le proprieta pubbliche nelle celle della finestra.

Le funzioni che costruiscono mappe, posizioni e punteggi controllano la dimensione dei buffer e segnalano errore se il payload non entra nella riga di protocollo. In questo modo il server evita troncamenti silenziosi e puo rispondere con `S2C_ERR ENCODING_FAILED`.

La funzione `server_run` costruisce il set di descrittori per `select`, accetta nuove connessioni, legge messaggi completi dai client e verifica periodicamente i timer.

Il client interpreta i comandi utente e li traduce nel protocollo applicativo. Gli aggiornamenti ricevuti dal server sono renderizzati direttamente su terminale.

### Tabella di conformita sintetica

| Requisito | Modulo principale | Verifica |
| --- | --- | --- |
| TCP client-server | `src/common/net.c`, `src/server/server.c`, `src/client/client.c` | avvio server e client |
| Multi-client concorrente | `src/server/server.c` | due o piu client collegati insieme |
| Registrazione/login | `src/server/users.c` | `register`, `login` |
| Ownership celle | `src/server/game.c` | movimento e mappa globale |
| Ostacoli privati | `src/server/game.c` | mappa locale con `#` |
| Update periodico | `src/server/server.c` | attesa periodo `T` |
| Timeout e vincitore | `src/server/server.c`, `src/server/game.c` | partita breve |
| Docker | `Dockerfile`, `compose.yaml` | `docker compose up/run` |

## 6. Docker

Il `Dockerfile` installa `build-essential`, copia il progetto e compila con `make`.

Con Compose:

```sh
docker compose up --build server
docker compose run --rm client
```

Il servizio `client` si collega al server usando il nome DNS Docker `server`.

## 7. Verifica manuale

La verifica prevista e manuale: compilazione, avvio server, connessione di uno o piu client, registrazione, login, movimento, ostacoli, aggiornamenti globali, lista utenti, timeout partita e Docker.

## 8. Limiti e possibili estensioni

Limiti:

- utenti non persistenti su file;
- password non cifrate;
- dimensione mappa compilata nel codice;
- dimensione finestra locale compilata nel codice;
- percentuale ostacoli compilata nel codice;
- invio sincrono verso client, adeguato a messaggi piccoli e progetto didattico.

Estensioni:

- salvataggio utenti su file;
- mappa configurabile;
- gestione di pareggi esplicita;
- protocollo binario a lunghezza prefissata;
- script di dimostrazione per avviare piu client con comandi predefiniti.
