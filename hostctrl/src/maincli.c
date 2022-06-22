#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include "./electronctrl.h"


static void printUsage(int argc, char* argv[]) {
    printf("Usage: %s [OPTIONS] [COMMANDS]\n", argv[0]);

    printf("\nSupported OPTIONS:\n");
    printf("\t-port [FILENAME]\n\t\tSpecifies the serial port device\n");

    printf("\nSupported COMMANDS:\n");
    printf("\tid\n\t\tGet board ID and version\n");

    printf("\toff\n\t\tDisable all powersupplies\n");

    printf("\tcatgetv\n\t\tGet cathode voltage\n");
    printf("\tcatgeta\n\t\tGet cathode current\n");
    printf("\tcatgetpol\n\t\tGet polarity of cathode\n");

    printf("\tcatsetv N\n\t\tSet cathode target voltage\n");
    printf("\tcatseta N\n\t\tSet cathode current limit\n");
    printf("\tcatsetpol <pos/neg>\n\t\tSet polarity of cathode\n");

    printf("\twhegetv\n\t\tGet Whenelt cylinder voltage\n");
    printf("\twhegeta\n\t\tGet Whenelt cylinder current\n");
    printf("\twhegetpol\n\t\tGet Whenelt cylinder polarity\n");

    printf("\twhesetv N\n\t\tSet Whenelt cylinder target voltage\n");
    printf("\twheseta N\n\t\tSet Whenelt cylinder current limit\n");
    printf("\twhesetpol <pos/neg>\n\t\tSet Whenelt cylinder polarity\n");

    printf("\tfocgetv\n\t\tGet focus voltage\n");
    printf("\tfocgeta\n\t\tGet focus current\n");
    printf("\tfocgetpol\n\t\tGet focus polarity\n");

    printf("\tfocsetv N\n\t\tSet focus target voltage\n");
    printf("\tfocseta N\n\t\tSet focus current limit\n");
    printf("\tfocsetpol <pos/neg>\n\t\tSet focus polarity\n");

    printf("\t4getv\n\t\tGet voltage of PSU 4\n");
    printf("\t4geta\n\t\tGet current of PSU 4\n");
    printf("\t4getpol\n\t\tGet polarity of PSU 4\n");

    printf("\t4setv N\n\t\tSet target voltage of PSU 4\n");
    printf("\t4seta N\n\t\tSet current limit of PSU 4\n");
    printf("\t4setpol <pos/neg>\n\t\tSet polarity of PSU 4\n");

    printf("\tfilgeta\n\t\tGet filament current\n");
    printf("\tfilseta N\n\t\tSet filament current\n");
    printf("\tfilon\n\t\tFilament on\n");
    printf("\tfiloff\n\t\tFilament off\n");

    printf("\tinsul\n\t\tRun HV insulation test\n");
    printf("\tbeamon\n\t\tRun beam on sequence (note: Filament current specified before)\n");

    printf("\tsleep [N]\n\t\tSleep the specified number of seconds before running next command");

    printf("\tmodes\n\t\tGet PSU modes (constant voltage / constant current)\n");

    printf("\n\n_Note:_ Reconnecting resets the state of the controller so it's not possible to\nrun a script updating values in a loop without starting from 0 again - one has\nto keep the port open for this (network or UDS service)\n");
}

enum egunCliCommand {
    egunCliCommand_ID               = 0,
};

