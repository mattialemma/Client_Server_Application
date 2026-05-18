#include "common/utils.h"
#include "common/net.h"
#include "server/logger.h"
#include "server/server.h"

#include <stdio.h>
#include <stdlib.h>

// Punto di ingresso del server: valida gli argomenti e avvia server_run().
int main(int argc, char **argv) {
    int ok;
    long duration = 300;
    long period = 5;
    const char *log_path = getenv("SERVER_LOG_PATH");

    if (log_path == NULL || log_path[0] == '\0') {
        log_path = "server.log";
    }

    if (server_log_init(log_path) != 0) {
        fprintf(stderr, "Impossibile aprire il file di log: %s\n", log_path);
        return EXIT_FAILURE;
    }

    if (argc < 2 || argc > 4) {
        server_log_error("uso non valido: %s <porta> [durata_secondi (default 300)] [periodo_secondi (default 5)]", argv[0]);
        server_log_close();
        return EXIT_FAILURE;
    }
    if (argc >= 3) {
        duration = utils_parse_long(argv[2], 10, 86400, &ok);
        if (!ok) {
            server_log_error("durata non valida: %s", argv[2]);
            server_log_close();
            return EXIT_FAILURE;
        }
    }
    if (argc == 4) {
        period = utils_parse_long(argv[3], 1, 3600, &ok);
        if (!ok) {
            server_log_error("periodo non valido: %s", argv[3]);
            server_log_close();
            return EXIT_FAILURE;
        }
    }
   
    if (server_run(argv[1], (int)duration, (int)period) != 0) {
        server_log_error("terminazione server con errore: %s", net_last_error());
        server_log_close();
        return EXIT_FAILURE;
    }
    server_log_close();
    return EXIT_SUCCESS;
}
