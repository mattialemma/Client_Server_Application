#include "common/utils.h"
#include "common/net.h"
#include "server/server.h"

#include <stdio.h>
#include <stdlib.h>

// Punto di ingresso del server: valida gli argomenti e avvia server_run().
int main(int argc, char **argv) {
    int ok;
    long duration = 300;
    long period = 5;

    if (argc < 2 || argc > 4) {
        fprintf(stderr, "uso non valido: %s <porta> [durata_secondi (default 300)] [periodo_secondi (default 5)]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (argc >= 3) {
        duration = utils_parse_long(argv[2], 10, 86400, &ok);
        if (!ok) {
            fprintf(stderr, "durata non valida: %s\n", argv[2]);
            return EXIT_FAILURE;
        }
    }
    if (argc == 4) {
        period = utils_parse_long(argv[3], 1, 3600, &ok);
        if (!ok) {
            fprintf(stderr, "periodo non valido: %s\n", argv[3]);
            return EXIT_FAILURE;
        }
    }
   
    if (server_run(argv[1], (int)duration, (int)period) != 0) {
        fprintf(stderr, "terminazione server con errore: %s\n", net_last_error());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
