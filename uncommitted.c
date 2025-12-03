#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

// ANSI color codes
#define RESET       "\033[0m"
#define BOLD        "\033[1m"
#define RED         "\033[31m"
#define GREEN       "\033[32m"
#define YELLOW      "\033[33m"
#define BLUE        "\033[34m"
#define MAGENTA     "\033[35m"
#define CYAN        "\033[36m"
#define WHITE       "\033[37m"
#define BG_BLUE     "\033[44m"

// Box drawing characters
#define TOP_LEFT    "╔"
#define TOP_RIGHT   "╗"
#define BOT_LEFT    "╚"
#define BOT_RIGHT   "╝"
#define HORIZ       "═"
#define VERT        "║"
#define T_RIGHT     "╠"
#define T_LEFT      "╣"

// Initial capacities (will grow as needed)
#define INITIAL_FILES_CAPACITY 16
#define INITIAL_REPOS_CAPACITY 8

typedef struct {
    char *filename;   // dynamically allocated
    char status;      // 'M' modified, 'A' added, 'D' deleted, '?' untracked, 'R' renamed
    int staged;       // 1 if staged, 0 if not
} FileChange;

typedef struct {
    char *path;            // dynamically allocated
    char *branch;          // dynamically allocated
    char *remote_branch;   // dynamically allocated
    char *remote_url;      // dynamically allocated (e.g., GitHub URL)
    int ahead;
    int behind;
    int has_remote;        // 1 if repo has a remote configured
    int is_pushed;         // 1 if current branch exists on remote
    FileChange *changes;   // dynamic array
    int change_count;
    int change_capacity;
    int staged_count;
    int unstaged_count;
    int untracked_count;
} GitRepo;

typedef struct {
    GitRepo *repos;        // dynamic array
    int count;
    int capacity;
} RepoList;

// Initialize a repo list
void init_repo_list(RepoList *list) {
    list->repos = malloc(INITIAL_REPOS_CAPACITY * sizeof(GitRepo));
    list->count = 0;
    list->capacity = INITIAL_REPOS_CAPACITY;
}

// Initialize a git repo struct
void init_git_repo(GitRepo *repo) {
    repo->path = NULL;
    repo->branch = NULL;
    repo->remote_branch = NULL;
    repo->remote_url = NULL;
    repo->ahead = 0;
    repo->behind = 0;
    repo->has_remote = 0;
    repo->is_pushed = 0;
    repo->changes = malloc(INITIAL_FILES_CAPACITY * sizeof(FileChange));
    repo->change_count = 0;
    repo->change_capacity = INITIAL_FILES_CAPACITY;
    repo->staged_count = 0;
    repo->unstaged_count = 0;
    repo->untracked_count = 0;
}

// Add a file change to a repo, growing array if needed
void add_file_change(GitRepo *repo, const char *filename, char status, int staged) {
    if (repo->change_count >= repo->change_capacity) {
        repo->change_capacity *= 2;
        repo->changes = realloc(repo->changes, repo->change_capacity * sizeof(FileChange));
        if (!repo->changes) {
            fprintf(stderr, "Failed to allocate memory for file changes\n");
            exit(1);
        }
    }

    FileChange *fc = &repo->changes[repo->change_count++];
    fc->filename = strdup(filename);
    fc->status = status;
    fc->staged = staged;
}

// Add a repo to the list, growing array if needed
GitRepo *add_repo(RepoList *list) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->repos = realloc(list->repos, list->capacity * sizeof(GitRepo));
        if (!list->repos) {
            fprintf(stderr, "Failed to allocate memory for repos\n");
            exit(1);
        }
    }

    GitRepo *repo = &list->repos[list->count++];
    init_git_repo(repo);
    return repo;
}

// Free a file change
void free_file_change(FileChange *fc) {
    free(fc->filename);
}

// Free a git repo
void free_git_repo(GitRepo *repo) {
    free(repo->path);
    free(repo->branch);
    free(repo->remote_branch);
    free(repo->remote_url);
    for (int i = 0; i < repo->change_count; i++) {
        free_file_change(&repo->changes[i]);
    }
    free(repo->changes);
}

