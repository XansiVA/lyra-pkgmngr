#include "lyra.h"

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
