//(C) Lyra Package Manager Developers - Licensed under MIT License
//Owned by Xansi - chaoscatsofficial@gmail.com, https://github.com/XansiVA

//Cjson library is used for JSON handling (MIT License) 
//Owned by Dave Gamble - https://github.com/DaveGamble

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <cjson/cJSON.h>

// Database functions
void db_init() {
    char db_path[512];
    char *home = getenv("HOME");
    
    // Create directories
    char path[512];
    snprintf(path, sizeof(path), "%s/.lyra", home);
    mkdir(path, 0755);
    
    snprintf(path, sizeof(path), "%s/.lyra/vault", home);
    mkdir(path, 0755);
    
    // Create database file path
    snprintf(db_path, sizeof(db_path), "%s/.lyra/active_packages.json", home);
    
    // Check if exists
    if (access(db_path, F_OK) == 0) {
        return;  // Already exists
    }
    
    // Create empty JSON object
    cJSON *root = cJSON_CreateObject();
    char *json_str = cJSON_Print(root);
    
    // Write to file
    FILE *fp = fopen(db_path, "w");
    if (fp != NULL) {
        fprintf(fp, "%s", json_str);
        fclose(fp);
    }
    
    free(json_str);
    cJSON_Delete(root);
}

cJSON* db_read() {
    char db_path[512];
    char *home = getenv("HOME");
    snprintf(db_path, sizeof(db_path), "%s/.lyra/active_packages.json", home);
    
    // Read file
    FILE *fp = fopen(db_path, "r");
    if (fp == NULL) {
        return cJSON_CreateObject();  // Return empty if doesn't exist
    }
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    // Read content
    char *content = malloc(size + 1);
    fread(content, 1, size, fp);
    content[size] = '\0';
    fclose(fp);
    
    // Parse JSON
    cJSON *root = cJSON_Parse(content);
    free(content);
    
    if (root == NULL) {
        return cJSON_CreateObject();
    }
    
    return root;
}

void db_write(cJSON *root) {
    char db_path[512];
    char *home = getenv("HOME");
    snprintf(db_path, sizeof(db_path), "%s/.lyra/active_packages.json", home);
    
    // Convert to string (formatted nicely)
    char *json_str = cJSON_Print(root);
    
    if (json_str == NULL) {
        // If printing failed, write empty object
        FILE *fp = fopen(db_path, "w");
        if (fp != NULL) {
            fprintf(fp, "{\n}\n");
            fclose(fp);
        }
        return;
    }
    
    // Write to file
    FILE *fp = fopen(db_path, "w");
    if (fp != NULL) {
        fprintf(fp, "%s\n", json_str);
        fclose(fp);
    }
    
    free(json_str);
}

// Simplified - only for NEW packages (not upgrades)
void db_add_package(char *name, char *version, char *url) {
    cJSON *root = db_read();
    
    // Only for NEW packages (not upgrades)
    cJSON *package = cJSON_CreateObject();
    cJSON_AddStringToObject(package, "version", version);
    cJSON_AddStringToObject(package, "url", url);
    
    char install_path[512];
    snprintf(install_path, sizeof(install_path), "/usr/local/bin/%s", name);
    cJSON_AddStringToObject(package, "installed_path", install_path);
    cJSON_AddStringToObject(package, "status", "active");
    
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", localtime(&now));
    cJSON_AddStringToObject(package, "installed_date", timestamp);
    
    // Empty versions array
    cJSON *versions = cJSON_CreateArray();
    cJSON_AddItemToObject(package, "versions", versions);
    
    cJSON_AddItemToObject(root, name, package);
    
    db_write(root);
    cJSON_Delete(root);
}

void db_remove_package(char *name) {
    cJSON *root = db_read();
    cJSON_DeleteItemFromObject(root, name);
    db_write(root);
    cJSON_Delete(root);
}