// Free entire repo list
void free_repo_list(RepoList *list) {
    for (int i = 0; i < list->count; i++) {
        free_git_repo(&list->repos[i]);
    }
    free(list->repos);
}

void print_horizontal_line(int width, const char *left, const char *right) {
    printf("%s%s", CYAN, left);
    for (int i = 0; i < width - 2; i++) {
        printf("%s", HORIZ);
    }
    printf("%s%s\n", right, RESET);
}

void print_centered(const char *text, int width) {
    int len = strlen(text);
    int padding = (width - len - 2) / 2;
    printf("%s%s%s", CYAN, VERT, RESET);
    for (int i = 0; i < padding; i++) printf(" ");
    printf("%s", text);
    for (int i = 0; i < width - len - padding - 2; i++) printf(" ");
    printf("%s%s%s\n", CYAN, VERT, RESET);
}

const char *get_status_color(char status, int staged) {
    if (staged) return GREEN;
    switch (status) {
        case 'M': return YELLOW;
        case 'A': return GREEN;
        case 'D': return RED;
        case '?': return MAGENTA;
        case 'R': return BLUE;
        default: return WHITE;
    }
}

const char *get_status_label(char status, int staged) {
    if (staged) {
        switch (status) {
            case 'M': return "modified (staged)";
            case 'A': return "new file (staged)";
            case 'D': return "deleted (staged)";
            case 'R': return "renamed (staged)";
            default: return "staged";
        }
    }
    switch (status) {
        case 'M': return "modified";
        case 'A': return "new file";
        case 'D': return "deleted";
        case '?': return "untracked";
        case 'R': return "renamed";
        default: return "unknown";
    }
}

int is_git_repo(const char *path) {
    char *git_path = malloc(strlen(path) + 6); // "/.git" + null
    sprintf(git_path, "%s/.git", path);
    struct stat st;
    int result = stat(git_path, &st) == 0;
    free(git_path);
    return result;
}

void get_branch_info(const char *repo_path, GitRepo *repo) {
    char *cmd = NULL;
    size_t cmd_len;
    FILE *fp;
    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len;

    // Get current branch
    cmd_len = strlen(repo_path) + 150;
    cmd = malloc(cmd_len);
    snprintf(cmd, cmd_len, "cd \"%s\" && git rev-parse --abbrev-ref HEAD 2>/dev/null", repo_path);
    fp = popen(cmd, "r");
    if (fp) {
        if ((line_len = getline(&line, &line_cap, fp)) > 0) {
            line[strcspn(line, "\n")] = 0;
            repo->branch = strdup(line);
        }
        pclose(fp);
    }

    // Check if remote 'origin' exists and get its URL
    snprintf(cmd, cmd_len, "cd \"%s\" && git remote get-url origin 2>/dev/null", repo_path);
    fp = popen(cmd, "r");
    if (fp) {
        if ((line_len = getline(&line, &line_cap, fp)) > 0) {
            line[strcspn(line, "\n")] = 0;
            repo->remote_url = strdup(line);
            repo->has_remote = 1;
        }
        pclose(fp);
    }

    // Get remote tracking branch
    snprintf(cmd, cmd_len, "cd \"%s\" && git rev-parse --abbrev-ref --symbolic-full-name @{u} 2>/dev/null", repo_path);
    fp = popen(cmd, "r");
    if (fp) {
        if ((line_len = getline(&line, &line_cap, fp)) > 0) {
            line[strcspn(line, "\n")] = 0;
            repo->remote_branch = strdup(line);
            repo->is_pushed = 1;  // Branch has upstream, so it's been pushed
        }
        pclose(fp);
    }

    // If no tracking branch but has remote, check local refs for origin/<branch>
    // This avoids network calls by checking cached remote-tracking refs
    if (!repo->is_pushed && repo->has_remote && repo->branch) {
        snprintf(cmd, cmd_len, "cd \"%s\" && git rev-parse --verify --quiet refs/remotes/origin/%s 2>/dev/null", repo_path, repo->branch);
        fp = popen(cmd, "r");
        if (fp) {
            if ((line_len = getline(&line, &line_cap, fp)) > 0 && line_len > 1) {
                repo->is_pushed = 1;  // Branch exists in cached remote refs
            }
            pclose(fp);
        }
    }

    // Get ahead/behind counts
    snprintf(cmd, cmd_len, "cd \"%s\" && git rev-list --left-right --count HEAD...@{u} 2>/dev/null", repo_path);
    fp = popen(cmd, "r");
    if (fp) {
        if ((line_len = getline(&line, &line_cap, fp)) > 0) {
            sscanf(line, "%d\t%d", &repo->ahead, &repo->behind);
        }
        pclose(fp);
    }

    free(cmd);
    free(line);
}

