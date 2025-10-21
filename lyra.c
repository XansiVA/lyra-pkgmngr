#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void find_and_install_binary(char *extract_dir, char *package_name) {
    char command[1024];
    char binary_path[512];
    FILE *fp;
    
    printf("→ Finding binary...\n");
    
    snprintf(command, sizeof(command), 
             "find %s -type f -executable | head -n 1", extract_dir);
    
    fp = popen(command, "r");
    if (fp == NULL || fgets(binary_path, sizeof(binary_path), fp) == NULL) {
        printf("Could not find binary!\n");
        if (fp) pclose(fp);
        return;
    }
    pclose(fp);
    
    binary_path[strcspn(binary_path, "\n")] = 0;
    printf("Found: %s\n", binary_path);
    
    printf("→ Installing to /usr/local/bin/%s (need sudo)...\n", package_name);
    snprintf(command, sizeof(command), 
             "sudo cp %s /usr/local/bin/%s", binary_path, package_name);
    system(command);
    
    snprintf(command, sizeof(command), 
             "sudo chmod +x /usr/local/bin/%s", package_name);
    system(command);
    
    printf("Installed! Try: %s --version\n", package_name);
}

void install_package(char *package_name, char *url) {
    char download_path[512];
    char extract_dir[512];
    char command[1024];
    
    printf("Installing %s...\n", package_name);
    
    snprintf(download_path, sizeof(download_path), "/tmp/%s.tar.gz", package_name);
    snprintf(extract_dir, sizeof(extract_dir), "/tmp/%s_extracted", package_name);
    
    printf("→ Downloading...\n");
    snprintf(command, sizeof(command), "curl -L -o %s %s", download_path, url);
    system(command);
    
    snprintf(command, sizeof(command), "mkdir -p %s", extract_dir);
    system(command);
    
    printf("→ Extracting...\n");
    snprintf(command, sizeof(command), "tar -xzf %s -C %s", download_path, extract_dir);
    system(command);
    
    find_and_install_binary(extract_dir, package_name);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: lyra -i <package> <url>\n");
        return 1;
    }
    
    if (strcmp(argv[1], "-i") == 0) {
        if (argc < 4) {
            printf("Usage: lyra -i <package> <url>\n");
            return 1;
        }
        install_package(argv[2], argv[3]);
    }
    else {
        printf("Unknown command: %s\n", argv[1]);
    }
    
    return 0;
}