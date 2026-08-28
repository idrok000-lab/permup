# PermUp

An alternative to `sudo` and `pkexec`

---

## Looking for Developers!

**Permup is a promising project, but I need help!** The concept is solid – a unified replacement for both `sudo` and `pkexec` with group-based permission management, client-server architecture, and strong security foundations. The code works, but there are issues that need fixing.

### Current Problems

- Authentication and the subsequent execution part are **infinite** – sessions don't terminate properly
- After each daemon start, you need to manually run `chmod 666 /run/permup/socket` (temporary workaround)
- Zombie processes appear despite `SIGCHLD` handling
- PTY passing via `SCM_RIGHTS` works but has sporadic issues

### What I Need

- **C/C++ developers** familiar with Unix/Linux system programming (PAM, PTY, Unix sockets, process management)
- **Optimization specialists** to improve performance and resource management
- **Testers** for various distributions and init systems (systemd, OpenRC, runit, SysVinit)
- **GUI developers** for graphical clients (`permup-gui`, `permup-gnome`, `permup-kde`)

### Collaboration Rules

1. All changes must comply with the existing architecture
2. If you believe an architecture change is necessary – **contact me first** to discuss it
3. Priority: fixing bugs → stability → performance → new features
4. Test changes on at least two different init systems

---

## 1. PURPOSE

To replace `sudo` and `pkexec` with a single tool that:
- Works independently of the init system (systemd, OpenRC, runit, SysVinit)
- Uses groups as the sole permission carrier
- Works in both CLI and GUI
- **Does not work remotely** – only locally via a Unix socket; remote access is via SSH, then through `permup`

## 2. OVERALL ARCHITECTURE