// Check if a file matches gitignore patterns (even if tracked)
int is_gitignored(const char *repo_path, const char *filename) {
    char *cmd = NULL;
    size_t cmd_len = strlen(repo_path) + strlen(filename) + 100;
    cmd = malloc(cmd_len);
    // --no-index checks ignore rules even for tracked files
    snprintf(cmd, cmd_len, "cd \"%s\" && git check-ignore -q --no-index \"%s\" 2>/dev/null", repo_path, filename);
    int result = system(cmd);
    free(cmd);
    // git check-ignore returns 0 if the file matches ignore patterns
    return (WIFEXITED(result) && WEXITSTATUS(result) == 0);
}

void get_git_status(const char *repo_path, GitRepo *repo) {
    char *cmd = NULL;
    size_t cmd_len;
    FILE *fp;
    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len;

    repo->path = strdup(repo_path);

    get_branch_info(repo_path, repo);

    // Get porcelain status
    cmd_len = strlen(repo_path) + 100;
    cmd = malloc(cmd_len);
    snprintf(cmd, cmd_len, "cd \"%s\" && git status --porcelain 2>/dev/null", repo_path);
    fp = popen(cmd, "r");
    if (fp) {
        while ((line_len = getline(&line, &line_cap, fp)) > 0) {
            if (line_len < 4) continue;

            char index_status = line[0];
            char worktree_status = line[1];
            char *filename = line + 3;
            filename[strcspn(filename, "\n")] = 0;

            // Skip files that are in .gitignore
            if (is_gitignored(repo_path, filename)) {
                continue;
            }

            // Handle staged changes
            if (index_status != ' ' && index_status != '?') {
                add_file_change(repo, filename, index_status, 1);
                repo->staged_count++;
            }

            // Handle unstaged changes
            if (worktree_status != ' ' && worktree_status != '?') {
                add_file_change(repo, filename, worktree_status, 0);
                repo->unstaged_count++;
            }

            // Handle untracked files
            if (index_status == '?' && worktree_status == '?') {
                add_file_change(repo, filename, '?', 0);
                repo->untracked_count++;
            }
        }
        pclose(fp);
    }

    free(cmd);
    free(line);
}