void db_list_packages() {
    cJSON *root = db_read();
    
    printf("Installed packages:\n");
    printf("------------------\n");
    
    int count = 0;
    cJSON *package = NULL;
    cJSON_ArrayForEach(package, root) {
        char *name = package->string;
        cJSON *version = cJSON_GetObjectItem(package, "version");
        cJSON *versions = cJSON_GetObjectItem(package, "versions");
        
        printf("  %s", name);
        if (version) printf(" (%s - active)", version->valuestring);
        
        // Show muted versions
        if (versions && cJSON_GetArraySize(versions) > 0) {
            printf(" [muted: ");
            int first = 1;
            cJSON *ver = NULL;
            cJSON_ArrayForEach(ver, versions) {
                cJSON *v = cJSON_GetObjectItem(ver, "version");
                if (v) {
                    if (!first) printf(", ");
                    printf("%s", v->valuestring);
                    first = 0;
                }
            }
            printf("]");
        }
        printf("\n");
        
        count++;
    }
    
    if (count == 0) {
        printf("  (no packages installed)\n");
    } else {
        printf("\nTotal: %d packages\n", count);
    }
    
    cJSON_Delete(root);
}

// Extract version from URL
void extract_version_from_url(char *url, char *version_out) {
    char temp[64] = "unknown";
    
    // Look for version after "releases/download/"
    char *ptr = strstr(url, "releases/download/");
    if (ptr) {
        ptr += strlen("releases/download/");
        
        // Skip 'v' if present
        if (*ptr == 'v') ptr++;
        
        // Extract version (numbers and dots only)
        int i = 0;
        while (i < 63 && ((*ptr >= '0' && *ptr <= '9') || *ptr == '.')) {
            temp[i++] = *ptr++;
        }
        temp[i] = '\0';
        
        // Remove trailing dots
        while (i > 0 && temp[i-1] == '.') {
            temp[--i] = '\0';
        }
        
        if (i > 0) {
            strcpy(version_out, temp);
            return;
        }
    }
    
    strcpy(version_out, "unknown");
}

// Backup current version to vault
void backup_to_vault(char *package_name, char *version) {
    char *home = getenv("HOME");
    char vault_dir[512];
    char version_dir[512];
    char source[512];
    char dest[512];
    char command[1024];
    
    // Create vault structure
    snprintf(vault_dir, sizeof(vault_dir), "%s/.lyra/vault/%s", home, package_name);
    mkdir(vault_dir, 0755);
    
    snprintf(version_dir, sizeof(version_dir), "%s/%s", vault_dir, version);
    mkdir(version_dir, 0755);
    
    // Copy binary to vault
    snprintf(source, sizeof(source), "/usr/local/bin/%s", package_name);
    snprintf(dest, sizeof(dest), "%s/%s", version_dir, package_name);
    
    snprintf(command, sizeof(command), "cp %s %s", source, dest);
    system(command);
    
    printf("→ Backed up %s (%s) to vault\n", package_name, version);
}

// Find and install binary
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
    
    printf("→ Installing to /usr/local/bin/%s (need sudo)...\n", package_name);
    snprintf(command, sizeof(command), 
             "sudo cp %s /usr/local/bin/%s", binary_path, package_name);
    system(command);
    
    snprintf(command, sizeof(command), 
             "sudo chmod +x /usr/local/bin/%s", package_name);
    system(command);
    
    printf("Done! Installed to /usr/local/bin/%s\n", package_name);
}

