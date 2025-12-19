#include "lyra.h"

void find_and_install_binary(char *extract_dir, char *package_name) {
    char command[1024];
    char binary_path[512];
    FILE *fp;
    
    printf("→ Finding binary...\n");
    
    snprintf(command, sizeof(command), 
             "find %s -type f -executable | head -n 1", extract_dir);
    
    fp = popen(command, "r");
    if (fp == NULL || fgets(binary_path, sizeof(binary_path), fp) == NULL) {
        printf("Error: Could not find binary!\n");
        if (fp) pclose(fp);
        return;
    }
    pclose(fp);
    
    binary_path[strcspn(binary_path, "\n")] = 0;
    printf("Found: %s\n", binary_path);
    
    printf("→ Installing to /usr/local/bin/%s...\n", package_name);
    snprintf(command, sizeof(command), 
             "cp %s /usr/local/bin/%s", binary_path, package_name);
    system(command);
    
    snprintf(command, sizeof(command), 
             "chmod +x /usr/local/bin/%s", package_name);
    system(command);
    
    printf("Done! Installed to /usr/local/bin/%s\n", package_name);
}

void install_package_with_mirror(char *package_name, char *url) {
    char download_path[512];
    char extract_dir[512];
    char command[1024];
    char version[64];
    char installed_path[512];
    
    char old_version[256] = "";
    char old_url[1024] = "";
    int has_old_version = 0;
    int is_mirror = 0;
    
    db_init();
    
    printf("Installing %s...\n", package_name);
    
    if (strcmp(url, "mirror") == 0) {
        is_mirror = 1;
        printf("→ Using Lyra mirror for installation\n");
    } else {
        extract_version_from_url(url, version);
    }
    
    snprintf(installed_path, sizeof(installed_path), "/usr/local/bin/%s", package_name);
    if (access(installed_path, F_OK) == 0) {
        cJSON *root = db_read();
        cJSON *pkg = cJSON_GetObjectItem(root, package_name);
        if (pkg) {
            cJSON *current_ver = cJSON_GetObjectItem(pkg, "version");
            cJSON *current_url_obj = cJSON_GetObjectItem(pkg, "url");
            
            if (current_ver && current_ver->valuestring) {
                strncpy(old_version, current_ver->valuestring, 255);
                old_version[255] = '\0';
                has_old_version = 1;
                
                printf("→ Found existing version: %s\n", old_version);
                printf("→ Auto-muting and backing up to vault...\n");
            }
            
            if (current_url_obj && current_url_obj->valuestring) {
                strncpy(old_url, current_url_obj->valuestring, 1023);
                old_url[1023] = '\0';
            }
        }
        cJSON_Delete(root);
        
        if (has_old_version) {
            backup_to_vault(package_name, old_version);
        }
    }
    
    snprintf(download_path, sizeof(download_path), "/tmp/%s.tar.gz", package_name);
    snprintf(extract_dir, sizeof(extract_dir), "/tmp/%s_extracted", package_name);
    
    if (is_mirror) {
        if (!download_from_mirror(package_name, download_path)) {
            printf("Error: Failed to download from mirror\n");
            return;
        }
        strcpy(version, "latest");
    } else {
        printf("→ Downloading version %s...\n", version);
        snprintf(command, sizeof(command), "curl -L -o %s %s 2>/dev/null", download_path, url);
        system(command);
    }
    
    snprintf(command, sizeof(command), "mkdir -p %s", extract_dir);
    system(command);
    
    printf("→ Extracting...\n");
    snprintf(command, sizeof(command), "tar -xzf %s -C %s 2>/dev/null", download_path, extract_dir);
    system(command);
    
    if (is_mirror) {
        apply_install_rules(package_name, extract_dir);
    } else {
        find_and_install_binary(extract_dir, package_name);
    }
    
    if (has_old_version) {
        cJSON *root = db_read();
        cJSON *pkg = cJSON_GetObjectItem(root, package_name);
        
        if (pkg) {
            cJSON *versions = cJSON_GetObjectItem(pkg, "versions");
            if (!versions) {
                versions = cJSON_CreateArray();
                cJSON_AddItemToObject(pkg, "versions", versions);
            }
            
            cJSON *ver_entry = cJSON_CreateObject();
            cJSON_AddStringToObject(ver_entry, "version", old_version);
            cJSON_AddStringToObject(ver_entry, "url", old_url);
            cJSON_AddStringToObject(ver_entry, "status", "muted");
            
            time_t now = time(NULL);
            char timestamp[64];
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", localtime(&now));
            cJSON_AddStringToObject(ver_entry, "installed_date", timestamp);
            
            cJSON_AddItemToArray(versions, ver_entry);
            
            cJSON_ReplaceItemInObject(pkg, "version", cJSON_CreateString(version));
            
            if (is_mirror) {
                cJSON_ReplaceItemInObject(pkg, "url", cJSON_CreateString("mirror"));
                cJSON_ReplaceItemInObject(pkg, "source", cJSON_CreateString("mirror"));
            } else {
                cJSON_ReplaceItemInObject(pkg, "url", cJSON_CreateString(url));
            }
            
            db_write(root);
        }
        cJSON_Delete(root);
    } else {
        db_add_package(package_name, version, is_mirror ? "mirror" : url);
    }
    
    printf("→ Added to database\n");
    
    remove(download_path);
    snprintf(command, sizeof(command), "rm -rf %s", extract_dir);
    system(command);
}

void remove_package(char *package_name) {
    char path[512];
    char command[1024];
    
    snprintf(path, sizeof(path), "/usr/local/bin/%s", package_name);
    
    if (access(path, F_OK) != 0) {
        printf("Error: Package '%s' is not installed\n", package_name);
        return;
    }
    
    printf("→ Removing %s...\n", package_name);
    
    snprintf(command, sizeof(command), "rm %s", path);
    
    if (system(command) == 0) {
        printf("Done! Removed %s\n", package_name);
        printf("→ Vault copy preserved for future restoration\n");
        
        db_remove_package(package_name);
        printf("→ Removed from database\n");
    } else {
        printf("Error: Failed to remove %s\n", package_name);
    }
}

void remove_package_completely(char *package_name) {
    char path[512];
    char command[1024];
    
    snprintf(path, sizeof(path), "/usr/local/bin/%s", package_name);
    
    if (access(path, F_OK) != 0) {
        printf("Error: Package '%s' is not installed\n", package_name);
        return;
    }
    
    printf("→ Completely removing %s (including vault and frozen copies)...\n", package_name);
    
    snprintf(command, sizeof(command), "rm %s", path);
    
    if (system(command) == 0) {
        printf("Done! Removed %s\n", package_name);
        
        char *home = get_user_home();
        char vault_dir[512];
        char frozen_dir[512];
        
        snprintf(vault_dir, sizeof(vault_dir), "%s/.lyra/vault/%s", home, package_name);
        snprintf(command, sizeof(command), "rm -rf %s", vault_dir);
        system(command);
        
        snprintf(frozen_dir, sizeof(frozen_dir), "%s/.lyra/vault/frozen/%s", home, package_name);
        snprintf(command, sizeof(command), "rm -rf %s", frozen_dir);
        system(command);
        
        db_remove_package(package_name);
        printf("→ Removed from database, vault, and frozen copies\n");
    } else {
        printf("Error: Failed to remove %s\n", package_name);
    }
}
