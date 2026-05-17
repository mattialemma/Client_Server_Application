#include "client/client.h"

#include <stdio.h>
#include <stdlib.h>

// Punto di ingresso del client: richiede host/porta e avvia client_run().
int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <host> <porta>\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (client_run(argv[1], argv[2]) != 0) {
        fprintf(stderr, "Client terminato con errore\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