int main(int argc, char* argv[]) {
    struct electronGun* lpEgun;
    enum egunError e;
    unsigned long int dwTemp;

    char* lpPort = NULL;

    if(argc < 1) {
        printUsage(argc, argv);
        return 1;
    }

    /* "Parse" CLI arguments ... */
    {
        unsigned long int i;
        for(i = 1; i < argc; i = i + 1) {
            if(strcmp(argv[i], "-port") == 0) {
                if(i+1 >= argc) {
                    printf("Missing port argument\n");
                    printUsage(argc, argv);
                    return 1;
                }
                lpPort = argv[i+1];
                i = i + 1;
                continue;
            } else if(strcmp(argv[i], "id") == 0) {
            } else if(strcmp(argv[i], "off") == 0) {
            } else if(strcmp(argv[i], "noprotection") == 0) {
            } else if(strcmp(argv[i], "catgetv") == 0) {
            } else if(strcmp(argv[i], "catgeta") == 0) {
            } else if(strcmp(argv[i], "catgetpol") == 0) {
            } else if(strcmp(argv[i], "catsetv") == 0) {
                if(i == argc-1) {
                    printf("Missing cathode voltage argument after catsetv\n");
                    return 1;
                }
                if(sscanf(argv[i+1], "%lu", &dwTemp) != 1) {
                    printf("Invalid voltage %s after catsetv\n", argv[i+1]);
                    return 1;
                }
                if(dwTemp > 2200) {
                    printf("Invalid voltage %s after catsetv\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
            } else if(strcmp(argv[i], "catseta") == 0) {
                if(i == argc-1) {
                    printf("Missing cathode current argument after catseta\n");
                    return 1;
                }
                if(sscanf(argv[i+1], "%lu", &dwTemp) != 1) {
                    printf("Invalid current %s after catseta\n", argv[i+1]);
                    return 1;
                }
                if(dwTemp > 1000) {
                    printf("Invalid current %s after catseta\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
            } else if(strcmp(argv[i], "catsetpol") == 0) {
                if(i == argc-1) {
                    printf("Missing cathode polarity after catsetpol\n");
                    return 1;
                }
                if((strcmp(argv[i+1], "pos") != 0) && (strcmp(argv[i+1], "neg") != 0)) {
                    printf("Invalid polarity %s after catsetpol\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
            } else if(strcmp(argv[i], "whegetv") == 0) {
            } else if(strcmp(argv[i], "whegeta") == 0) {
            } else if(strcmp(argv[i], "whegetpol") == 0) {
            } else if(strcmp(argv[i], "whesetv") == 0) {
                if(i == argc-1) {
                    printf("Missing Whenelt cylinder voltage argument after whesetv\n");
                    return 1;
                }
                if(sscanf(argv[i+1], "%lu", &dwTemp) != 1) {
                    printf("Invalid voltage %s after whesetv\n", argv[i+1]);
                    return 1;
                }
                if(dwTemp > 2200) {
                    printf("Invalid voltage %s after whesetv\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
            } else if(strcmp(argv[i], "wheseta") == 0) {
                if(i == argc-1) {
                    printf("Missing Whenelt cylinder current argument after wheseta\n");
                    return 1;
                }
                if(sscanf(argv[i+1], "%lu", &dwTemp) != 1) {
                    printf("Invalid current %s after wheseta\n", argv[i+1]);
                    return 1;
                }
                if(dwTemp > 1000) {
                    printf("Invalid current %s after wheseta\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
            } else if(strcmp(argv[i], "whesetpol") == 0) {
                if(i == argc-1) {
                    printf("Missing Whenelt polarity after whesetpol\n");
                    return 1;
                }
                if((strcmp(argv[i+1], "pos") != 0) && (strcmp(argv[i+1], "neg") != 0)) {
                    printf("Invalid polarity %s after whesetpol\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
            } else if(strcmp(argv[i], "focgetv") == 0) {
            } else if(strcmp(argv[i], "focgeta") == 0) {
            } else if(strcmp(argv[i], "focgetpol") == 0) {
            } else if(strcmp(argv[i], "focsetv") == 0) {
                if(i == argc-1) {
                    printf("Missing focus grid voltage argument after focsetv\n");
                    return 1;
                }
                if(sscanf(argv[i+1], "%lu", &dwTemp) != 1) {
                    printf("Invalid voltage %s after focsetv\n", argv[i+1]);
                    return 1;
                }
                if(dwTemp > 2200) {
                    printf("Invalid voltage %s after focsetv\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
            } else if(strcmp(argv[i], "focseta") == 0) {
                if(i == argc-1) {
                    printf("Missing focus grid current argument after focseta\n");
                    return 1;
                }
                if(sscanf(argv[i+1], "%lu", &dwTemp) != 1) {
                    printf("Invalid current %s after focseta\n", argv[i+1]);
                    return 1;
                }
                if(dwTemp > 1000) {
                    printf("Invalid current %s after focseta\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
            } else if(strcmp(argv[i], "focsetpol") == 0) {
                if(i == argc-1) {
                    printf("Missing focus grid polarity after focsetpol\n");
                    return 1;
                }
                if((strcmp(argv[i+1], "pos") != 0) && (strcmp(argv[i+1], "neg") != 0)) {
                    printf("Invalid polarity %s after focsetpol\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
            } else if(strcmp(argv[i], "4getv") == 0) {
            } else if(strcmp(argv[i], "4geta") == 0) {
            } else if(strcmp(argv[i], "4getpol") == 0) {
            } else if(strcmp(argv[i], "4setv") == 0) {
                if(i == argc-1) {
                    printf("Missing power supply 4 voltage argument after 4setv\n");
                    return 1;
                }
                if(sscanf(argv[i+1], "%lu", &dwTemp) != 1) {
                    printf("Invalid voltage %s after 4setv\n", argv[i+1]);
                    return 1;
                }
                if(dwTemp > 2200) {
                    printf("Invalid voltage %s after rsetv\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
            } else if(strcmp(argv[i], "4seta") == 0) {
                if(i == argc-1) {
                    printf("Missing power supply 4 current argument after 4seta\n");
                    return 1;
                }
                if(sscanf(argv[i+1], "%lu", &dwTemp) != 1) {
                    printf("Invalid current %s after 4seta\n", argv[i+1]);
                    return 1;
                }
                if(dwTemp > 1000) {
                    printf("Invalid current %s after 4seta\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
            } else if(strcmp(argv[i], "4setpol") == 0) {
                if(i == argc-1) {
                    printf("Missing power supply 4 polarity after 4setpol\n");
                    return 1;
                }
                if((strcmp(argv[i+1], "pos") != 0) && (strcmp(argv[i+1], "neg") != 0)) {
                    printf("Invalid polarity %s after 4setpol\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
            } else if(strcmp(argv[i], "filgeta") == 0) {
            } else if(strcmp(argv[i], "filseta") == 0) {
                if(i == argc-1) {
                    printf("Missing filament current after filseta\n");
                    return 1;
                }
                if(sscanf(argv[i+1], "%lu", &dwTemp) != 1) {
                    printf("Invalid current %s after filseta\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
            } else if(strcmp(argv[i], "filon") == 0) {
            } else if(strcmp(argv[i], "filoff") == 0) {
            } else if(strcmp(argv[i], "insul") == 0) {
            } else if(strcmp(argv[i], "beamon") == 0) {
            } else if(strcmp(argv[i], "sleep") == 0) {
                if(i == argc-1) {
                    printf("Missing time after sleep\n");
                    return 1;
                }
                if(sscanf(argv[i+1], "%lu", &dwTemp) != 1) {
                    printf("Invalid time %s after sleep\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
            } else if(strcmp(argv[i], "modes") == 0) {
            } else {
                printf("Unknown command %s\n\n", argv[i]);
                printUsage(argc, argv);
                return 1;
            }
        }
    }

    e = egunConnect_Serial(&lpEgun, lpPort);
    if(e != egunE_Ok) {
        printf("%s:%u Failed to connect on port %s (%u)\n", __FILE__, __LINE__, (lpPort == NULL) ? "default" : lpPort, e);
        return 1;
    }

    /* Execute CLI commands */
    {
        unsigned long int i;
        for(i = 1; i < argc; i = i + 1) {
            if(strcmp(argv[i], "-port") == 0) { i = i + 1;
            } else if(strcmp(argv[i], "id") == 0) {
                printf("Requesting ID\n");
                lpEgun->vtbl->requestId(lpEgun);
            } else if(strcmp(argv[i], "off") == 0) {
                printf("Disabling supplies\n");
                lpEgun->vtbl->off(lpEgun);
            } else if(strcmp(argv[i], "noprotection") == 0) {
                printf("Disabling overcurrent protection\n");
                lpEgun->vtbl->noProtection(lpEgun);
            } else if(strcmp(argv[i], "catgetv") == 0) {
                printf("Requesting cathode voltage\n");
                lpEgun->vtbl->getCurrentVoltage(lpEgun, 1);
            } else if(strcmp(argv[i], "catgeta") == 0) {
                printf("Requesting cathode current\n");
                lpEgun->vtbl->getCurrentCurrent(lpEgun, 1);
            } else if(strcmp(argv[i], "catsetv") == 0) {
                if(i == argc-1) {
                    printf("Missing cathode voltage argument after catsetv\n");
                    return 1;
                }
                if(sscanf(argv[i+1], "%lu", &dwTemp) != 1) {
                    printf("Invalid voltage %s after catsetv\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
                printf("Setting cathode voltage to %lu\n", dwTemp);
                lpEgun->vtbl->setVoltage(lpEgun, 1, dwTemp);
            } else if(strcmp(argv[i], "catseta") == 0) {
                if(i == argc-1) {
                    printf("Missing cathode current argument after catseta\n");
                    return 1;
                }
                if(sscanf(argv[i+1], "%lu", &dwTemp) != 1) {
                    printf("Invalid current %s after catseta\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
                printf("Setting cathode current to %lu\n", dwTemp);
                lpEgun->vtbl->setCurrent(lpEgun, 1, dwTemp);
            } else if(strcmp(argv[i], "catsetpol") == 0) {
                if(i == argc-1) {
                    printf("Missing cathode polarity after catsetpol\n");
                    return 1;
                }
                i = i + 1;
                printf("Setting cathode polarity to %s\n", argv[i]);
                lpEgun->vtbl->setPSUPolarity(lpEgun, 1, (strcmp(argv[i], "pos") == 0) ? egunPolarity_Pos : egunPolarity_Neg);
            } else if(strcmp(argv[i], "whegetv") == 0) {
                printf("Requesting Whenelt cylinder voltage\n");
                lpEgun->vtbl->getCurrentVoltage(lpEgun, 2);
            } else if(strcmp(argv[i], "whegeta") == 0) {
                printf("Requesting Whenelt cylinder current\n");
                lpEgun->vtbl->getCurrentCurrent(lpEgun, 2);
            } else if(strcmp(argv[i], "whesetv") == 0) {
                if(i == argc-1) {
                    printf("Missing Whenelt cylinder voltage argument after whesetv\n");
                    return 1;
                }
                if(sscanf(argv[i+1], "%lu", &dwTemp) != 1) {
                    printf("Invalid voltage %s after whesetv\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
                printf("Setting Whenelt cylinder voltage to %lu\n", dwTemp);
                lpEgun->vtbl->setVoltage(lpEgun, 2, dwTemp);
            } else if(strcmp(argv[i], "wheseta") == 0) {
                if(i == argc-1) {
                    printf("Missing Whenelt cylinder current argument after wheseta\n");
                    return 1;
                }
                if(sscanf(argv[i+1], "%lu", &dwTemp) != 1) {
                    printf("Invalid current %s after wheseta\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
                printf("Setting Whenelt cylinder current limit to %lu\n", dwTemp);
                lpEgun->vtbl->setCurrent(lpEgun, 2, dwTemp);
            } else if(strcmp(argv[i], "whesetpol") == 0) {
                if(i == argc-1) {
                    printf("Missing Whenelt polarity after whesetpol\n");
                    return 1;
                }
                i = i + 1;
                printf("Setting Whenelt cylinder polarity to %s\n", argv[i]);
                lpEgun->vtbl->setPSUPolarity(lpEgun, 2, (strcmp(argv[i], "pos") == 0) ? egunPolarity_Pos : egunPolarity_Neg);
            } else if(strcmp(argv[i], "focgetv") == 0) {
                printf("Requesting focus voltage\n");
                lpEgun->vtbl->getCurrentVoltage(lpEgun, 3);
            } else if(strcmp(argv[i], "focgeta") == 0) {
                printf("Requesting focus current\n");
                lpEgun->vtbl->getCurrentCurrent(lpEgun, 3);
            } else if(strcmp(argv[i], "focsetv") == 0) {
                if(i == argc-1) {
                    printf("Missing focus grid voltage argument after focsetv\n");
                    return 1;
                }
                if(sscanf(argv[i+1], "%lu", &dwTemp) != 1) {
                    printf("Invalid voltage %s after focsetv\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
                printf("Setting focus voltage to %lu\n", dwTemp);
                lpEgun->vtbl->setVoltage(lpEgun, 3, dwTemp);
            } else if(strcmp(argv[i], "focseta") == 0) {
                if(i == argc-1) {
                    printf("Missing focus grid current argument after focseta\n");
                    return 1;
                }
                if(sscanf(argv[i+1], "%lu", &dwTemp) != 1) {
                    printf("Invalid current %s after focseta\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
                printf("Setting focus current limit to %lu\n", dwTemp);
                lpEgun->vtbl->setCurrent(lpEgun, 3, dwTemp);
            } else if(strcmp(argv[i], "focsetpol") == 0) {
                if(i == argc-1) {
                    printf("Missing focus grid polarity after focsetpol\n");
                    return 1;
                }
                i = i + 1;
                printf("Setting focus polarity to %s\n", argv[i]);
                lpEgun->vtbl->setPSUPolarity(lpEgun, 3, (strcmp(argv[i], "pos") == 0) ? egunPolarity_Pos : egunPolarity_Neg);
            } else if(strcmp(argv[i], "4getv") == 0) {
                printf("Requesting PSU4 voltage\n");
                lpEgun->vtbl->getCurrentVoltage(lpEgun, 4);
            } else if(strcmp(argv[i], "4geta") == 0) {
                printf("Requesting PSU4 current\n");
                lpEgun->vtbl->getCurrentCurrent(lpEgun, 4);
            } else if(strcmp(argv[i], "4setv") == 0) {
                if(i == argc-1) {
                    printf("Missing power supply 4 voltage argument after 4setv\n");
                    return 1;
                }
                if(sscanf(argv[i+1], "%lu", &dwTemp) != 1) {
                    printf("Invalid voltage %s after 4setv\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
                printf("Setting PSU4 voltage to %lu\n", dwTemp);
                lpEgun->vtbl->setVoltage(lpEgun, 4, dwTemp);
            } else if(strcmp(argv[i], "4seta") == 0) {
                if(i == argc-1) {
                    printf("Missing power supply 4 current argument after 4seta\n");
                    return 1;
                }
                if(sscanf(argv[i+1], "%lu", &dwTemp) != 1) {
                    printf("Invalid current %s after 4seta\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
                printf("Setting PSU4 current limit to %lu\n", dwTemp);
                lpEgun->vtbl->setCurrent(lpEgun, 4, dwTemp);
            } else if(strcmp(argv[i], "4setpol") == 0) {
                if(i == argc-1) {
                    printf("Missing power supply 4 polarity after 4setpol\n");
                    return 1;
                }
                i = i + 1;
                printf("Setting PSU4 polarity to %s\n", argv[i]);
                lpEgun->vtbl->setPSUPolarity(lpEgun, 4, (strcmp(argv[i], "pos") == 0) ? egunPolarity_Pos : egunPolarity_Neg);
            } else if(strcmp(argv[i], "filgeta") == 0) {
                printf("Requesting filament current\n");
                lpEgun->vtbl->getFilamentCurrent(lpEgun);
            } else if(strcmp(argv[i], "filseta") == 0) {
                if(i == argc-1) {
                    printf("Missing filament current after filseta\n");
                    return 1;
                }
                if(sscanf(argv[i+1], "%lu", &dwTemp) != 1) {
                    printf("Invalid current %s after filseta\n", argv[i+1]);
                    return 1;
                }
                i = i + 1;
                printf("Setting filament current to %lu\n", dwTemp);
                lpEgun->vtbl->setFilamentCurrent(lpEgun, dwTemp);
            } else if(strcmp(argv[i], "filon") == 0) {
                printf("Filament on\n");
                lpEgun->vtbl->setFilamentOn(lpEgun, true);
            } else if(strcmp(argv[i], "filoff") == 0) {
                printf("Filament off\n");
                lpEgun->vtbl->setFilamentOn(lpEgun, false);
            } else if(strcmp(argv[i], "insul") == 0) {
                printf("Running insulation test\n");
                lpEgun->vtbl->insulationTest(lpEgun);
                sleep(135); /* ToDo: Wait on "insulok" or error message instead of sleeping a measured amount of time */
            } else if(strcmp(argv[i], "beamon") == 0) {
                printf("Enabling beam\n");
                lpEgun->vtbl->beamOn(lpEgun);
                sleep(525); /* ToDo: Wait on "beamon" or error message instead of sleeping a measured amount of time */
            } else if(strcmp(argv[i], "sleep") == 0) {
                if(i == argc-1) {
                    printf("Missing time after sleep\n");
                    return 1;
                }
                if(sscanf(argv[i+1], "%lu", &dwTemp) != 1) {
                    printf("Invalid time %s after sleep\n", argv[i+1]);
                    return 1;
                }
                printf("Sleeping for %lu seconds\n", dwTemp);
                sleep(dwTemp);
                i = i + 1;
            } else if(strcmp(argv[i], "modes") != 0) {
                printf("Requesting PSU modes\n");
                lpEgun->vtbl->getPSUModes(lpEgun);
            } else {
                printf("Unknown command %s\n\n", argv[i]);
                printUsage(argc, argv);
                return 1;
            }
        }
    }

    e = lpEgun->vtbl->release(lpEgun);
    #ifdef DEBUG
        printf("%s:%u Release returned %u (%s)\n", __FILE__, __LINE__, e, (e == egunE_Ok) ? "ok" : "failed");
    #endif
    return 0;
}
