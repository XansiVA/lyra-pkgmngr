//(C) Lyra Package Manager Developers - Licensed under MIT License

// Copyright (c) 2025 Xansi.net - chaoscatsofficial@gmail.com, https://github.com/XansiVA
//Free to use, distribute and edit.

//Cjson library is used for JSON handling (MIT License) 
//Owned by Dave Gamble - https://github.com/DaveGamble

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cjson/cJSON.h>
#include <pwd.h>
#include <termios.h>
#include <libgen.h>

// Forward declarations
void install_package(char *package_name, char *url);
void remove_package(char *package_name);
int extract_github_repo(char *url, char *owner, char *repo);
int get_latest_github_release(char *owner, char *repo, char *url_out, char *version_out);
void extract_version_from_url(char *url, char *version_out);
void backup_to_vault(char *package_name, char *version);
void find_and_install_binary(char *extract_dir, char *package_name);
void uninstall_lyra();
void take_snapshot();
void list_snapshots();
void restore_snapshot(char *date, int number);
void mute_package(char *arg);
void unmute_package(char *package_name);
void clean_everything();
void update_packages();

// Database functions
void db_init();
cJSON* db_read();
void db_write(cJSON *root);
void db_add_package(char *name, char *version, char *url);
void db_remove_package(char *name);
void db_list_packages();
void list_versions(char *package_name);

// NEW: Freeze-copy and encryption functions
void vault_password_setup();
int vault_password_verify(char *password);
void vault_password_prompt(char *password, int is_setup);
void freeze_copy_package(char *package_name);
void list_frozen_copies();
void restore_frozen_copy(char *package_spec);
void cleanup_old_frozen_copies();
void encrypt_file(char *input_path, char *output_path, char *password);
void decrypt_file(char *input_path, char *output_path, char *password);
void create_manifest(char *package_name, char *version, char *binary_path, char *frozen_path);

// Helper function to get the actual user's home directory
char* get_user_home() {
    char *sudo_user = getenv("SUDO_USER");
    if (sudo_user) {
        struct passwd *pw = getpwnam(sudo_user);
        if (pw) return pw->pw_dir;
    }
    return getenv("HOME");
}

// Check if running with sudo
void ensure_sudo() {
    if (geteuid() != 0) {
        printf("Lyra requires sudo privileges.\n");
        printf("Please run: sudo lyra [command]\n");
        exit(1);
    }
}

// NEW: Password input with hidden characters
void vault_password_prompt(char *password, int is_setup) {
    struct termios oldt, newt;
    
    if (is_setup) {
        printf("Create vault password: ");
    } else {
        printf("Enter vault password: ");
    }
    fflush(stdout);
    
    // Disable echo
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    
    fgets(password, 256, stdin);
    password[strcspn(password, "\n")] = 0;
    
    // Re-enable echo
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\n");
}

// NEW: Setup vault password (first time)
void vault_password_setup() {
    char *home = get_user_home();
    char auth_path[512];
    snprintf(auth_path, sizeof(auth_path), "%s/.lyra/config/.auth", home);
    
    if (access(auth_path, F_OK) == 0) {
        return; // Already set up
    }
    
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  VAULT PASSWORD SETUP\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("\nYour vault password will encrypt all frozen copies.\n");
    printf("Keep it safe - you'll need it to restore packages.\n\n");
    
    char password[256];
    char confirm[256];
    
    vault_password_prompt(password, 1);
    printf("Confirm password: ");
    fflush(stdout);
    
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    
    fgets(confirm, sizeof(confirm), stdin);
    confirm[strcspn(confirm, "\n")] = 0;
    
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\n");
    
    if (strcmp(password, confirm) != 0) {
        printf("✗ Passwords don't match!\n");
        exit(1);
    }
    
    if (strlen(password) < 8) {
        printf("✗ Password must be at least 8 characters!\n");
        exit(1);
    }
    
    // Simple hash storage (TODO: Use bcrypt in production but im too lazy :3)
    FILE *fp = fopen(auth_path, "w");
    if (fp) {
        unsigned long hash = 5381;
        for (int i = 0; password[i]; i++) {
            hash = ((hash << 5) + hash) + password[i];
        }
        fprintf(fp, "%lu\n", hash);
        fclose(fp);
        chmod(auth_path, 0600);
        
        printf("✓ Vault password set successfully!\n");
    } else {
        printf("✗ Error: Could not save vault password\n");
        exit(1);
    }
}

// NEW: Verify vault password
int vault_password_verify(char *password) {
    char *home = get_user_home();
    char auth_path[512];
    snprintf(auth_path, sizeof(auth_path), "%s/.lyra/config/.auth", home);
    
    FILE *fp = fopen(auth_path, "r");
    if (!fp) {
        printf("✗ Error: Vault password not set up\n");
        return 0;
    }
    
    unsigned long stored_hash;
    fscanf(fp, "%lu", &stored_hash);
    fclose(fp);
    
    unsigned long hash = 5381;
    for (int i = 0; password[i]; i++) {
        hash = ((hash << 5) + hash) + password[i];
    }
    
    return hash == stored_hash;
}

// NEW: Simple XOR encryption (TODO: Replace with OpenSSL AES-256-GCM in production)
void encrypt_file(char *input_path, char *output_path, char *password) {
    FILE *in = fopen(input_path, "rb");
    FILE *out = fopen(output_path, "wb");
    
    if (!in || !out) {
        printf("✗ Error: Could not open files for encryption\n");
        if (in) fclose(in);
        if (out) fclose(out);
        return;
    }
    
    int key_len = strlen(password);
    int key_pos = 0;
    int c;
    
    while ((c = fgetc(in)) != EOF) {
        fputc(c ^ password[key_pos], out);
        key_pos = (key_pos + 1) % key_len;
    }
    
    fclose(in);
    fclose(out);
}

// NEW: Decrypt file (XOR is symmetric)
void decrypt_file(char *input_path, char *output_path, char *password) {
    encrypt_file(input_path, output_path, password);
}

// NEW: Create manifest.json for frozen copy
void create_manifest(char *package_name, char *version, char *binary_path, char *frozen_path) {
    cJSON *manifest = cJSON_CreateObject();
    
    cJSON_AddStringToObject(manifest, "package", package_name);
    cJSON_AddStringToObject(manifest, "version", version);
    
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", localtime(&now));
    cJSON_AddStringToObject(manifest, "createdAt", timestamp);
    
    struct stat st;
    if (stat(frozen_path, &st) == 0) {
        cJSON_AddNumberToObject(manifest, "sizeBytes", st.st_size);
    }
    
    cJSON_AddBoolToObject(manifest, "isEncrypted", 1);
    
    // TODO: Add dependency tracking
    cJSON *deps = cJSON_CreateArray();
    cJSON_AddItemToObject(manifest, "dependencies", deps);
    
    // Use a buffer instead of strdup to avoid memory leak
    char manifest_dir[512];
    strncpy(manifest_dir, frozen_path, sizeof(manifest_dir) - 1);
    manifest_dir[sizeof(manifest_dir) - 1] = '\0';
    
    char *last_slash = strrchr(manifest_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
    }
    
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", manifest_dir);
    
    char *json_str = cJSON_Print(manifest);
    if (json_str) {
        FILE *fp = fopen(manifest_path, "w");
        if (fp) {
            fprintf(fp, "%s\n", json_str);
            fclose(fp);
        }
        free(json_str);  // FIX: Free the JSON string
    }
    
    cJSON_Delete(manifest);  // FIX: This was already here, good!
}