// Install package from URL - FIXED VERSION
void install_package(char *package_name, char *url) {
    char download_path[512];
    char extract_dir[512];
    char command[1024];
    char version[64];
    char installed_path[512];
    
    // Storage for old version info (copied EARLY before any modifications)
    char old_version[256] = "";
    char old_url[1024] = "";
    int has_old_version = 0;
    
    db_init();
    
    printf("Installing %s...\n", package_name);
    
    // Extract version from URL
    extract_version_from_url(url, version);
    
    // Check if package already exists AND get old version info NOW
    snprintf(installed_path, sizeof(installed_path), "/usr/local/bin/%s", package_name);
    if (access(installed_path, F_OK) == 0) {
        // Read database and COPY old version info immediately
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
        
        // Backup to vault if we found an old version
        if (has_old_version) {
            backup_to_vault(package_name, old_version);
        }
    }
    
    // Download and install new version
    snprintf(download_path, sizeof(download_path), "/tmp/%s.tar.gz", package_name);
    snprintf(extract_dir, sizeof(extract_dir), "/tmp/%s_extracted", package_name);
    
    printf("→ Downloading version %s...\n", version);
    snprintf(command, sizeof(command), "curl -L -o %s %s 2>/dev/null", download_path, url);
    system(command);
    
    snprintf(command, sizeof(command), "mkdir -p %s", extract_dir);
    system(command);
    
    printf("→ Extracting...\n");
    snprintf(command, sizeof(command), "tar -xzf %s -C %s 2>/dev/null", download_path, extract_dir);
    system(command);
    
    find_and_install_binary(extract_dir, package_name);
    
    // Update database
    if (has_old_version) {
        // Upgrade existing package
        cJSON *root = db_read();
        cJSON *pkg = cJSON_GetObjectItem(root, package_name);
        
        if (pkg) {
            // Get or create versions array
            cJSON *versions = cJSON_GetObjectItem(pkg, "versions");
            if (!versions) {
                versions = cJSON_CreateArray();
                cJSON_AddItemToObject(pkg, "versions", versions);
            }
            
            // Add old version to muted list
            cJSON *ver_entry = cJSON_CreateObject();
            cJSON_AddStringToObject(ver_entry, "version", old_version);
            cJSON_AddStringToObject(ver_entry, "url", old_url);
            cJSON_AddStringToObject(ver_entry, "status", "muted");
            
            time_t now = time(NULL);
            char timestamp[64];
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", localtime(&now));
            cJSON_AddStringToObject(ver_entry, "installed_date", timestamp);
            
            cJSON_AddItemToArray(versions, ver_entry);
            
            // Update to new version
            cJSON_ReplaceItemInObject(pkg, "version", cJSON_CreateString(version));
            cJSON_ReplaceItemInObject(pkg, "url", cJSON_CreateString(url));
            
            db_write(root);
        }
        cJSON_Delete(root);
    } else {
        // Brand new package
        db_add_package(package_name, version, url);
    }
    
    printf("→ Added to database\n");
    
    // Cleanup
    remove(download_path);
    snprintf(command, sizeof(command), "rm -rf %s", extract_dir);
    system(command);
}

// Mute current version and activate a muted one
void mute_package(char *package_name) {
    cJSON *root = db_read();
    cJSON *pkg = cJSON_GetObjectItem(root, package_name);
    
    if (!pkg) {
        printf("Error: Package '%s' not found\n", package_name);
        cJSON_Delete(root);
        return;
    }
    
    cJSON *versions = cJSON_GetObjectItem(pkg, "versions");
    if (!versions || cJSON_GetArraySize(versions) == 0) {
        printf("Error: No muted versions available for '%s'\n", package_name);
        cJSON_Delete(root);
        return;
    }
    
    // Get current active version - COPY IT IMMEDIATELY
    cJSON *current_ver = cJSON_GetObjectItem(pkg, "version");
    if (!current_ver || !current_ver->valuestring) {
        printf("Error: Could not determine current version\n");
        cJSON_Delete(root);
        return;
    }
    
    char active_version[256];
    strncpy(active_version, current_ver->valuestring, 255);
    active_version[255] = '\0';
    
    // Get first muted version - COPY IT IMMEDIATELY
    cJSON *first_muted = cJSON_GetArrayItem(versions, 0);
    cJSON *target_ver = cJSON_GetObjectItem(first_muted, "version");
    
    if (!target_ver || !target_ver->valuestring) {
        printf("Error: Invalid muted version data\n");
        cJSON_Delete(root);
        return;
    }
    
    char target_version[256];
    strncpy(target_version, target_ver->valuestring, 255);
    target_version[255] = '\0';
    
    printf("→ Switching from %s to %s\n", active_version, target_version);
    
    // Backup current active to vault
    backup_to_vault(package_name, active_version);
    
    // Restore target version from vault
    char *home = getenv("HOME");
    char vault_path[512];
    char dest_path[512];
    char command[1024];
    
    snprintf(vault_path, sizeof(vault_path), "%s/.lyra/vault/%s/%s/%s", 
             home, package_name, target_version, package_name);
    snprintf(dest_path, sizeof(dest_path), "/usr/local/bin/%s", package_name);
    
    if (access(vault_path, F_OK) != 0) {
        printf("Error: Muted version not found in vault\n");
        cJSON_Delete(root);
        return;
    }
    
    snprintf(command, sizeof(command), "sudo cp %s %s", vault_path, dest_path);
    system(command);
    snprintf(command, sizeof(command), "sudo chmod +x %s", dest_path);
    system(command);
    
    // Update database - swap versions
    cJSON *target_url = cJSON_GetObjectItem(first_muted, "url");
    cJSON_ReplaceItemInObject(pkg, "version", cJSON_CreateString(target_version));
    if (target_url) {
        cJSON_ReplaceItemInObject(pkg, "url", cJSON_CreateString(target_url->valuestring));
    }
    
    // Remove from muted array
    cJSON_DetachItemFromArray(versions, 0);
    
    // Add old active to muted array
    cJSON *new_muted = cJSON_CreateObject();
    cJSON_AddStringToObject(new_muted, "version", active_version);
    cJSON *old_url = cJSON_GetObjectItem(pkg, "url");
    if (old_url) {
        cJSON_AddStringToObject(new_muted, "url", old_url->valuestring);
    }
    cJSON_AddItemToArray(versions, new_muted);
    
    db_write(root);
    cJSON_Delete(root);
    
    printf("Done! Now using %s version %s\n", package_name, target_version);
}

