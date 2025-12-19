#include "lyra.h"

int download_from_mirror(char *package_name, char *dest_path) {
    char command[2048];
    char metadata_url[1024];
    char temp_metadata[512];
    
    snprintf(metadata_url, sizeof(metadata_url), 
             "https://xansiva.github.io/lyra-mirror?package=%s", package_name);
    
    snprintf(temp_metadata, sizeof(temp_metadata), "/tmp/%s_metadata.json", package_name);
    
    printf("→ Fetching package metadata from mirror...\n");
    snprintf(command, sizeof(command), 
             "curl -s '%s' -o %s", metadata_url, temp_metadata);
    
    if (system(command) != 0) {
        printf("Error: Failed to fetch package metadata\n");
        return 0;
    }
    
    FILE *fp = fopen(temp_metadata, "r");
    if (!fp) {
        printf("Error: Could not read metadata\n");
        return 0;
    }
    
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char *content = malloc(size + 1);
    if (!content) {
        fclose(fp);
        return 0;
    }
    
    fread(content, 1, size, fp);
    content[size] = '\0';
    fclose(fp);
    
    cJSON *metadata = cJSON_Parse(content);
    free(content);
    
    if (!metadata) {
        printf("Error: Invalid metadata JSON\n");
        remove(temp_metadata);
        return 0;
    }
    
    cJSON *download_url_obj = cJSON_GetObjectItem(metadata, "downloadUrl");
    cJSON *version_obj = cJSON_GetObjectItem(metadata, "version");
    
    if (!download_url_obj || !download_url_obj->valuestring) {
        printf("Error: No download URL in metadata\n");
        cJSON_Delete(metadata);
        remove(temp_metadata);
        return 0;
    }
    
    char *download_url = download_url_obj->valuestring;
    char version[64] = "unknown";
    
    if (version_obj && version_obj->valuestring) {
        strncpy(version, version_obj->valuestring, sizeof(version) - 1);
        version[sizeof(version) - 1] = '\0';
    }
    
    printf("→ Downloading %s version %s from mirror...\n", package_name, version);
    
    snprintf(command, sizeof(command), 
             "curl -L -o %s '%s' 2>/dev/null", dest_path, download_url);
    
    int result = system(command);
    
    cJSON_Delete(metadata);
    
    return result == 0 ? 1 : 0;
}

void parse_install_rules(cJSON *rules_array, InstallRule *rules, int *rule_count) {
    *rule_count = 0;
    
    if (!rules_array || !cJSON_IsArray(rules_array)) {
        return;
    }
    
    int max_rules = 32;
    cJSON *rule = NULL;
    
    cJSON_ArrayForEach(rule, rules_array) {
        if (*rule_count >= max_rules) break;
        
        cJSON *type = cJSON_GetObjectItem(rule, "type");
        cJSON *source = cJSON_GetObjectItem(rule, "source");
        cJSON *destination = cJSON_GetObjectItem(rule, "destination");
        cJSON *permissions = cJSON_GetObjectItem(rule, "permissions");
        cJSON *script = cJSON_GetObjectItem(rule, "script");
        
        if (type && type->valuestring) {
            strncpy(rules[*rule_count].type, type->valuestring, 
                    sizeof(rules[*rule_count].type) - 1);
        }
        
        if (source && source->valuestring) {
            strncpy(rules[*rule_count].source, source->valuestring, 
                    sizeof(rules[*rule_count].source) - 1);
        }
        
        if (destination && destination->valuestring) {
            strncpy(rules[*rule_count].destination, destination->valuestring, 
                    sizeof(rules[*rule_count].destination) - 1);
        }
        
        if (permissions && permissions->valuestring) {
            strncpy(rules[*rule_count].permissions, permissions->valuestring, 
                    sizeof(rules[*rule_count].permissions) - 1);
        } else {
            strcpy(rules[*rule_count].permissions, "755");
        }
        
        if (script && script->valuestring) {
            strncpy(rules[*rule_count].script, script->valuestring, 
                    sizeof(rules[*rule_count].script) - 1);
        }
        
        (*rule_count)++;
    }
}

void apply_install_rules(char *package_name, char *extract_dir) {
    char metadata_path[512];
    snprintf(metadata_path, sizeof(metadata_path), "/tmp/%s_metadata.json", package_name);
    
    FILE *fp = fopen(metadata_path, "r");
    if (!fp) {
        printf("→ No install rules found, using auto-detection\n");
        return;
    }
    
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char *content = malloc(size + 1);
    if (!content) {
        fclose(fp);
        return;
    }
    
    fread(content, 1, size, fp);
    content[size] = '\0';
    fclose(fp);
    
    cJSON *metadata = cJSON_Parse(content);
    free(content);
    
    if (!metadata) {
        return;
    }
    
    cJSON *rules_array = cJSON_GetObjectItem(metadata, "installRules");
    
    if (!rules_array || cJSON_GetArraySize(rules_array) == 0) {
        printf("→ No custom install rules, using defaults\n");
        cJSON_Delete(metadata);
        find_and_install_binary(extract_dir, package_name);
        return;
    }
    
    printf("→ Applying custom install rules...\n");
    
    InstallRule rules[32];
    int rule_count = 0;
    
    parse_install_rules(rules_array, rules, &rule_count);
    
    for (int i = 0; i < rule_count; i++) {
        InstallRule *rule = &rules[i];
        char command[2048];
        
        if (strcmp(rule->type, "copy") == 0) {
            char full_source[1024];
            snprintf(full_source, sizeof(full_source), "%s/%s", extract_dir, rule->source);
            
            printf("  → Copying %s to %s\n", rule->source, rule->destination);
            
            char dest_dir[512];
            strncpy(dest_dir, rule->destination, sizeof(dest_dir) - 1);
            dest_dir[sizeof(dest_dir) - 1] = '\0';
            char *last_slash = strrchr(dest_dir, '/');
            if (last_slash) {
                *last_slash = '\0';
                snprintf(command, sizeof(command), "mkdir -p %s", dest_dir);
                system(command);
            }
            
            snprintf(command, sizeof(command), "cp %s %s", full_source, rule->destination);
            system(command);
            
            snprintf(command, sizeof(command), "chmod %s %s", 
                     rule->permissions, rule->destination);
            system(command);
        }
        else if (strcmp(rule->type, "symlink") == 0) {
            printf("  → Creating symlink %s -> %s\n", rule->destination, rule->source);
            
            snprintf(command, sizeof(command), "ln -sf %s %s", 
                     rule->source, rule->destination);
            system(command);
        }
        else if (strcmp(rule->type, "script") == 0 && strlen(rule->script) > 0) {
            printf("  → Running post-install script\n");
            
            char script_path[512];
            snprintf(script_path, sizeof(script_path), "/tmp/%s_postinstall.sh", package_name);
            
            FILE *script_fp = fopen(script_path, "w");
            if (script_fp) {
                fprintf(script_fp, "#!/bin/bash\n");
                fprintf(script_fp, "cd %s\n", extract_dir);
                fprintf(script_fp, "%s\n", rule->script);
                fclose(script_fp);
                
                chmod(script_path, 0755);
                system(script_path);
                remove(script_path);
            }
        }
    }
    
    printf("✓ Install rules applied\n");
    cJSON_Delete(metadata);
}