// NEW: Freeze-copy a package
void freeze_copy_package(char *package_name) {
    char *home = get_user_home();
    
    char auth_path[512];
    snprintf(auth_path, sizeof(auth_path), "%s/.lyra/config/.auth", home);
    if (access(auth_path, F_OK) != 0) {
        vault_password_setup();
    }
    
    char password[256];
    vault_password_prompt(password, 0);
    
    if (!vault_password_verify(password)) {
        printf("✗ Incorrect password!\n");
        return;
    }
    
    cJSON *root = db_read();
    cJSON *pkg = cJSON_GetObjectItem(root, package_name);
    
    if (!pkg) {
        printf("✗ Error: Package '%s' not installed\n", package_name);
        cJSON_Delete(root);
        return;
    }
    
    cJSON *version_obj = cJSON_GetObjectItem(pkg, "version");
    if (!version_obj || !version_obj->valuestring) {
        printf("✗ Error: Could not determine package version\n");
        cJSON_Delete(root);
        return;
    }
    
    char *version = version_obj->valuestring;
    
    printf("→ Freeze-copying %s (%s)...\n", package_name, version);
    
    char frozen_dir[512];
    char version_dir[512];
    snprintf(frozen_dir, sizeof(frozen_dir), "%s/.lyra/vault/frozen/%s", home, package_name);
    snprintf(version_dir, sizeof(version_dir), "%s/%s", frozen_dir, version);
    
    char command[1024];
    snprintf(command, sizeof(command), "mkdir -p %s", version_dir);
    system(command);
    
    char source_path[512];
    char temp_path[512];
    char frozen_path[512];
    
    snprintf(source_path, sizeof(source_path), "/usr/local/bin/%s", package_name);
    snprintf(temp_path, sizeof(temp_path), "/tmp/%s_freeze.tar.gz", package_name);
    snprintf(frozen_path, sizeof(frozen_path), "%s/%s-%s.tar.gz.enc", 
             version_dir, package_name, version);
    
    printf("→ Compressing binary...\n");
    snprintf(command, sizeof(command), "tar -czf %s -C /usr/local/bin %s 2>/dev/null", 
             temp_path, package_name);
    system(command);
    
    printf("→ Encrypting...\n");
    encrypt_file(temp_path, frozen_path, password);
    
    chmod(frozen_path, 0400);
    
    create_manifest(package_name, version, source_path, frozen_path);
    
    remove(temp_path);
    
    printf("✓ Frozen copy created: %s\n", frozen_path);
    printf("  Size: ");
    
    struct stat st;
    if (stat(frozen_path, &st) == 0) {
        double mb = st.st_size / 1048576.0;
        printf("%.2f MB\n", mb);
    }
    
    cJSON_Delete(root);
}

// NEW: List all frozen copies //but probably won't be new for long :3
void list_frozen_copies() {
    char *home = get_user_home();
    char frozen_dir[512];
    snprintf(frozen_dir, sizeof(frozen_dir), "%s/.lyra/vault/frozen", home);
    
    DIR *dir = opendir(frozen_dir);
    if (!dir) {
        printf("No frozen copies found\n");
        return;
    }
    
    printf("Frozen Copies:\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    struct dirent *entry;
    int count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        char pkg_dir[512];
        snprintf(pkg_dir, sizeof(pkg_dir), "%s/%s", frozen_dir, entry->d_name);
        
        DIR *ver_dir = opendir(pkg_dir);
        if (!ver_dir) continue;
        
        printf("\n%s:\n", entry->d_name);
        
        struct dirent *ver_entry;
        while ((ver_entry = readdir(ver_dir)) != NULL) {
            if (ver_entry->d_name[0] == '.') continue;
            
            char manifest_path[512];
            snprintf(manifest_path, sizeof(manifest_path), "%s/%s/manifest.json", 
                     pkg_dir, ver_entry->d_name);
            
            FILE *fp = fopen(manifest_path, "r");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                long size = ftell(fp);
                fseek(fp, 0, SEEK_SET);
                
                char *content = malloc(size + 1);
                if (content) {  // FIX: Check malloc result
                    fread(content, 1, size, fp);
                    content[size] = '\0';
                    fclose(fp);
                    
                    cJSON *manifest = cJSON_Parse(content);
                    if (manifest) {
                        cJSON *version = cJSON_GetObjectItem(manifest, "version");
                        cJSON *created = cJSON_GetObjectItem(manifest, "createdAt");
                        cJSON *size_obj = cJSON_GetObjectItem(manifest, "sizeBytes");
                        
                        printf("  → %s", version ? version->valuestring : ver_entry->d_name);
                        if (created) printf(" (created: %s)", created->valuestring);
                        if (size_obj) {
                            double mb = size_obj->valuedouble / 1048576.0;
                            printf(" [%.2f MB]", mb);
                        }
                        printf(" 🔒\n");
                        
                        cJSON_Delete(manifest);  // FIX: Free cJSON
                    }
                    free(content);  // FIX: Free malloc'd content
                    count++;
                } else {
                    fclose(fp);
                }
            }
        }
        closedir(ver_dir);  // FIX: Close directory
    }
    
    closedir(dir);  // FIX: Close main directory
    
    if (count == 0) {
        printf("  (no frozen copies found)\n");
    } else {
        printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("Total: %d frozen copies (encrypted)\n", count);
    }
}

// NEW: Restore from frozen copy
void restore_frozen_copy(char *package_spec) {
    char package_name[256];
    char version[256];
    
    char *at_sign = strchr(package_spec, '@');
    if (!at_sign) {
        printf("✗ Error: Use format 'package@version'\n");
        printf("Example: lyra -r ripgrep@14.1.0\n");
        return;
    }
    
    int pkg_len = at_sign - package_spec;
    strncpy(package_name, package_spec, pkg_len);
    package_name[pkg_len] = '\0';
    strcpy(version, at_sign + 1);
    
    printf("→ Restoring %s version %s from frozen copy...\n", package_name, version);
    
    char password[256];
    vault_password_prompt(password, 0);
    
    if (!vault_password_verify(password)) {
        printf("✗ Incorrect password!\n");
        return;
    }
    
    char *home = get_user_home();
    char frozen_path[512];
    snprintf(frozen_path, sizeof(frozen_path), 
             "%s/.lyra/vault/frozen/%s/%s/%s-%s.tar.gz.enc",
             home, package_name, version, package_name, version);
    
    if (access(frozen_path, F_OK) != 0) {
        printf("✗ Error: Frozen copy not found for %s@%s\n", package_name, version);
        printf("  Run 'lyra -fl' to see available frozen copies\n");
        return;
    }
    
    char temp_encrypted[512];
    char temp_decrypted[512];
    snprintf(temp_encrypted, sizeof(temp_encrypted), "/tmp/%s_restore.tar.gz.enc", package_name);
    snprintf(temp_decrypted, sizeof(temp_decrypted), "/tmp/%s_restore.tar.gz", package_name);
    
    printf("→ Decrypting...\n");
    
    char command[1024];
    snprintf(command, sizeof(command), "cp %s %s", frozen_path, temp_encrypted);
    system(command);
    
    decrypt_file(temp_encrypted, temp_decrypted, password);
    
    printf("→ Extracting...\n");
    snprintf(command, sizeof(command), "tar -xzf %s -C /usr/local/bin/ 2>/dev/null", temp_decrypted);
    system(command);
    
    snprintf(command, sizeof(command), "chmod +x /usr/local/bin/%s", package_name);
    system(command);
    
    cJSON *root = db_read();
    cJSON *pkg = cJSON_GetObjectItem(root, package_name);
    
    if (pkg) {
        cJSON_ReplaceItemInObject(pkg, "version", cJSON_CreateString(version));
    }
    
    db_write(root);
    cJSON_Delete(root);
    
    remove(temp_encrypted);
    remove(temp_decrypted);
    
    printf("✓ Restored %s version %s successfully!\n", package_name, version);
}

