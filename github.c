#include "lyra.h"

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
