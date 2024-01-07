#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <math.h>
#include <limits.h>

typedef struct {
    char *file;
    char *host;
    int success;
} URI;

/**
 * @brief Print a usage message to stderr and exit the process with EXIT_FAILURE.
 * @param process The name of the current process.
 */
void usage(const char *process) {
    fprintf(stderr, "[%s] USAGE: %s [-p PORT] [-o FILE | -d DIR] URL\n", process, process);
    exit(EXIT_FAILURE);
}

int parsePort(const char *portStr) {
    char *endptr;
    errno = 0;  // Set errno to 0 before the call to strtol to detect errors properly

    long port = strtol(portStr, &endptr, 10);

    // Check for errors during conversion
    if ((errno == ERANGE && (port == LONG_MAX || port == LONG_MIN)) || (errno != 0 && port == 0)) {
        return -1;  // Error during conversion
    }

    if (endptr == portStr || *endptr != '\0') {
        return -1;
    }

    if ((port < 0) || (port > 65535)) {
        return -1;
    }

    return (int)port;  // Return the parsed port as an integer
}

URI parseUrl(const char *url) {

    URI uri = {
        .file = NULL,
        .host = NULL,
        .success = -1
    };

    if (strncasecmp(url, "http://", 7) != 0) {
        return uri;
    }
    if ((strlen(url) - 7) == 0) {
        return uri;
    }

    char* s = strpbrk(url + 7, ";/?:@=&");
    if (s == NULL) {
        if (asprintf(&uri.file, "/index.html") == -1) {
            return uri;
        }
    } else if (s[0] != '/') {
        if (asprintf(&uri.file, "/%s", s) == -1) {
            return uri;
        }
    } else {
        if (asprintf(&uri.file, "%s", s) == -1) {
            return uri;
        }
    }

    if (asprintf(&uri.host, "%.*s", (int) (strlen(url) - 7 - strlen(uri.file)), (url + 7)) == -1) {
        free(uri.file);
        return uri;
    }

    if (strlen(uri.host) == 0) {
        free(uri.host);
        free(uri.file);
        return uri;
    }


    uri.success = 0;
    return uri;
}

int parseDir(char *dir) {
    if (strspn(dir, "/\\:*?\"<>|") != 0) {
        return -1;
    }
    return 0;
}

int parseFile(char *file) {
    if (strspn(file, "/\\:*?\"<>|") != 0) {
        return -1;
    }
    if (strlen(file) > 255) {
        return -1;
    }
    return 0;
}

int parseResponseCode(char protocol[9], char status[4]) {
    if (strncmp(protocol, "HTTP/1.1", 8) != 0) {
        return 2;
    }

    if (strncmp(status, "200", 3) != 0) {
        return 3;
    }

    return 0;
}

// SYNOPSIS
//       client [-p PORT] [ -o FILE | -d DIR ] URL
// EXAMPLE
//       client http://www.example.com/
int main(int argc, char *argv[]) {
    int port = 80;
    char *path = NULL;
    char *url = NULL;
    URI uri;

    bool portSet = false;
    bool fileSet = false;
    bool dirSet = false;

    int option;
    while ((option = getopt(argc, argv, "p:o:d:")) != -1) {
        switch (option) {
            case 'p':
                if (portSet) {
                    usage(argv[0]);
                }
                portSet = true;
                port = parsePort(optarg);
                if (port == -1) {
                    fprintf(stderr, "An error occurred while parsing the port.\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'o':
                if (dirSet || fileSet) {
                    usage(argv[0]);
                }
                fileSet = true;
                path = optarg;
                break;
            case 'd':
                if (dirSet || fileSet) {
                    usage(argv[0]);
                }
                dirSet = true;
                path = optarg;
                break;
            case '?':
                usage(argv[0]);
                break;
            default:
                assert(0);
        }
    }

    int length = (int) log10(port) + 1;
    char strPort[length + 1];
    snprintf(strPort, sizeof(strPort), "%d", port);

    // what?
    if (argc - optind != 1) {
        usage(argv[0]);
        fprintf(stderr, "URL is missing.\n");
    }

    url = argv[optind];
    uri = parseUrl(url);
    if (uri.success == -1) {
        fprintf(stderr, "An error occurred while parsing the URL.\n");
        exit(EXIT_FAILURE);
    }

    if (dirSet == true) {
        if (parseDir(path) == -1) {
            fprintf(stderr, "An error occurred while parsing the directory.\n");
            exit(EXIT_FAILURE);
        }
    }

    if (fileSet == true) {
        if (parseFile(path) == -1) {
            fprintf(stderr, "An error occurred while parsing the file.\n");
            exit(EXIT_FAILURE);
        }
    }

    // source: https://www.youtube.com/watch?v=MOrvead27B4
    int clientSocket;
    struct addrinfo hints;
    struct addrinfo *results;
    struct addrinfo *record;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    int error;
    if ((error = getaddrinfo(uri.host, strPort, &hints, &results)) != 0) {
        free(uri.host);
        free(uri.file);
        fprintf(stderr, "Failed getting address information.\n");
        exit(EXIT_FAILURE);
    }

    for (record = results; record != NULL; record = record->ai_next) {
        clientSocket = socket(record->ai_family, record->ai_socktype, record->ai_protocol);
        if (clientSocket == -1) continue;
        if (connect(clientSocket, record->ai_addr, record->ai_addrlen) != -1) break;
        close(clientSocket);
    }

    freeaddrinfo(results);
    if (record == NULL) {
        free(uri.host);
        free(uri.file);
        fprintf(stderr, "Failed connecting to server.\n");
        exit(2);
    }


    char *request = NULL;
    asprintf(&request, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", uri.file, uri.host);
    send(clientSocket, request, strlen(request), 0);
    free(request);
    free(uri.host);
    free(uri.file);

    FILE *socketFile = fdopen(clientSocket, "r+");
    if (socketFile == NULL) {
        close(clientSocket);
        fprintf(stderr, "ERROR opening client socket as file.\n");
        exit(EXIT_FAILURE);
    }

    char *line = NULL;
    size_t linelen = 0;
    // case where first line is null
    if (getline(&line, &linelen, socketFile) == -1) {
        close(clientSocket);
        fprintf(stderr, "ERROR parsing first line of client socket as file.\n");
        exit(2);
    }

    char protocol[9];
    char status[4];
    char misc[strlen(line)];

    if (sscanf(line, "%8s %3s %[^\\r\\n]", protocol, status, misc) != 3) {
        close(clientSocket);
        fprintf(stderr, "ERROR parsing first line of client socket as file.\n");
        exit(2);
    }

    int response = parseResponseCode(protocol, status);
    if (response != 0) {
        fprintf(stderr, "%s %s\n", status, misc);
        exit(response);
    }

    FILE *outfile = path == NULL ? stdout : fopen(path, "w");
    if (outfile == NULL)  {
        free(line);
        fclose(socketFile);
        close(clientSocket);
        fprintf(stderr, "ERROR opening output file\n");
        exit(EXIT_FAILURE);
    }

    // skip header
    while (getline(&line, &linelen, socketFile) != -1) {
        // printf("%s", line);
        if (strcmp(line, "\r\n") == 0) {
            break;
        }
    }

    while (getline(&line, &linelen, socketFile) != -1) {
        fprintf(outfile, "%s", line);
    }

    free(line);
    fflush(socketFile);
    fclose(socketFile);
    close(clientSocket);
    // printf("what\n");
    exit(0);
}