// NEW: Clean up old frozen copies (keep only latest)
void cleanup_old_frozen_copies() {
    printf("→ Cleaning up old frozen copies (keeping only latest version per package)...\n");
    
    char *home = get_user_home();
    char frozen_dir[512];
    snprintf(frozen_dir, sizeof(frozen_dir), "%s/.lyra/vault/frozen", home);
    
    DIR *dir = opendir(frozen_dir);
    if (!dir) {
        printf("No frozen copies to clean\n");
        return;
    }
    
    struct dirent *entry;
    int cleaned = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        char pkg_dir[512];
        snprintf(pkg_dir, sizeof(pkg_dir), "%s/%s", frozen_dir, entry->d_name);
        
        DIR *ver_dir = opendir(pkg_dir);
        if (!ver_dir) continue;
        
        char latest_version[256] = "";
        time_t latest_time = 0;
        
        struct dirent *ver_entry;
        while ((ver_entry = readdir(ver_dir)) != NULL) {
            if (ver_entry->d_name[0] == '.') continue;
            
            char manifest_path[512];
            snprintf(manifest_path, sizeof(manifest_path), "%s/%s/manifest.json", 
                     pkg_dir, ver_entry->d_name);
            
            struct stat st;
            if (stat(manifest_path, &st) == 0) {
                if (st.st_mtime > latest_time) {
                    latest_time = st.st_mtime;
                    strncpy(latest_version, ver_entry->d_name, sizeof(latest_version) - 1);
                    latest_version[sizeof(latest_version) - 1] = '\0';
                }
            }
        }
        closedir(ver_dir);  // FIX: Close first time
        
        ver_dir = opendir(pkg_dir);
        if (!ver_dir) continue;
        
        while ((ver_entry = readdir(ver_dir)) != NULL) {
            if (ver_entry->d_name[0] == '.') continue;
            if (strcmp(ver_entry->d_name, latest_version) == 0) continue;
            
            char old_dir[512];
            snprintf(old_dir, sizeof(old_dir), "%s/%s", pkg_dir, ver_entry->d_name);
            
            char command[1024];
            snprintf(command, sizeof(command), "rm -rf %s", old_dir);
            system(command);
            
            printf("  Removed %s/%s\n", entry->d_name, ver_entry->d_name);
            cleaned++;
        }
        closedir(ver_dir);  // FIX: Close second time
    }
    
    closedir(dir);  // FIX: Close main directory
    
    printf("✓ Cleaned %d old frozen copies\n", cleaned);
}

// Database functions
void db_init() {
    char db_path[512];
    char *home = get_user_home();
    
    char path[512];
    snprintf(path, sizeof(path), "%s/.lyra", home);
    mkdir(path, 0755);
    
    snprintf(path, sizeof(path), "%s/.lyra/vault", home);
    mkdir(path, 0755);
    
    snprintf(path, sizeof(path), "%s/.lyra/vault/snapshots", home);
    mkdir(path, 0755);
    
    snprintf(path, sizeof(path), "%s/.lyra/vault/frozen", home);
    mkdir(path, 0755);
    
    snprintf(path, sizeof(path), "%s/.lyra/config", home);
    mkdir(path, 0755);
    
    char rules_path[512];
    snprintf(rules_path, sizeof(rules_path), "%s/.lyra/config/rules.conf", home);
    if (access(rules_path, F_OK) != 0) {
        FILE *rules_fp = fopen(rules_path, "w");
        if (rules_fp) {
            fprintf(rules_fp, "# Lyra Update Rules Configuration\n");
            fprintf(rules_fp, "# Add packages to sections to control update behavior\n\n");
            fprintf(rules_fp, "[bleeding_edge]\n");
            fprintf(rules_fp, "# Auto-update to latest versions, even breaking changes\n\n");
            fprintf(rules_fp, "[experimental]\n");
            fprintf(rules_fp, "# Update to latest stable releases only\n\n");
            fprintf(rules_fp, "[stable]\n");
            fprintf(rules_fp, "# Update only for security patches and bug fixes\n\n");
            fprintf(rules_fp, "[locked]\n");
            fprintf(rules_fp, "# Never update - stay at specific version\n");
            fprintf(rules_fp, "# Format: package=version\n");
            fclose(rules_fp);
        }
    }
    
    snprintf(db_path, sizeof(db_path), "%s/.lyra/active_packages.json", home);
    
    if (access(db_path, F_OK) == 0) {
        return;
    }
    
    cJSON *root = cJSON_CreateObject();
    char *json_str = cJSON_Print(root);
    
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
    char *home = get_user_home();
    snprintf(db_path, sizeof(db_path), "%s/.lyra/active_packages.json", home);
    
    FILE *fp = fopen(db_path, "r");
    if (fp == NULL) {
        return cJSON_CreateObject();
    }
    
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char *content = malloc(size + 1);
    fread(content, 1, size, fp);
    content[size] = '\0';
    fclose(fp);
    
    cJSON *root = cJSON_Parse(content);
    free(content);
    
    if (root == NULL) {
        return cJSON_CreateObject();
    }
    
    return root;
}

void db_write(cJSON *root) {
    char db_path[512];
    char *home = get_user_home();
    snprintf(db_path, sizeof(db_path), "%s/.lyra/active_packages.json", home);
    
    char *json_str = cJSON_Print(root);
    
    if (json_str == NULL) {
        FILE *fp = fopen(db_path, "w");
        if (fp != NULL) {
            fprintf(fp, "{\n}\n");
            fclose(fp);
        }
        return;
    }
    
    FILE *fp = fopen(db_path, "w");
    if (fp != NULL) {
        fprintf(fp, "%s\n", json_str);
        fclose(fp);
    }
    
    free(json_str);  // FIX: Free the JSON string
}

void db_add_package(char *name, char *version, char *url) {
    cJSON *root = db_read();
    
    cJSON *package = cJSON_CreateObject();
    cJSON_AddStringToObject(package, "version", version);
    cJSON_AddStringToObject(package, "url", url);
    
    if (strstr(url, "github.com")) {
        cJSON_AddStringToObject(package, "source", "github");
    } else {
        cJSON_AddStringToObject(package, "source", "mirror");
    }
    
    char install_path[512];
    snprintf(install_path, sizeof(install_path), "/usr/local/bin/%s", name);
    cJSON_AddStringToObject(package, "installed_path", install_path);
    cJSON_AddStringToObject(package, "status", "active");
    
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", localtime(&now));
    cJSON_AddStringToObject(package, "installed_date", timestamp);
    
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

void list_versions(char *package_name) {
    cJSON *root = db_read();
    cJSON *pkg = cJSON_GetObjectItem(root, package_name);
    
    if (!pkg) {
        printf("Error: Package '%s' not found\n", package_name);
        cJSON_Delete(root);
        return;
    }
    
    printf("Available versions for %s:\n", package_name);
    printf("--------------------------\n");
    
    cJSON *active_ver = cJSON_GetObjectItem(pkg, "version");
    cJSON *active_date = cJSON_GetObjectItem(pkg, "installed_date");
    
    if (active_ver) {
        printf("→ %s [active]", active_ver->valuestring);
        if (active_date) {
            printf(" - %s", active_date->valuestring);
        }
        printf("\n");
    }
    
    cJSON *versions = cJSON_GetObjectItem(pkg, "versions");
    int muted_count = 0;
    
    if (versions && cJSON_GetArraySize(versions) > 0) {
        cJSON *ver = NULL;
        cJSON_ArrayForEach(ver, versions) {
            cJSON *v = cJSON_GetObjectItem(ver, "version");
            cJSON *date = cJSON_GetObjectItem(ver, "installed_date");
            
            if (v) {
                printf("  %s [muted]", v->valuestring);
                if (date) {
                    printf(" - %s", date->valuestring);
                }
                printf("\n");
                muted_count++;
            }
        }
    }
    
    int total = (active_ver ? 1 : 0) + muted_count;
    printf("\nTotal: %d version%s in vault\n", total, total == 1 ? "" : "s");
    
    cJSON_Delete(root);
}

char* get_package_policy(char *package_name) {
    char *home = get_user_home();
    char rules_path[512];
    snprintf(rules_path, sizeof(rules_path), "%s/.lyra/config/rules.conf", home);
    
    FILE *fp = fopen(rules_path, "r");
    if (!fp) return "stable";
    
    char line[256];
    char current_section[64] = "";
    
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = 0;
        
        if (line[0] == '#' || line[0] == '\0') continue;
        
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end) {
                int len = end - line - 1;
                strncpy(current_section, line + 1, len);
                current_section[len] = '\0';
            }
            continue;
        }
        
        if (strstr(line, package_name) != NULL) {
            fclose(fp);
            static char policy[64];
            strcpy(policy, current_section);
            return policy;
        }
    }
    
    fclose(fp);
    return "stable";
}

