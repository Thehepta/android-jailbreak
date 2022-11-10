#include <string.h>
#include "su_service.h"
#include "su_client.h"


int main(int argc, char *argv[]) {

    if (argc == 2 && strcmp(argv[1], "--daemon") == 0) {
        return service_main();
    }


    return client_main(argc, argv);

}
