#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int is_cygwin() {
#ifdef _WIN32
    // On Windows, use system command to check for Cygwin
    FILE *fp = popen("uname -s", "r");
    if (fp == NULL) {
        return 0;
    }

    char buffer[128];
    if (fgets(buffer, sizeof(buffer), fp) != NULL) {
        pclose(fp);
        // Check if the output contains "CYGWIN"
        if (strstr(buffer, "CYGWIN") != NULL) {
            return 1;  // Cygwin environment detected
        }
    }

    pclose(fp);
    return 0;  // Not Cygwin
#else
    // On Unix-like systems, check uname directly
    FILE *fp = popen("uname -s", "r");
    if (fp == NULL) {
        return 0;
    }

    char buffer[128];
    if (fgets(buffer, sizeof(buffer), fp) != NULL) {
        
        // Check if the output contains "CYGWIN"
        if (strstr(buffer, "CYGWIN") != NULL) {
            return 1;  // Cygwin environment detected
        }
    }

    pclose(fp);
    return 0;  // Not Cygwin
#endif
}

int main() {
    if (is_cygwin()) {
        printf("Cygwin environment detected.\n");
    } else {
        printf("Not running in a Cygwin environment.\n");
    }
    return 0;
}