int extract_github_repo(char *url, char *owner, char *repo) {
    char *github = strstr(url, "github.com/");
    if (!github) return 0;
    
    github += strlen("github.com/");
    
    char *slash = strchr(github, '/');
    if (!slash) return 0;
    
    int owner_len = slash - github;
    strncpy(owner, github, owner_len);
    owner[owner_len] = '\0';
    
    github = slash + 1;
    slash = strchr(github, '/');
    if (!slash) return 0;
    
    int repo_len = slash - github;
    strncpy(repo, github, repo_len);
    repo[repo_len] = '\0';
    
    return 1;
}

int get_latest_github_release(char *owner, char *repo, char *url_out, char *version_out) {
    char command[1024];
    char api_url[512];
    
    snprintf(api_url, sizeof(api_url), 
             "https://api.github.com/repos/%s/%s/releases/latest", owner, repo);
    
    snprintf(command, sizeof(command),
             "curl -s %s | grep -m 1 'browser_download_url.*linux.*x86_64.*tar.gz' | cut -d '\"' -f 4",
             api_url);
    
    FILE *fp = popen(command, "r");
    if (!fp) return 0;
    
    if (fgets(url_out, 512, fp) == NULL) {
        pclose(fp);
        return 0;
    }
    pclose(fp);
    
    url_out[strcspn(url_out, "\n")] = 0;
    
    if (strlen(url_out) == 0) return 0;
    
    extract_version_from_url(url_out, version_out);
    
    return 1;
}

void extract_version_from_url(char *url, char *version_out) {
    char temp[64] = "unknown";
    
    char *ptr = strstr(url, "releases/download/");
    if (ptr) {
        ptr += strlen("releases/download/");
        
        if (*ptr == 'v') ptr++;
        
        int i = 0;
        while (i < 63 && ((*ptr >= '0' && *ptr <= '9') || *ptr == '.')) {
            temp[i++] = *ptr++;
        }
        temp[i] = '\0';
        
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

void backup_to_vault(char *package_name, char *version) {
    char *home = get_user_home();
    char vault_dir[512];
    char version_dir[512];
    char source[512];
    char dest[512];
    char command[1024];
    
    snprintf(vault_dir, sizeof(vault_dir), "%s/.lyra/vault/%s", home, package_name);
    mkdir(vault_dir, 0755);
    
    snprintf(version_dir, sizeof(version_dir), "%s/%s", vault_dir, version);
    mkdir(version_dir, 0755);
    
    snprintf(source, sizeof(source), "/usr/local/bin/%s", package_name);
    snprintf(dest, sizeof(dest), "%s/%s", version_dir, package_name);
    
    snprintf(command, sizeof(command), "cp %s %s", source, dest);
    system(command);
    
    printf("→ Backed up %s (%s) to vault\n", package_name, version);
}

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

void install_package(char *package_name, char *url) {
    char download_path[512];
    char extract_dir[512];
    char command[1024];
    char version[64];
    char installed_path[512];
    
    char old_version[256] = "";
    char old_url[1024] = "";
    int has_old_version = 0;
    
    db_init();
    
    printf("Installing %s...\n", package_name);
    
    extract_version_from_url(url, version);
    
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
    
    printf("→ Downloading version %s...\n", version);
    snprintf(command, sizeof(command), "curl -L -o %s %s 2>/dev/null", download_path, url);
    system(command);
    
    snprintf(command, sizeof(command), "mkdir -p %s", extract_dir);
    system(command);
    
    printf("→ Extracting...\n");
    snprintf(command, sizeof(command), "tar -xzf %s -C %s 2>/dev/null", download_path, extract_dir);
    system(command);
    
    find_and_install_binary(extract_dir, package_name);
    
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
            cJSON_ReplaceItemInObject(pkg, "url", cJSON_CreateString(url));
            
            db_write(root);
        }
        cJSON_Delete(root);
    } else {
        db_add_package(package_name, version, url);
    }
    
    printf("→ Added to database\n");
    
    remove(download_path);
    snprintf(command, sizeof(command), "rm -rf %s", extract_dir);
    system(command);
}

void update_packages() {
    printf("Checking for updates...\n");
    
    cJSON *root = db_read();
    cJSON *pkg = NULL;
    
    int updated = 0;
    
    cJSON_ArrayForEach(pkg, root) {
        char *pkg_name = pkg->string;
        char *policy = get_package_policy(pkg_name);
        
        printf("\n%s (policy: %s)\n", pkg_name, policy);
        
        if (strcmp(policy, "locked") == 0) {
            printf("  Locked - skipping\n");
            continue;
        }
        
        cJSON *source_obj = cJSON_GetObjectItem(pkg, "source");
        cJSON *url_obj = cJSON_GetObjectItem(pkg, "url");
        cJSON *current_ver_obj = cJSON_GetObjectItem(pkg, "version");
        
        if (!source_obj || !url_obj || !current_ver_obj) {
            printf("  Error: Missing package metadata\n");
            continue;
        }
        
        char *source = source_obj->valuestring;
        char *current_url = url_obj->valuestring;
        char *current_version = current_ver_obj->valuestring;
        
        if (strcmp(source, "github") == 0) {
            char owner[128], repo[128];
            if (extract_github_repo(current_url, owner, repo)) {
                printf("  Checking GitHub (%s/%s)...\n", owner, repo);
                
                char latest_url[512];
                char latest_version[64];
                
                if (get_latest_github_release(owner, repo, latest_url, latest_version)) {
                    if (strcmp(current_version, latest_version) != 0) {
                        printf("  → Update available: %s → %s\n", current_version, latest_version);
                        printf("  → Installing update...\n");
                        
                        install_package(pkg_name, latest_url);
                        updated++;
                    } else {
                        printf("  Already up to date (%s)\n", current_version);
                    }
                } else {
                    printf("  Error: Could not fetch latest release from GitHub\n");
                }
            } else {
                printf("  Error: Could not parse GitHub URL\n");
            }
        } else if (strcmp(source, "mirror") == 0) {
            printf("  Mirror updates not implemented yet\n");
        } else {
            printf("  Unknown source type: %s\n", source);
        }
    }
    
    if (updated == 0) {
        printf("\n✓ All packages are up to date!\n");
    } else {
        printf("\n✓ Updated %d package%s\n", updated, updated == 1 ? "" : "s");
    }
    
    cJSON_Delete(root);
}