```
┌─────────────────────────────────────────────────────────────┐
│                         SYSTEM                              │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────┐                             ┌───────────┐ │
│  │    permup    │ ──────────────────────────┐ │  permupd  │ │
│  │   (client)   │                           │ │  (daemon) │ │
│  └──────────────┘                           │ └─────┬─────┘ │
│                                             │       │       │
│                                       Unix Socket   │       │
│                                      /run/permup/   │       │
│  ┌──────────────┐                           │       │       │
│  │  permup-gui  │ ──────────────────────────┘       │       │
│  │   (client)   │                                   │       │
│  └──────────────┘                                   │       │
│                                              ┌──────▼─────┐ │
│                                              │   Child    │ │
│                                              │ (process)  │ │
│                                              │  runuser   │ │
│                                              │ + command  │ │
│                                              │  ; exit    │ │
│                                              └────────────┘ │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

## 3. COMPONENTS

### 3.1. `permupd` – daemon

- **Startup:** by the init system (systemd, OpenRC, runit, SysVinit) as root
- **Function:** listens on the Unix socket, authenticates, checks permissions, creates children
- **Model:** single process, `fork()` on each connection, parent returns to listening
- **Zombie reaping:** `SIGCHLD` or `waitpid(-1, WNOHANG)` in the loop
- **New request:** `list_users` – returns the list of allowed `-u` and `-h` for the given user
- **Rule for root:** If `-h root` → **skip all checks** (permissions, time, locks, shells)
- **Configuration files:**
  - `/etc/permup/permup.cfg` – permissions (groups, lists, user locks)
  - `/etc/permup/permdown.cfg` – timeouts for commands
  - `/etc/permup/shell.rc` – shell prompt patterns
  - `/etc/permup/permup.time` – time restrictions for groups
  - `/etc/permup/permup.session` – password validity time (client-side)
  - `/etc/permup/permup.ratelimit` – rate limiting (client-side)

### 3.2. `permup` – CLI client

- **Function:** connects to `permupd`, retrieves the list of allowed users, sends the command, password, receives PTY
- **Operation:**
  - Checks if it has a terminal (`isatty()`)
  - If yes → interactive mode (PTY)
  - **First** sends a `list_users` request to `permupd`
  - Receives the list of allowed `-u` and `-h`
  - Checks if the provided `-u` and `-h` are allowed
  - If not → displays the appropriate error message
  - If yes → checks rate limiting (`/etc/permup/permup.ratelimit`)
    - If the user is locked → displays a message and exits
    - If not → proceeds
  - Checks if `-h` is **root**
    - If **yes** → **always asks for the root password** (does not use `/etc/permup.session`)
    - If **no** → checks if a valid session exists for the pair `(-h, -u)` (according to `/etc/permup.session`)
      - If yes → sends the remembered password automatically
      - If no → asks for the password interactively
  - Sends the actual request to the daemon
  - Receives the master PTY via `SCM_RIGHTS`
  - Takes over the terminal and handles the session
- **Syntax:**
  ```bash
  permup [-u target_user] [-h authenticating_user] command [arguments]
  permup -l
  ```

- `-u` – target user to run the command as (default: root)
- `-h` – user whose password is used for authentication (default: root)
- `-l` – displays the list of allowed users
- The password is always provided interactively after pressing Enter, just like in sudo and pkexec – no exceptions
- Example: `permup -u janusz -h root htop` → run as janusz, authenticate with root's password

### 3.3. `permup-gui` – GUI client

- **Function:** connects to `permupd`, retrieves the list of allowed users, displays a dialog window, sends the command and password
- **Operation:**
  - Sends a `list_users` request to `permupd`
  - Receives the list of allowed `-u` and `-h`
  - Displays a dialog window with:
    - A dropdown list for `-u` (only allowed users)
    - A dropdown list for `-h` (only allowed users)
    - A password field
    - OK/Cancel button
  - After confirmation, checks rate limiting (`/etc/permup/permup.ratelimit`)
    - If the user is locked → displays a message and exits
    - If not → proceeds
  - Checks if `-h` is root
    - If yes → always asks for the root password (does not use `/etc/permup.session`)
    - If no → checks if a valid session exists for the pair `(-h, -u)`
      - If yes → sends the remembered password automatically
      - If no → asks for the password in the dialog window
  - Sends the actual request to `permupd`
  - Receives the output and displays it in a window (or in the console from which it was launched)
- **Use case:** `.desktop` files, menu shortcuts, Nautilus

## 4. CONFIGURATION

### 4.1. `/etc/permup/permup.cfg` – permissions

Structure:

```
group: {
    mode: darklist | whitelist
    list: {
        command1,
        command2,
        ...
    }
    except: {
        command3,
        command4,
        ...
    }
    blocked_users: {
        user1,
        user2,
        ...
    }
    allowed_users: {
        user1,
        user2,
        ...
    }
}
```

General rules:

- `darklist: {}` → empty = everything allowed
- `whitelist: {}` → empty = nothing allowed
- `except` always reverses the main rule:
  - For darklist: `except` = allowed (even though on the list)
  - For whitelist: `except` = blocked (even though on the list)

Command matching:

- Flag normalization (alphabetical sorting)
- `rm -rf` = `rm -fr` = `rm -f -r` = same
- Path canonicalization (`realpath`)
- Substring matching: `rm -r` blocks `rm -rf`, `rm -fr`, `rm -r -f`

Rules for target users:

- `blocked_users` – blacklist: these users are blocked
- `allowed_users` – whitelist: only these users are allowed
- If both sections are defined → `allowed_users` takes precedence, `blocked_users` is ignored
- If neither is defined → no restrictions (any target user can be used)
- If the section is empty → no restrictions

Example:

```
adm: {
    darklist: {
        rm -rf,
        reboot,
        shutdown
    },
    except: {
        rm -rf /var/cache/pacman,
        rm -rf /tmp
    },
    blocked_users: {
        root
    }
}

dev: {
    whitelist: {
        docker,
        systemctl,
        journalctl
    },
    except: {
        docker run --privileged,
        systemctl disable
    },
    allowed_users: {
        marek,
        ola
    }
}
```

Default group:

- `adm` has `darklist: {}` (everything allowed)

Group merging:

- A user can belong to multiple groups
- Permissions are cumulative (set union)
- If conflict (allowed vs blocked) → allowed wins

Users without groups:

- No permissions → denial

Root user:

- Not subject to `/etc/permup/permup.cfg`
- Has all permissions by default – can execute any command as any user

### 4.2. `/etc/permup/permdown.cfg` – timeouts

Structure:

```
default: 120s

pacman -Syu {
    1000s
}

systemctl restart nginx {
    30s
}

