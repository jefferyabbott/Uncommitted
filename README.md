# uncommitted

A command-line tool that recursively scans directories to find git repositories with uncommitted changes and displays the results in a visually appealing format.

## Features

- Recursively scans directories for git repositories
- Detects staged, modified, untracked, and deleted files
- Shows current branch and remote tracking branch
- Displays ahead/behind status relative to remote
- Shows remote push status (whether the repo has been pushed to GitHub or other remotes)
- Color-coded output for easy scanning
- Unicode box-drawing characters for a clean look

## Support This Project

If you find Uncommitted useful, consider buying the developer a coffee!

[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-support-yellow?style=for-the-badge&logo=buy-me-a-coffee)](https://buymeacoffee.com/jefferyabbott)

<img src="assets/buymeacoffee-qr.png" alt="Buy Me A Coffee QR Code" width="200">

## Screenshot

```
╔══════════════════════════════════════════════════════════════════════════════╗
║ /Users/you/projects/my-app                                                   ║
╠══════════════════════════════════════════════════════════════════════════════╣
║  Branch: main -> origin/main                                                 ║
║  Remote: GitHub (pushed)                                                     ║
║  ↑ 2 ahead                                                                   ║
║  Summary: 3 staged 5 modified 2 untracked                                    ║
╠══════════════════════════════════════════════════════════════════════════════╣
║  File                                      Status                            ║
║  src/app.js                                modified (staged)                 ║
║  src/utils.js                              modified                          ║
║  config.json                               untracked                         ║
╚══════════════════════════════════════════════════════════════════════════════╝
```

## Color Coding

| Color   | Meaning                          |
|---------|----------------------------------|
| Green   | Staged files / Pushed to remote  |
| Yellow  | Modified (unstaged) / Not pushed |
| Magenta | Untracked files                  |
| Red     | Deleted files / No remote        |
| Blue    | Remote branches / GitHub         |

## Compiling

Compile the program using gcc:

```bash
gcc -o uncommitted uncommitted.c -Wall
```

## Installation

### Recommended: /usr/local/bin (system-wide)

Copy the compiled binary to `/usr/local/bin` so it's available in your PATH:

```bash
sudo cp uncommitted /usr/local/bin/
```

### Alternative: ~/bin (user-only)

If you prefer not to use `sudo`, create a personal bin directory:

```bash
mkdir -p ~/bin
cp uncommitted ~/bin/
```

Then add this line to your `~/.zshrc` (or `~/.bashrc` if using bash):

```bash
export PATH="$HOME/bin:$PATH"
```

Reload your shell configuration:

```bash
source ~/.zshrc
```

## Usage

```bash
# Scan from current directory
uncommitted

# Scan from a specific directory
uncommitted /path/to/directory
```

## Example Output

The tool displays:
1. A header banner
2. Each repository with uncommitted changes showing:
   - Repository path
   - Current branch and remote tracking branch
   - Remote status: GitHub (pushed), GitHub (not pushed), Remote configured (pushed/not pushed), or No remote configured
   - Ahead/behind commit counts
   - Summary of changes (staged, modified, untracked counts)
   - List of changed files with their status
3. A summary footer with totals across all repositories

## Requirements

- macOS or Linux
- gcc compiler
- git (installed and available in PATH)

## License

MIT