void take_snapshot() {
    char *home = get_user_home();
    char snapshot_dir[512];
    char snapshot_path[512];
    
    db_init();
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    char timestamp[64];
    char date_str[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", tm_info);
    strftime(date_str, sizeof(date_str), "%d-%m-%Y", tm_info);
    
    snprintf(snapshot_dir, sizeof(snapshot_dir), "%s/.lyra/vault/snapshots", home);
    mkdir(snapshot_dir, 0755);
    
    int snapshot_num = 1;
    DIR *dir = opendir(snapshot_dir);
    if (dir) {
        struct dirent *entry;
        char search_prefix[64];
        snprintf(search_prefix, sizeof(search_prefix), "%s_", date_str);
        
        while ((entry = readdir(dir)) != NULL) {
            if (strncmp(entry->d_name, search_prefix, strlen(search_prefix)) == 0) {
                snapshot_num++;
            }
        }
        closedir(dir);
    }
    
    cJSON *snapshot = cJSON_CreateObject();
    cJSON_AddStringToObject(snapshot, "timestamp", timestamp);
    cJSON_AddStringToObject(snapshot, "date", date_str);
    cJSON_AddNumberToObject(snapshot, "snapshotNumber", snapshot_num);
    
    cJSON *db = db_read();
    cJSON *packages = cJSON_CreateObject();
    
    cJSON *pkg = NULL;
    cJSON_ArrayForEach(pkg, db) {
        char *pkg_name = pkg->string;
        
        cJSON *pkg_snapshot = cJSON_CreateObject();
        
        cJSON *version = cJSON_GetObjectItem(pkg, "version");
        cJSON *status = cJSON_GetObjectItem(pkg, "status");
        cJSON *install_path = cJSON_GetObjectItem(pkg, "installed_path");
        cJSON *url = cJSON_GetObjectItem(pkg, "url");
        
        if (version) cJSON_AddStringToObject(pkg_snapshot, "version", version->valuestring);
        if (status) cJSON_AddStringToObject(pkg_snapshot, "status", status->valuestring);
        if (install_path) cJSON_AddStringToObject(pkg_snapshot, "installPath", install_path->valuestring);
        if (url) cJSON_AddStringToObject(pkg_snapshot, "url", url->valuestring);
        
        cJSON *muted_array = cJSON_CreateArray();
        cJSON *versions = cJSON_GetObjectItem(pkg, "versions");
        if (versions) {
            cJSON *ver = NULL;
            cJSON_ArrayForEach(ver, versions) {
                cJSON *v = cJSON_GetObjectItem(ver, "version");
                if (v) cJSON_AddItemToArray(muted_array, cJSON_CreateString(v->valuestring));
            }
        }
        cJSON_AddItemToObject(pkg_snapshot, "mutedVersions", muted_array);
        
        cJSON_AddItemToObject(packages, pkg_name, pkg_snapshot);
    }
    
    cJSON_AddItemToObject(snapshot, "packages", packages);
    cJSON_Delete(db);
    
    snprintf(snapshot_path, sizeof(snapshot_path), "%s/%s_%d.json", 
             snapshot_dir, date_str, snapshot_num);
    
    char *json_str = cJSON_Print(snapshot);
    FILE *fp = fopen(snapshot_path, "w");
    if (fp) {
        fprintf(fp, "%s\n", json_str);
        fclose(fp);
        printf("Snapshot saved: %s_%d\n", date_str, snapshot_num);
        printf("Location: %s\n", snapshot_path);
    } else {
        printf("Error: Could not save snapshot\n");
    }
    
    free(json_str);
    cJSON_Delete(snapshot);
}

void list_snapshots() {
    char *home = get_user_home();
    char snapshot_dir[512];
    
    snprintf(snapshot_dir, sizeof(snapshot_dir), "%s/.lyra/vault/snapshots", home);
    
    DIR *dir = opendir(snapshot_dir);
    if (!dir) {
        printf("No snapshots found\n");
        return;
    }
    
    printf("Available snapshots:\n");
    printf("-------------------\n");
    
    struct dirent *entry;
    int count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (strstr(entry->d_name, ".json") == NULL) continue;
        
        char snapshot_path[512];
        snprintf(snapshot_path, sizeof(snapshot_path), "%s/%s", snapshot_dir, entry->d_name);
        
        FILE *fp = fopen(snapshot_path, "r");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long size = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            
            char *content = malloc(size + 1);
            if (content) {  // FIX: Check malloc result
                fread(content, 1, size, fp);
                content[size] = '\0';
                fclose(fp);
                
                cJSON *snapshot = cJSON_Parse(content);
                if (snapshot) {
                    cJSON *timestamp = cJSON_GetObjectItem(snapshot, "timestamp");
                    cJSON *packages = cJSON_GetObjectItem(snapshot, "packages");
                    int pkg_count = packages ? cJSON_GetArraySize(packages) : 0;
                    
                    printf("  %s", entry->d_name);
                    if (timestamp) printf(" - %s", timestamp->valuestring);
                    printf(" (%d packages)\n", pkg_count);
                    
                    cJSON_Delete(snapshot);  // FIX: Free cJSON
                }
                free(content);  // FIX: Free malloc'd content
                count++;
            } else {
                fclose(fp);
            }
        }
    }
    
    closedir(dir);  // FIX: Close directory
    
    if (count == 0) {
        printf("  (no snapshots found)\n");
    } else {
        printf("\nTotal: %d snapshots\n", count);
    }
}