htop {
    20s
}
```

Rules:

- `default` – used when the command is not defined
- Timeout applies to establishing a connection with the calling terminal
- If the return connection cannot be established within the timeout → the child kills the command and exits
- For GUI (no terminal) → timeout acts as the maximum execution time

### 4.3. `/etc/permup/shell.rc` – shell prompt patterns

Structure:

```
[root@
[user@
$
#
>
%
bash-
zsh-
fish-
sh-
```

Rules:

- Each line is a text pattern that may appear in the PTY output
- If any pattern is found in the command output → we consider that a shell has been started
- The file is read by the child before passing the PTY to the client
- Admin can add their own patterns (e.g., `[admin@`, `(venv)`)
- Root (`-h root`) skips this test – can run shells without restrictions

### 4.4. `/etc/permup/permup.time` – time restrictions

Structure:

```
group: {
    allowed: "08:00-18:00",
    days: "Mon-Fri"
}

dev: {
    allowed: "10:00-16:00",
    days: "Mon-Fri"
}

backup: {
    allowed: "00:00-23:59",
    days: "Mon-Sun"
}
```

Rules:

- If a group has no entry in `/etc/permup/permup.time` → no time restrictions (default 24/7)
- If a group has an entry → `permupd` checks the current time and day of the week
- If the current time falls within the range and the day matches → allows
- If not → denies with the message: "Time permissions for this group are not available at this moment."
- If the user belongs to multiple groups → any group that allows access at the given time → permission is valid
- Root (`-h root`) ignores `/etc/permup/permup.time` – time restrictions are not checked

### 4.5. `/etc/permup/permup.session` – password validity time (client-side)

Structure:

```
# Time in seconds
adm: {
    session_time: 600
}

dev: {
    session_time: 300
}
```

Rules:

- `session_time` – time in seconds during which the client (`permup` or `permup-gui`) remembers the password for a specific pair `(-h, -u)` and sends it automatically on subsequent calls with the same pair
- If the group has no entry → ask for the password every time (default: no memory)
- If `session_time = 0` → disabled (always ask)
- If `session_time > 0` → the client remembers the password for that time (counted from the last authentication for that pair)
- Each pair `(-h, -u)` has its own independent session
- Changing the pair `(-h, -u)` → old session is closed, a new one is opened (after providing the correct password for the new pair)
- Works exclusively on the client side – `permupd` has no knowledge of the session
- Root (`-h root`) does not use `/etc/permup.session` – always asks for the password
- Sessions are independent for each calling user – X and Kowalski have their own separate sets of sessions

### 4.6. `/etc/permup/permup.ratelimit` – rate limiting (client-side)

Structure:

```
# Default settings
default_n: 3
default_x: 30

# Settings for specific users (override defaults)
marek {
    n: 5
    x: 60
}

janusz {
    n: 2
    x: 10
}
```

Rules:

- `default_n` – default number of failed attempts before lockout (for all users)
- `default_x` – default lockout time in seconds (for all users)
- Settings for a specific user (e.g., `marek { }`) override default values
- `n` – number of failed authentication attempts (password) before lockout
- `x` – time in seconds for which the user is locked after exceeding `n`
- Counter is reset after successful authentication or after `x` expires
- Applies exclusively on the client side – `permupd` has no knowledge of the lockout
- Root (`-h root`) is not subject to rate limiting – can always try

## 5. FLOW OF OPERATION

### 5.1. System startup

1. Init starts `permupd` as root
2. `permupd` opens `/run/permup/socket` (permissions: 0600, root only)
3. `permupd` enters the listening loop

### 5.2. Invocation by user (CLI)

1. User types: `permup htop`
2. `permup` connects to `/run/permup/socket`
3. `permup` sends a `list_users` request to `permupd`
4. `permupd` checks the groups of the calling user and returns the list of allowed `-u` and `-h`
5. `permup` checks if the provided `-u` (default root) and `-h` (default root) are allowed
   - If `-h root` → skips all checks (has everything)
   - If `-u` not allowed → "You do not have permissions as [user]."
   - If `-h` not allowed → "You do not have permissions to authenticate as [user]."
   - If both not allowed → "You do not have permissions as [user] nor to authenticate as [user]."
6. If OK → `permup` checks rate limiting (`/etc/permup/permup.ratelimit`):
   - If the user is locked → displays: "Too many failed attempts. Try again in Xs." and exits
   - If not → proceeds
7. `permup` checks if `-h` is root:
   - If yes → always asks for the root password (does not use `/etc/permup.session`)
   - If no → checks `/etc/permup.session` for the pair `(-h, -u)`:
     - If session exists and has not expired → sends the remembered password automatically
     - If session expired or does not exist → asks for the password interactively
   - Changing the pair `(-h, -u)` relative to the previous call causes the old session to be closed and a new one opened (after providing the correct password)
8. `permup` sends the actual request to `permupd` (with the password)
9. `permupd` authenticates the password (PAM, `/etc/shadow`)
   - If the password is incorrect → the client increments the failed attempt counter (rate limiting)
   - If the password is correct → the client resets the failed attempt counter
10. If OK → the parent performs `fork()`
11. Parent returns to listening
12. Child:
    - Creates PTY (`posix_openpt()`)
    - Runs `runuser -l target_user -c "htop; exit"` on the slave PTY
    - Reads from the master PTY for a short time (0.5–1s)
    - Checks if the output contains a pattern from `/etc/permup/shell.rc`
      - If `-h root` → skips this test
      - If `-h != root` and pattern detected → kills the process, closes PTY, returns error
      - If not detected → proceeds to the next step
    - Attempts to establish a connection with the calling terminal
    - Starts the timeout from `/etc/permup/permdown.cfg`
13. If connection is established within the timeout → session continues
14. User uses `htop` as the target user
15. After `htop` is closed:
    - Child exits (`exit`)
    - Client closes the connection
    - Parent cleans up resources

### 5.3. Invocation via GUI

1. User runs a `.desktop` file with `Exec=permup-gui htop`
2. `permup-gui` connects to `permupd` and sends a `list_users` request
3. `permupd` checks the groups of the calling user and returns the list of allowed `-u` and `-h`
4. `permup-gui` displays a dialog window with:
   - A dropdown list for `-u` (only allowed users)
   - A dropdown list for `-h` (only allowed users)
   - A password field
   - OK/Cancel button
5. User selects, types the password, confirms
6. `permup-gui` checks rate limiting (`/etc/permup/permup.ratelimit`):
   - If the user is locked → displays a message and exits
   - If not → proceeds
7. `permup-gui` checks if `-h` is root:
   - If yes → always asks for the root password (does not use `/etc/permup.session`)
   - If no → checks `/etc/permup.session` for the pair `(-h, -u)`:
     - If session exists and has not expired → sends the remembered password automatically
     - If session expired or does not exist → asks for the password in the dialog window
   - Changing the pair closes the old session and opens a new one
8. `permup-gui` sends the actual request to `permupd` (with the selected options and password)
9. `permupd` authenticates, checks permissions, forks the child
10. Child creates PTY, runs `runuser -l target_user -c "htop; exit"`
11. Child reads from the master PTY and checks patterns from `/etc/permup/shell.rc`
    - If `-h root` → skips the test
    - If `-h != root` and pattern detected → kills the process, returns error
12. Attempts to establish a connection with the calling terminal
13. Client has no terminal, so it cannot establish a connection
14. Timeout from `/etc/permup/permdown.cfg` counts down (e.g., 120s)
15. If `htop` finishes before the timeout → OK, result returned
16. If timeout expires → child kills `htop`, exits, returns error

### 5.4. PTY and timeout – one phase

- Only one phase: establishing a connection with the calling terminal
- Timeout applies from the moment the command is started until the return connection is established
- If connection is established → session continues until the command is closed
- If not → after the timeout, the command is killed
- Oneshot also works through PTY – there is no separate mode without PTY

### 5.5. Shell blocking

- **Purpose:** to prevent running shells (bash, sh, zsh, fish, mc, etc.) when `-h != root` and `-u == root`
- **Method:** child reads the PTY output and checks patterns from `/etc/permup/shell.rc`
- **Exception:** `-h root` → skip the test (root can run shells)
- **Robustness:** the method works independently of the filename, symbolic links, binary copies – because it detects behavior (prompt), not the name

### 5.6. User list (`list_users`)

- `permupd` provides a `list_users` request, which for a given calling user returns:
  - List of allowed target users (`-u`)
  - List of allowed authenticating users (`-h`)
- If `-h root` → returns all system users
- `permup` (CLI):
  - Retrieves the list in the background (without displaying) before each call
  - Checks if the provided `-u` and `-h` are allowed
  - If not → displays the appropriate error message
  - `permup -l` → displays the list explicitly
- `permup-gui` (GUI):
  - Retrieves the list and builds the dialog window with dropdown lists based on it

### 5.7. Session for the pair `(-h, -u)`

- Session is assigned to a specific pair (authenticating user `-h`, target user `-u`)
- Each pair has its own independent timer (according to `/etc/permup.session`)
- If the user calls `permup` with the same pair and the session has not expired → password sent automatically
- If the user calls `permup` with a different pair → old session is closed, a new one is opened (after providing the correct password)
- Sessions are independent for each calling user – X and Kowalski have their own separate sets of sessions
- Root (`-h root`) does not use sessions – always asks for the password

### 5.8. Rate limiting (lockout after failed attempts)

- **Purpose:** protection against brute-force attacks
- **Method:** client counts failed authentication attempts for each user
- If the counter reaches `n` (from `/etc/permup/permup.ratelimit`), the user is locked for `x` seconds
- Message: "Too many failed attempts. Try again in Xs."
- Counter resets after successful authentication or after `x` expires
- Root (`-h root`) is not subject to rate limiting

## 6. SECURITY

1. `permupd` runs as root, but does not execute commands – only forks children
2. Children execute commands via `runuser` (secure user switching)
3. Child inherits root privileges ONLY when authentication was performed with the root password (`-h root`). Otherwise, it inherits the privileges of the authenticating user.
4. `permup` does not have SUID – does not run with elevated privileges
5. Unix socket has permissions 0600 – only root can connect (or the `permup` group)
6. PTYs created by children, passed via `SCM_RIGHTS` – secure
7. Timeout kills hanging processes – no zombies
8. Passwords are not stored – only verified via PAM
9. GUI cannot take over PTY – no risk of terminal takeover by unauthorized processes
10. Always `; exit` – guarantees session closure after the command
11. Root has everything – not subject to config, preventing accidental access lockout
12. Password always interactive – no way to provide the password on the command line (safer)
13. Shell blocking – prevents running shells with non-root authentication
14. PTY-based verification – reliable, resistant to symbolic links and binary copies
15. Time restrictions – additional security layer (ignored by root)
16. User locks (`blocked_users` / `allowed_users`) – fine-grained control over which target users can be used
17. Rate limiting – protection against password brute-forcing

---

## Why Configuration Based Solely on User Groups?

The answer is simple:

- **Ease of management** – Instead of defining permissions for each user individually, assign the user to the appropriate group. This reduces configuration entries from `n` users × `m` permissions to `g` groups × `m` permissions.
- **Convenience** – Adding a new user is one command: `usermod -aG group user`. No config file editing.
- **Control** – Groups are a proven mechanism in Unix/Linux. Administrators know and trust it.
- **Security** – A unified group system reduces the risk of configuration errors. No accidental permission grants.
- **Scalability** – In large environments (companies, universities), group-based management is the only sensible approach.
- **Consistency** – All system tools use groups – permup goes further by making groups the **sole** permission carrier.

## Why Invest Time in Permup?

The idea of permup as an alternative to both `sudo` and `pkexec` is good. I've fixed many bugs already, but there are still many left. Searching for issues takes too much time alone. A developer support team would really help.

### Why It's Worth It

1. **Unified authorization** – One tool for CLI and GUI instead of two (`sudo` + `pkexec`)
2. **Group-based management** – Simpler, more controlled, and more secure
3. **Independence from init system** – Works everywhere
4. **Security by design** – No SUID, PAM authentication, rate limiting, shell blocking
5. **Client-server architecture** – Clear separation of concerns

## Future Plans

I want to develop various graphical clients:
- `permup-gui` – universal graphical client
- `permup-gnome` – native GNOME integration
- `permup-kde` – native KDE integration
- Other creative ideas

The goal: permup becomes the default authorization tool in Linux systems.

---

**Contact:** If you want to join, have questions, or want to report an issue – contact me via https://github.com/idrok000-lab/permup/issues or via e-mail:  zagrzeb456@int.pl. Any help is invaluable!