void print_repo_info(GitRepo *repo, int box_width) {
    // Repository header
    print_horizontal_line(box_width, TOP_LEFT, TOP_RIGHT);

    // Repository path - truncate if needed for display
    printf("%s%s%s %s%s", CYAN, VERT, RESET, BOLD WHITE, repo->path);
    int path_len = strlen(repo->path);
    int padding = box_width - path_len - 3;
    if (padding < 0) padding = 0;
    for (int i = 0; i < padding; i++) printf(" ");
    printf("%s%s%s\n", RESET CYAN, VERT, RESET);

    print_horizontal_line(box_width, T_RIGHT, T_LEFT);

    // Branch info
    printf("%s%s%s  %sBranch:%s %s%s%s", CYAN, VERT, RESET, BOLD, RESET, GREEN,
           repo->branch ? repo->branch : "(unknown)", RESET);
    int len = 10 + (repo->branch ? strlen(repo->branch) : 9);

    if (repo->remote_branch && repo->remote_branch[0]) {
        printf(" -> %s%s%s", BLUE, repo->remote_branch, RESET);
        len += 4 + strlen(repo->remote_branch);
    }

    for (int i = len; i < box_width - 2; i++) printf(" ");
    printf("%s%s%s\n", CYAN, VERT, RESET);

    // Remote/Push status
    printf("%s%s%s  %sRemote:%s ", CYAN, VERT, RESET, BOLD, RESET);
    len = 10;
    if (!repo->has_remote) {
        printf("%sNo remote configured%s", RED, RESET);
        len += 20;  // "No remote configured"
    } else {
        // Check if it's a GitHub URL
        int is_github = repo->remote_url &&
            (strstr(repo->remote_url, "github.com") != NULL);

        if (is_github) {
            printf("%sGitHub%s", BLUE, RESET);
            len += 6;  // "GitHub"
        } else {
            printf("%sRemote configured%s", GREEN, RESET);
            len += 17;  // "Remote configured"
        }

        if (repo->is_pushed) {
            printf(" %s(pushed)%s", GREEN, RESET);
            len += 9;  // " (pushed)"
        } else {
            printf(" %s(not pushed)%s", YELLOW, RESET);
            len += 13;  // " (not pushed)"
        }
    }
    for (int i = len; i < box_width - 2; i++) printf(" ");
    printf("%s%s%s\n", CYAN, VERT, RESET);

    // Ahead/Behind info
    if (repo->ahead > 0 || repo->behind > 0) {
        printf("%s%s%s  ", CYAN, VERT, RESET);
        len = 2;
        if (repo->ahead > 0) {
            printf("%s↑ %d ahead%s", GREEN, repo->ahead, RESET);
            len += snprintf(NULL, 0, "↑ %d ahead", repo->ahead);
        }
        if (repo->ahead > 0 && repo->behind > 0) {
            printf("  ");
            len += 2;
        }
        if (repo->behind > 0) {
            printf("%s↓ %d behind%s", RED, repo->behind, RESET);
            len += snprintf(NULL, 0, "↓ %d behind", repo->behind);
        }
        for (int i = len; i < box_width - 2; i++) printf(" ");
        printf("%s%s%s\n", CYAN, VERT, RESET);
    }

    // Summary
    printf("%s%s%s  %sSummary:%s ", CYAN, VERT, RESET, BOLD, RESET);
    len = 11;
    if (repo->staged_count > 0) {
        printf("%s%d staged%s ", GREEN, repo->staged_count, RESET);
        len += snprintf(NULL, 0, "%d staged ", repo->staged_count);
    }
    if (repo->unstaged_count > 0) {
        printf("%s%d modified%s ", YELLOW, repo->unstaged_count, RESET);
        len += snprintf(NULL, 0, "%d modified ", repo->unstaged_count);
    }
    if (repo->untracked_count > 0) {
        printf("%s%d untracked%s", MAGENTA, repo->untracked_count, RESET);
        len += snprintf(NULL, 0, "%d untracked", repo->untracked_count);
    }
    for (int i = len; i < box_width - 2; i++) printf(" ");
    printf("%s%s%s\n", CYAN, VERT, RESET);

    print_horizontal_line(box_width, T_RIGHT, T_LEFT);

    // File list header
    printf("%s%s%s  %s%-40s  %-20s%s", CYAN, VERT, RESET, BOLD, "File", "Status", RESET);
    for (int i = 64; i < box_width - 2; i++) printf(" ");
    printf("%s%s%s\n", CYAN, VERT, RESET);

    // File list
    for (int i = 0; i < repo->change_count; i++) {
        FileChange *fc = &repo->changes[i];
        const char *color = get_status_color(fc->status, fc->staged);
        const char *status_label = get_status_label(fc->status, fc->staged);

        // Truncate filename for display if needed
        char truncated_name[41];
        if (strlen(fc->filename) > 40) {
            strncpy(truncated_name, fc->filename, 37);
            truncated_name[37] = '\0';
            strcat(truncated_name, "...");
        } else {
            strncpy(truncated_name, fc->filename, 40);
            truncated_name[40] = '\0';
        }

        printf("%s%s%s  %s%-40s%s  %s%-20s%s",
               CYAN, VERT, RESET,
               color, truncated_name, RESET,
               color, status_label, RESET);

        for (int j = 64; j < box_width - 2; j++) printf(" ");
        printf("%s%s%s\n", CYAN, VERT, RESET);
    }

    print_horizontal_line(box_width, BOT_LEFT, BOT_RIGHT);
    printf("\n");
}