void restore_snapshot(char *date, int number) {
    char *home = get_user_home();
    char snapshot_path[512];

    snprintf(snapshot_path, sizeof(snapshot_path), "%s/.lyra/vault/snapshots/%s_%d.json",
             home, date, number);

    if (access(snapshot_path, F_OK) != 0) {
        printf("Error: Snapshot '%s_%d' not found\n", date, number);
        return;
    }

    printf("WARNING: This will restore your system to snapshot %s_%d\n", date, number);
    printf("This will change installed package versions and active/muted states.\n");
    printf("Are you sure? (y/N): ");

    char response[10];
    if (fgets(response, sizeof(response), stdin) == NULL ||
        (response[0] != 'y' && response[0] != 'Y')) {
        printf("Restore cancelled\n");
        return;
    }

    FILE *fp = fopen(snapshot_path, "r");
    if (!fp) {
        printf("Error: Could not read snapshot\n");
        return;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *content = malloc(size + 1);
    if (!content) {
        fclose(fp);
        printf("Error: Out of memory\n");
        return;
    }
    fread(content, 1, size, fp);
    content[size] = '\0';
    fclose(fp);

    cJSON *snapshot = cJSON_Parse(content);
    free(content);

    if (!snapshot) {
        printf("Error: Invalid snapshot file\n");
        return;
    }

    cJSON *packages = cJSON_GetObjectItem(snapshot, "packages");
    if (!packages) {
        printf("Error: No packages in snapshot\n");
        cJSON_Delete(snapshot);
        return;
    }

    printf("→ Restoring snapshot...\n");

    cJSON *new_db = cJSON_CreateObject();

    cJSON *pkg = NULL;
    cJSON_ArrayForEach(pkg, packages) {
        char *pkg_name = pkg->string;
        if (!pkg_name) continue;

        cJSON *version_obj = cJSON_GetObjectItem(pkg, "version");
        cJSON *status_obj = cJSON_GetObjectItem(pkg, "status");
        cJSON *url_obj = cJSON_GetObjectItem(pkg, "url");
        cJSON *installed_path_obj = cJSON_GetObjectItem(pkg, "installPath");
        cJSON *muted_array = cJSON_GetObjectItem(pkg, "mutedVersions");

        const char *version = version_obj && version_obj->valuestring ? version_obj->valuestring : NULL;
        const char *status = status_obj && status_obj->valuestring ? status_obj->valuestring : "unknown";
        const char *url = url_obj && url_obj->valuestring ? url_obj->valuestring : NULL;
        const char *installed_path = installed_path_obj && installed_path_obj->valuestring ? installed_path_obj->valuestring : NULL;

        if (!version) {
            printf("  Warning: package '%s' in snapshot lacks 'version', skipping\n", pkg_name);
            continue;
        }

        char vault_path[512];
        char dest_path[512];
        char command[1024];

        snprintf(vault_path, sizeof(vault_path), "%s/.lyra/vault/%s/%s/%s",
                 home, pkg_name, version, pkg_name);
        snprintf(dest_path, sizeof(dest_path), "/usr/local/bin/%s", pkg_name);

        if (access(vault_path, F_OK) != 0) {
            printf("    Warning: Version %s for package %s not found in vault\n", version, pkg_name);
            
            if (url) {
                printf("    → Re-downloading from URL: %s\n", url);
                
                char download_path[512];
                char extract_dir[512];
                snprintf(download_path, sizeof(download_path), "/tmp/%s_restore.tar.gz", pkg_name);
                snprintf(extract_dir, sizeof(extract_dir), "/tmp/%s_restore_extracted", pkg_name);
                
                snprintf(command, sizeof(command), "curl -L -o %s %s 2>/dev/null", download_path, url);
                if (system(command) == 0) {
                    snprintf(command, sizeof(command), "mkdir -p %s", extract_dir);
                    system(command);
                    
                    snprintf(command, sizeof(command), "tar -xzf %s -C %s 2>/dev/null", download_path, extract_dir);
                    system(command);
                    
                    snprintf(command, sizeof(command), "find %s -type f -executable | head -n 1", extract_dir);
                    FILE *fp = popen(command, "r");
                    char binary_path[512];
                    if (fp && fgets(binary_path, sizeof(binary_path), fp)) {
                        binary_path[strcspn(binary_path, "\n")] = 0;
                        
                        snprintf(command, sizeof(command), "cp %s %s", binary_path, dest_path);
                        system(command);
                        snprintf(command, sizeof(command), "chmod +x %s", dest_path);
                        system(command);
                        
                        char vault_ver_dir[512];
                        snprintf(vault_ver_dir, sizeof(vault_ver_dir), "%s/.lyra/vault/%s/%s", home, pkg_name, version);
                        snprintf(command, sizeof(command), "mkdir -p %s", vault_ver_dir);
                        system(command);
                        snprintf(command, sizeof(command), "cp %s %s/%s", binary_path, vault_ver_dir, pkg_name);
                        system(command);
                        
                        printf("  → Restored %s (%s) from URL\n", pkg_name, version);
                        
                        if (fp) pclose(fp);
                    } else {
                        printf("  Error: Could not find binary in downloaded archive\n");
                        if (fp) pclose(fp);
                    }
                    
                    remove(download_path);
                    snprintf(command, sizeof(command), "rm -rf %s", extract_dir);
                    system(command);
                } else {
                    printf("  Error: Failed to download from URL\n");
                }
            } else {
                printf("  Error: No URL available to re-download package\n");
            }
        } else {
            snprintf(command, sizeof(command), "cp %s %s", vault_path, dest_path);
            if (system(command) == 0) {
                snprintf(command, sizeof(command), "chmod +x %s", dest_path);
                system(command);
                printf("  → Restored %s (%s)\n", pkg_name, version);
            } else {
                printf("  Error: Failed to copy %s to %s\n", vault_path, dest_path);
            }
        }

        cJSON *pkg_entry = cJSON_CreateObject();
        cJSON_AddStringToObject(pkg_entry, "version", version);
        if (url) cJSON_AddStringToObject(pkg_entry, "url", url);
        if (installed_path) cJSON_AddStringToObject(pkg_entry, "installed_path", installed_path);
        else cJSON_AddStringToObject(pkg_entry, "installed_path", dest_path);
        cJSON_AddStringToObject(pkg_entry, "status", status);

        cJSON *versions_obj = cJSON_CreateArray();
        if (muted_array && cJSON_IsArray(muted_array) && cJSON_GetArraySize(muted_array) > 0) {
            cJSON *mv = NULL;
            cJSON_ArrayForEach(mv, muted_array) {
                if (cJSON_IsString(mv)) {
                    cJSON *verobj = cJSON_CreateObject();
                    cJSON_AddStringToObject(verobj, "version", mv->valuestring);
                    cJSON_AddStringToObject(verobj, "status", "muted");
                    cJSON_AddItemToArray(versions_obj, verobj);
                } else if (cJSON_IsObject(mv)) {
                    cJSON *verstr = cJSON_GetObjectItem(mv, "version");
                    cJSON *urlstr = cJSON_GetObjectItem(mv, "url");
                    cJSON *verobj = cJSON_CreateObject();
                    if (verstr && verstr->valuestring) cJSON_AddStringToObject(verobj, "version", verstr->valuestring);
                    if (urlstr && urlstr->valuestring) cJSON_AddStringToObject(verobj, "url", urlstr->valuestring);
                    cJSON_AddStringToObject(verobj, "status", "muted");
                    cJSON_AddItemToArray(versions_obj, verobj);
                }
            }
        }
        cJSON_AddItemToObject(pkg_entry, "versions", versions_obj);

        cJSON_AddItemToObject(new_db, pkg_name, pkg_entry);
    }

    db_write(new_db);
    cJSON_Delete(new_db);

    printf("Done! System restored to snapshot %s_%d\n", date, number);
    printf("Note: Run 'lyra -list' to verify\n");

    cJSON_Delete(snapshot);
}

void mute_package(char *arg) {
    char package_name[256];
    char target_version[256] = "";
    int has_target = 0;
    
    char *at_sign = strchr(arg, '@');
    if (at_sign) {
        int pkg_len = at_sign - arg;
        strncpy(package_name, arg, pkg_len);
        package_name[pkg_len] = '\0';
        
        strcpy(target_version, at_sign + 1);
        has_target = 1;
    } else {
        strcpy(package_name, arg);
    }
    
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
    
    cJSON *current_ver = cJSON_GetObjectItem(pkg, "version");
    if (!current_ver || !current_ver->valuestring) {
        printf("Error: Could not determine current version\n");
        cJSON_Delete(root);
        return;
    }
    
    char active_version[256];
    strncpy(active_version, current_ver->valuestring, 255);
    active_version[255] = '\0';
    
    cJSON *target_entry = NULL;
    int target_index = 0;
    char found_version[256] = "";
    
    if (has_target) {
        int i = 0;
        cJSON *ver = NULL;
        cJSON_ArrayForEach(ver, versions) {
            cJSON *v = cJSON_GetObjectItem(ver, "version");
            if (v && v->valuestring && strcmp(v->valuestring, target_version) == 0) {
                target_entry = ver;
                target_index = i;
                strncpy(found_version, v->valuestring, 255);
                found_version[255] = '\0';
                break;
            }
            i++;
        }
        
        if (!target_entry) {
            printf("Error: Version '%s' not found in muted versions\n", target_version);
            printf("Use 'lyra -lv %s' to see available versions\n", package_name);
            cJSON_Delete(root);
            return;
        }
    } else {
        target_entry = cJSON_GetArrayItem(versions, 0);
        target_index = 0;
        cJSON *v = cJSON_GetObjectItem(target_entry, "version");
        if (v && v->valuestring) {
            strncpy(found_version, v->valuestring, 255);
            found_version[255] = '\0';
        }
    }
    
    if (!target_entry || strlen(found_version) == 0) {
        printf("Error: Could not find target version\n");
        cJSON_Delete(root);
        return;
    }
    
    printf("→ Switching from %s to %s\n", active_version, found_version);
    
    backup_to_vault(package_name, active_version);
    
    char *home = get_user_home();
    char vault_path[512];
    char dest_path[512];
    char command[1024];
    
    snprintf(vault_path, sizeof(vault_path), "%s/.lyra/vault/%s/%s/%s", 
             home, package_name, found_version, package_name);
    snprintf(dest_path, sizeof(dest_path), "/usr/local/bin/%s", package_name);
    
    if (access(vault_path, F_OK) != 0) {
        printf("Error: Muted version not found in vault\n");
        cJSON_Delete(root);
        return;
    }
    
    snprintf(command, sizeof(command), "cp %s %s", vault_path, dest_path);
    system(command);
    snprintf(command, sizeof(command), "chmod +x %s", dest_path);
    system(command);
    
    cJSON *target_url = cJSON_GetObjectItem(target_entry, "url");
    cJSON_ReplaceItemInObject(pkg, "version", cJSON_CreateString(found_version));
    if (target_url) {
        cJSON_ReplaceItemInObject(pkg, "url", cJSON_CreateString(target_url->valuestring));
    }
    
    cJSON_DetachItemFromArray(versions, target_index);
    
    cJSON *new_muted = cJSON_CreateObject();
    cJSON_AddStringToObject(new_muted, "version", active_version);
    cJSON *old_url = cJSON_GetObjectItem(pkg, "url");
    if (old_url) {
        cJSON_AddStringToObject(new_muted, "url", old_url->valuestring);
    }
    cJSON_AddItemToArray(versions, new_muted);
    
    db_write(root);
    cJSON_Delete(root);
    
    printf("Done! Now using %s version %s\n", package_name, found_version);
}

void unmute_package(char *package_name) {
    cJSON *root = db_read();
    cJSON *pkg = cJSON_GetObjectItem(root, package_name);
    
    if (!pkg) {
        printf("Error: Package '%s' not found\n", package_name);
        cJSON_Delete(root);
        return;
    }
    
    cJSON *versions = cJSON_GetObjectItem(pkg, "versions");
    if (!versions || cJSON_GetArraySize(versions) == 0) {
        printf("Error: Package '%s' has no muted versions to unmute\n", package_name);
        cJSON_Delete(root);
        return;
    }
    
    cJSON *current_ver = cJSON_GetObjectItem(pkg, "version");
    if (!current_ver || !current_ver->valuestring) {
        printf("Error: Could not determine current version\n");
        cJSON_Delete(root);
        return;
    }
    
    char active_version[256];
    strncpy(active_version, current_ver->valuestring, 255);
    active_version[255] = '\0';
    
    cJSON *target_entry = cJSON_GetArrayItem(versions, 0);
    cJSON *v = cJSON_GetObjectItem(target_entry, "version");
    
    if (!v || !v->valuestring) {
        printf("Error: Invalid muted version data\n");
        cJSON_Delete(root);
        return;
    }
    
    char unmute_version[256];
    strncpy(unmute_version, v->valuestring, 255);
    unmute_version[255] = '\0';
    
    printf("→ Unmuting %s (switching from %s to %s)\n", package_name, active_version, unmute_version);
    
    backup_to_vault(package_name, active_version);
    
    char *home = get_user_home();
    char vault_path[512];
    char dest_path[512];
    char command[1024];
    
    snprintf(vault_path, sizeof(vault_path), "%s/.lyra/vault/%s/%s/%s", 
             home, package_name, unmute_version, package_name);
    snprintf(dest_path, sizeof(dest_path), "/usr/local/bin/%s", package_name);
    
    if (access(vault_path, F_OK) != 0) {
        printf("Error: Unmuted version not found in vault\n");
        cJSON_Delete(root);
        return;
    }
    
    snprintf(command, sizeof(command), "cp %s %s", vault_path, dest_path);
    system(command);
    snprintf(command, sizeof(command), "chmod +x %s", dest_path);
    system(command);
    
    cJSON *target_url = cJSON_GetObjectItem(target_entry, "url");
    cJSON_ReplaceItemInObject(pkg, "version", cJSON_CreateString(unmute_version));
    if (target_url) {
        cJSON_ReplaceItemInObject(pkg, "url", cJSON_CreateString(target_url->valuestring));
    }
    
    cJSON_DetachItemFromArray(versions, 0);
    
    cJSON *new_muted = cJSON_CreateObject();
    cJSON_AddStringToObject(new_muted, "version", active_version);
    cJSON *old_url = cJSON_GetObjectItem(pkg, "url");
    if (old_url) {
        cJSON_AddStringToObject(new_muted, "url", old_url->valuestring);
    }
    cJSON_AddItemToArray(versions, new_muted);
    
    db_write(root);
    cJSON_Delete(root);
    
    printf("Done! Now using %s version %s\n", package_name, unmute_version);
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

void clean_everything() {
    char *home = get_user_home();
    char lyra_dir[512];
    char command[1024];
    
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  LYRA NUCLEAR CLEANUP WARNING \n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("\nThis will PERMANENTLY DELETE:\n");
    printf("  • All package vault copies (~/.lyra/vault/)\n");
    printf("  • All frozen copies (~/.lyra/vault/frozen/)\n");
    printf("  • All system snapshots (~/.lyra/vault/snapshots/)\n");
    printf("  • Package database (~/.lyra/active_packages.json)\n");
    printf("  • Configuration files (~/.lyra/config/)\n");
    printf("  • The entire ~/.lyra directory\n");
    printf("\nInstalled packages in /usr/local/bin/ will NOT be removed.\n");
    printf("You will need to manually uninstall them if desired.\n");
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("\nType 'YES' (all caps) to confirm nuclear cleanup: ");
    
    char response[10];
    if (fgets(response, sizeof(response), stdin) == NULL) {
        printf("\nCleanup cancelled\n");
        return;
    }
    
    response[strcspn(response, "\n")] = 0;
    
    if (strcmp(response, "YES") != 0) {
        printf("\nCleanup cancelled (you must type 'YES' exactly)\n");
        return;
    }
    
    printf("\n🧹 Starting nuclear cleanup...\n");
    
    snprintf(lyra_dir, sizeof(lyra_dir), "%s/.lyra", home);
    
    if (access(lyra_dir, F_OK) == 0) {
        printf("→ Removing %s...\n", lyra_dir);
        snprintf(command, sizeof(command), "rm -rf %s", lyra_dir);
        
        if (system(command) == 0) {
            printf("✓ Successfully removed ~/.lyra\n");
        } else {
            printf("✗ Error: Failed to remove ~/.lyra\n");
            return;
        }
    } else {
        printf("→ ~/.lyra directory doesn't exist (already clean)\n");
    }
    
    printf("\n→ Reinitializing Lyra...\n");
    db_init();
    
    printf("✓ Fresh database created\n");
    printf("✓ New vault structure created\n");
    printf("✓ Default rules.conf created\n");
    
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf(" Lyra has been reset to factory fresh state! \n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("\nYou can now start fresh with 'lyra -i <package> <url>'\n");
}

void uninstall_lyra() {
    char *home = get_user_home();
    char lyra_dir[512];
    char lyra_binary[512];
    char command[1024];
    
    printf("\n");
    printf("  LYRA COMPLETE UNINSTALL WARNING \n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("\nThis will PERMANENTLY DELETE:\n");
    printf("  • Lyra binary (/usr/local/bin/lyra)\n");
    printf("  • All package vault copies (~/.lyra/vault/)\n");
    printf("  • All frozen copies (~/.lyra/vault/frozen/)\n");
    printf("  • All system snapshots (~/.lyra/vault/snapshots/)\n");
    printf("  • Package database (~/.lyra/active_packages.json)\n");
    printf("  • Configuration files (~/.lyra/config/)\n");
    printf("  • The entire ~/.lyra directory\n");
    printf("\nPackages installed by Lyra will remain in /usr/local/bin/\n");
    printf("You will need to manually remove them if desired.\n");
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("\nType 'UNINSTALL' (all caps) to confirm complete removal: ");
    
    char response[20];
    if (fgets(response, sizeof(response), stdin) == NULL) {
        printf("\nUninstall cancelled\n");
        return;
    }
    
    response[strcspn(response, "\n")] = 0;
    
    if (strcmp(response, "UNINSTALL") != 0) {
        printf("\nUninstall cancelled (you must type 'UNINSTALL' exactly)\n");
        return;
    }
    
    printf("\n  Starting complete uninstall...\n");
    
    snprintf(lyra_dir, sizeof(lyra_dir), "%s/.lyra", home);
    
    if (access(lyra_dir, F_OK) == 0) {
        printf("→ Removing %s...\n", lyra_dir);
        snprintf(command, sizeof(command), "rm -rf %s", lyra_dir);
        
        if (system(command) == 0) {
            printf("✓ Successfully removed ~/.lyra\n");
        } else {
            printf("✗ Error: Failed to remove ~/.lyra\n");
        }
    } else {
        printf("→ ~/.lyra directory not found (already removed)\n");
    }
    
    snprintf(lyra_binary, sizeof(lyra_binary), "/usr/local/bin/lyra");
    
    if (access(lyra_binary, F_OK) == 0) {
        printf("→ Removing %s...\n", lyra_binary);
        snprintf(command, sizeof(command), "rm %s", lyra_binary);
        
        if (system(command) == 0) {
            printf("✓ Successfully removed Lyra binary\n");
        } else {
            printf("✗ Error: Failed to remove Lyra binary\n");
            printf("  You may need to manually run: sudo rm /usr/local/bin/lyra\n");
        }
    } else {
        printf("→ Lyra binary not found (already removed)\n");
    }
    
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Lyra has been completely uninstalled!\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("\nThank you for using Lyra Package Manager!\n");
    printf("Packages installed via Lyra remain in /usr/local/bin/\n");
}

int main(int argc, char *argv[]) {
    int needs_sudo = 1;
    if (argc >= 2) {
        if (strcmp(argv[1], "-list") == 0 ||
            strcmp(argv[1], "-lv") == 0 ||
            strcmp(argv[1], "-ssl") == 0 ||
            strcmp(argv[1], "-fl") == 0 ||
            strcmp(argv[1], "--version") == 0 ||
            strcmp(argv[1], "-h") == 0 ||
            strcmp(argv[1], "--help") == 0) {
            needs_sudo = 0;
        }
    }
    
    if (needs_sudo) {
        ensure_sudo();
    }
    
    if (argc < 2) {
        printf("Lyra Package Manager v0.8 (with freeze-copy & encryption)\n\n");
        printf("Usage:\n");
        printf("  lyra -i <package> <url>               Install package (auto-mutes old version)\n");
        printf("  lyra -fc <package>                    Freeze-copy package (encrypted backup)\n");
        printf("  lyra -fl                              List all frozen copies\n");
        printf("  lyra -r <package@version>             Restore from frozen copy\n");
        printf("  lyra -frm                             Clean old frozen copies (keep latest)\n");
        printf("  lyra -rmpkg <pkg1> [pkg2] [pkg3]...  Remove packages (keeps vault copies)\n");
        printf("  lyra -rmcpkg <pkg1> [pkg2] ...       Remove packages completely (deletes vault)\n");
        printf("  lyra -list                            List installed packages\n");
        printf("  lyra -lv <package>                    List all versions of a package\n");
        printf("  lyra -m <package>                     Cycle to next muted version\n");
        printf("  lyra -m <package@version>             Switch to specific version\n");
        printf("  lyra -um <package>                    Unmute package (reactivate muted version)\n");
        printf("  lyra -ss                              Take system snapshot\n");
        printf("  lyra -ssl                             List all snapshots\n");
        printf("  lyra -rsw <date> [number]             Restore snapshot (DD-MM-YYYY)\n");
        printf("  lyra -U                               Update packages (GitHub or mirror)\n");
        printf("  lyra -clean                           NUCLEAR: Delete everything and reset\n");
        printf("  lyra -uninstall                       Completely uninstall Lyra\n");
        return 1;
    }
    
    if (strcmp(argv[1], "-i") == 0) {
        if (argc < 4) {
            printf("Usage: lyra -i <package> <url>\n");
            return 1;
        }
        install_package(argv[2], argv[3]);
    }
    else if (strcmp(argv[1], "-fc") == 0) {
        if (argc < 3) {
            printf("Usage: lyra -fc <package>\n");
            return 1;
        }
        freeze_copy_package(argv[2]);
    }
    else if (strcmp(argv[1], "-fl") == 0) {
        list_frozen_copies();
    }
    else if (strcmp(argv[1], "-r") == 0) {
        if (argc < 3) {
            printf("Usage: lyra -r <package@version>\n");
            printf("Example: lyra -r ripgrep@14.1.0\n");
            return 1;
        }
        restore_frozen_copy(argv[2]);
    }
    else if (strcmp(argv[1], "-frm") == 0) {
        cleanup_old_frozen_copies();
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
    else if (strcmp(argv[1], "-rmcpkg") == 0) {
        if (argc < 3) {
            printf("Usage: lyra -rmcpkg <package1> [package2] ...\n");
            return 1;
        }
        for (int i = 2; i < argc; i++) {
            remove_package_completely(argv[i]);
        }
    }
    else if (strcmp(argv[1], "-list") == 0) {
        db_init();
        db_list_packages();
    }
    else if (strcmp(argv[1], "-lv") == 0) {
        if (argc < 3) {
            printf("Usage: lyra -lv <package>\n");
            return 1;
        }
        db_init();
        list_versions(argv[2]);
    }
    else if (strcmp(argv[1], "-m") == 0) {
        if (argc < 3) {
            printf("Usage: lyra -m <package> or lyra -m <package@version>\n");
            return 1;
        }
        mute_package(argv[2]);
    }
    else if (strcmp(argv[1], "-um") == 0) {
        if (argc < 3) {
            printf("Usage: lyra -um <package>\n");
            return 1;
        }
        unmute_package(argv[2]);
    }
    else if (strcmp(argv[1], "-ss") == 0) {
        take_snapshot();
    }
    else if (strcmp(argv[1], "-ssl") == 0) {
        list_snapshots();
    }
    else if (strcmp(argv[1], "-rsw") == 0) {
        if (argc < 3) {
            printf("Usage: lyra -rsw <DD-MM-YYYY> [snapshot_number]\n");
            printf("Example: lyra -rsw 21-10-2025\n");
            printf("Example: lyra -rsw 21-10-2025 2\n");
            return 1;
        }
        int snapshot_num = (argc >= 4) ? atoi(argv[3]) : 1;
        restore_snapshot(argv[2], snapshot_num);
    }
    else if (strcmp(argv[1], "-U") == 0) {
        update_packages();
    }
    else if (strcmp(argv[1], "-clean") == 0) {
        clean_everything();
    }
    else if (strcmp(argv[1], "-uninstall") == 0) {
        uninstall_lyra();
    }
    else {
        printf("Unknown command: %s\n", argv[1]);
        return 1;
    }
    
    return 0; //still not adding the parentheses here :3
}
