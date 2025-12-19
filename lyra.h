#ifndef LYRA_H
#define LYRA_H

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

// Install rules structure
typedef struct {
    char type[32];
    char source[512];
    char destination[512];
    char permissions[16];
    char script[1024];
} InstallRule;

// Utility functions
char* get_user_home();
void ensure_sudo();

// Database functions
void db_init();
cJSON* db_read();
void db_write(cJSON *root);
void db_add_package(char *name, char *version, char *url);
void db_remove_package(char *name);
void db_list_packages();
void list_versions(char *package_name);
char* get_package_policy(char *package_name);

// Package installation
void install_package(char *package_name, char *url);
void install_package_with_mirror(char *package_name, char *url);
void remove_package(char *package_name);
void remove_package_completely(char *package_name);
void update_packages();

// Mirror and install rules
int download_from_mirror(char *package_name, char *dest_path);
void parse_install_rules(cJSON *rules_array, InstallRule *rules, int *rule_count);
void apply_install_rules(char *package_name, char *extract_dir);

// GitHub integration
int extract_github_repo(char *url, char *owner, char *repo);
int get_latest_github_release(char *owner, char *repo, char *url_out, char *version_out);
void extract_version_from_url(char *url, char *version_out);

// Vault and backup
void backup_to_vault(char *package_name, char *version);
void find_and_install_binary(char *extract_dir, char *package_name);

// Freeze-copy and encryption
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

// Snapshots
void take_snapshot();
void list_snapshots();
void restore_snapshot(char *date, int number);

// Muting
void mute_package(char *arg);
void unmute_package(char *package_name);

// System management
void clean_everything();
void uninstall_lyra();

#endif