void scan_directories(const char *path, RepoList *list) {
    // Check if this directory is a git repo with changes
    if (is_git_repo(path)) {
        GitRepo *repo = add_repo(list);
        get_git_status(path, repo);

        // If no changes, remove it
        if (repo->change_count == 0) {
            free_git_repo(repo);
            list->count--;
        }
        return; // Don't recurse into .git subdirectories
    }

    DIR *dir = opendir(path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip hidden directories and special entries
        if (entry->d_name[0] == '.') continue;

        // Build full path dynamically
        size_t full_path_len = strlen(path) + strlen(entry->d_name) + 2;
        char *full_path = malloc(full_path_len);
        snprintf(full_path, full_path_len, "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            scan_directories(full_path, list);
        }

        free(full_path);
    }

    closedir(dir);
}

void print_header(int width) {
    printf("\n");
    print_horizontal_line(width, TOP_LEFT, TOP_RIGHT);
    printf("%s%s%s", CYAN, VERT, RESET);

    const char *title = "  GIT UNCOMMITTED CHANGES SCANNER  ";
    int title_len = strlen(title);
    int padding = (width - title_len - 2) / 2;

    for (int i = 0; i < padding; i++) printf(" ");
    printf("%s%s%s%s", BOLD, BG_BLUE, title, RESET);
    for (int i = 0; i < width - title_len - padding - 2; i++) printf(" ");
    printf("%s%s%s\n", CYAN, VERT, RESET);

    print_horizontal_line(width, BOT_LEFT, BOT_RIGHT);
    printf("\n");
}

void print_summary(int total_repos, int total_staged, int total_unstaged, int total_untracked, int width) {
    print_horizontal_line(width, TOP_LEFT, TOP_RIGHT);

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "SUMMARY: %d repositories with uncommitted changes", total_repos);
    print_centered(buffer, width);

    print_horizontal_line(width, T_RIGHT, T_LEFT);

    printf("%s%s%s  %s%d%s staged  |  %s%d%s modified  |  %s%d%s untracked",
           CYAN, VERT, RESET,
           GREEN, total_staged, RESET,
           YELLOW, total_unstaged, RESET,
           MAGENTA, total_untracked, RESET);

    int len = 50;
    for (int i = len; i < width - 2; i++) printf(" ");
    printf("%s%s%s\n", CYAN, VERT, RESET);

    print_horizontal_line(width, BOT_LEFT, BOT_RIGHT);
    printf("\n");
}

int main(int argc, char *argv[]) {
    char *start_path = NULL;
    int box_width = 80;

    if (argc > 1) {
        start_path = strdup(argv[1]);
    } else {
        start_path = getcwd(NULL, 0);  // POSIX extension: allocates buffer automatically
        if (!start_path) {
            perror("getcwd");
            return 1;
        }
    }

    printf("%sScanning for git repositories with uncommitted changes...%s\n", YELLOW, RESET);

    RepoList list;
    init_repo_list(&list);

    scan_directories(start_path, &list);

    if (list.count == 0) {
        printf("\n%s%s✓ No uncommitted changes found in any git repository!%s\n\n", BOLD, GREEN, RESET);
        free(start_path);
        free_repo_list(&list);
        return 0;
    }

    print_header(box_width);

    int total_staged = 0, total_unstaged = 0, total_untracked = 0;

    for (int i = 0; i < list.count; i++) {
        print_repo_info(&list.repos[i], box_width);
        total_staged += list.repos[i].staged_count;
        total_unstaged += list.repos[i].unstaged_count;
        total_untracked += list.repos[i].untracked_count;
    }

    print_summary(list.count, total_staged, total_unstaged, total_untracked, box_width);

    free(start_path);
    free_repo_list(&list);

    return 0;
}