// Remove package
void remove_package(char *package_name) {
    char path[512];
    char command[1024];
    
    snprintf(path, sizeof(path), "/usr/local/bin/%s", package_name);
    
    if (access(path, F_OK) != 0) {
        printf("Error: Package '%s' is not installed\n", package_name);
        return;
    }
    
    printf("→ Removing %s...\n", package_name);
    
    snprintf(command, sizeof(command), "sudo rm %s", path);
    
    if (system(command) == 0) {
        printf("Done! Removed %s\n", package_name);
        
        // Remove vault directory
        char *home = getenv("HOME");
        char vault_dir[512];
        snprintf(vault_dir, sizeof(vault_dir), "%s/.lyra/vault/%s", home, package_name);
        snprintf(command, sizeof(command), "rm -rf %s", vault_dir);
        system(command);
        
        db_remove_package(package_name);
        printf("→ Removed from database and vault\n");
    } else {
        printf("Error: Failed to remove %s\n", package_name);
    }
}

// Main
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Lyra Package Manager v0.2\n\n");
        printf("Usage:\n");
        printf("  lyra -i <package> <url>               Install package (auto-mutes old version)\n");
        printf("  lyra -rmpkg <pkg1> [pkg2] [pkg3]...  Remove one or more packages\n");
        printf("  lyra -list                            List installed packages\n");
        printf("  lyra -m <package>                     Mute current, switch to muted version\n");
        return 1;
    }
    
    if (strcmp(argv[1], "-i") == 0) {
        if (argc < 4) {
            printf("Usage: lyra -i <package> <url>\n");
            return 1;
        }
        install_package(argv[2], argv[3]);
    }
    else if (strcmp(argv[1], "-rmpkg") == 0) {
        if (argc < 3) {
            printf("Usage: lyra -rmpkg <package1> [package2] ...\n");
            return 1;
        }
        for (int i = 2; i < argc; i++) {
            remove_package(argv[i]);
        }
    }
    else if (strcmp(argv[1], "-list") == 0) {
        db_init();
        db_list_packages();
    }
    else if (strcmp(argv[1], "-m") == 0) {
        if (argc < 3) {
            printf("Usage: lyra -m <package>\n");
            return 1;
        }
        mute_package(argv[2]);
    }
    else {
        printf("Unknown command: %s\n", argv[1]);
        return 1;
    }
    
    return 0;
}
//i had to redo alot of the functions to fix the versioning issues and memory saving problems.