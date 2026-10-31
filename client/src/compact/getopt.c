// NOLINTBEGIN
// clang-format off
#include "getopt.h"

#include <string.h>
#include <stdio.h>

char *optarg = NULL;
int   optind = 1;
int   opterr = 1;
int   optopt = '?';

int getopt_long(int argc, char *const argv[], const char *optstring,
                const struct option *longopts, int *longindex) {
    if (optind >= argc || argv[optind] == NULL) {
        return -1;
    }

    char *arg = argv[optind];

    // Check if it's a long option option variant (--option)
    if (arg[0] == '-' && arg[1] == '-') {
        if (arg[2] == '\0') { // Handle explicit '--' to stop parsing options
            optind++;
            return -1;
        }

        const char *name_start = arg + 2;
        const char *equal_sign = strchr(name_start, '=');
        size_t name_len = equal_sign ? (size_t)(equal_sign - name_start) : strlen(name_start);

        for (int i = 0; longopts[i].name != NULL; i++) {
            if (strncmp(longopts[i].name, name_start, name_len) == 0 && longopts[i].name[name_len] == '\0') {
                if (longindex) *longindex = i;
                optind++;

                if (longopts[i].has_arg == required_argument) {
                    if (equal_sign) {
                        optarg = (char *)(equal_sign + 1);
                    } else {
                        if (optind >= argc) {
                            if (opterr && optstring[0] != ':') {
                                fprintf(stderr, "%s: option '--%s' requires an argument\n", argv[0], longopts[i].name);
                            }
                            return '?';
                        }
                        optarg = argv[optind++];
                    }
                } else {
                    if (equal_sign) {
                        if (opterr && optstring[0] != ':') {
                            fprintf(stderr, "%s: option '--%s' doesn't allow an argument\n", argv[0], longopts[i].name);
                        }
                        return '?';
                    }
                    optarg = NULL;
                }

                if (longopts[i].flag) {
                    *longopts[i].flag = longopts[i].val;
                    return 0;
                }
                return longopts[i].val;
            }
        }

        if (opterr && optstring[0] != ':') {
            fprintf(stderr, "%s: unrecognized option '%s'\n", argv[0], arg);
        }
        optind++;
        return '?';
    }

    // Fall back to a standard processing sequence for short options (-s)
    if (arg[0] == '-' && arg[1] != '\0') {
        char opt = arg[1];
        const char *oli = strchr(optstring, opt);
 
        if (!oli) {
            if (opterr && optstring[0] != ':') {
                fprintf(stderr, "%s: invalid option -- '%c'\n", argv[0], opt);
            }
            optopt = opt;
            optind++;
            return '?';
        }

        optind++;
        if (oli[1] == ':') { // The option requires an argument
            if (arg[2] != '\0') { // Glued flag argument like -p8080
                optarg = arg + 2;
            } else {
                if (optind >= argc) {
                    if (opterr && optstring[0] != ':') {
                        fprintf(stderr, "%s: option requires an argument -- '%c'\n", argv[0], opt);
                    }
                    optopt = opt;
                    return (optstring[0] == ':') ? ':' : '?';
                }
                optarg = argv[optind++];
            }
        } else {
            optarg = NULL;
        }
        return opt;
    }

    return -1; // Found a non-option operand argument
}
// NOLINTEND
