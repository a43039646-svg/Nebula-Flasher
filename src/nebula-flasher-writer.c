#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHUNK_SIZE (4U * 1024U * 1024U)

static void fail(const char *message) {
    fprintf(stderr, "ERROR: %s\n", message);
}

static char *capture_command(char *const argv[]) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return NULL;
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]); close(pipefd[1]);
        return NULL;
    }
    if (pid == 0) {
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) _exit(127);
        close(pipefd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(pipefd[1]);
    size_t used = 0, capacity = 256;
    char *output = malloc(capacity);
    if (!output) {
        close(pipefd[0]);
        (void)waitpid(pid, NULL, 0);
        return NULL;
    }
    for (;;) {
        if (used + 128 + 1 > capacity) {
            size_t next_capacity = capacity * 2;
            char *next = realloc(output, next_capacity);
            if (!next) { free(output); close(pipefd[0]); (void)waitpid(pid, NULL, 0); return NULL; }
            output = next;
            capacity = next_capacity;
        }
        ssize_t n = read(pipefd[0], output + used, capacity - used - 1);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        used += (size_t)n;
    }
    close(pipefd[0]);
    output[used] = '\0';
    int status = 0;
    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        free(output);
        return NULL;
    }
    while (used > 0 && (output[used - 1] == '\n' || output[used - 1] == '\r' || output[used - 1] == ' '))
        output[--used] = '\0';
    return output;
}

static bool filesystem_path_uses_device(const char *filesystem_path, const char *device_path) {
    char *findmnt_argv[] = {"findmnt", "-n", "-o", "SOURCE", "-T", (char *)filesystem_path, NULL};
    char *source = capture_command(findmnt_argv);
    if (!source || strncmp(source, "/dev/", 5) != 0) {
        free(source);
        return false;
    }
    char *bracket = strchr(source, '[');
    if (bracket) *bracket = '\0';
    char *lsblk_argv[] = {"lsblk", "-s", "-n", "-p", "-o", "PATH", source, NULL};
    char *ancestors = capture_command(lsblk_argv);
    free(source);
    if (!ancestors) return false;

    bool matches = false;
    char *saveptr = NULL;
    for (char *line = strtok_r(ancestors, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        char *candidate = line;
        while (*candidate == ' ' || *candidate == '\t') candidate++;
        char *end = candidate + strlen(candidate);
        while (end > candidate && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
        char candidate_real[PATH_MAX];
        if (realpath(candidate, candidate_real) && strcmp(candidate_real, device_path) == 0) {
            matches = true;
            break;
        }
    }
    free(ancestors);
    return matches;
}

static bool is_backing_device_for_root(const char *device_path) {
    char *findmnt_argv[] = {"findmnt", "-n", "-o", "SOURCE", "/", NULL};
    char *root_source = capture_command(findmnt_argv);
    if (!root_source || strncmp(root_source, "/dev/", 5) != 0) {
        free(root_source);
        return false;
    }
    char *bracket = strchr(root_source, '[');
    if (bracket) *bracket = '\0';
    char *lsblk_argv[] = {"lsblk", "-s", "-n", "-p", "-o", "PATH", root_source, NULL};
    char *ancestors = capture_command(lsblk_argv);
    free(root_source);
    if (!ancestors) return false;

    bool matches = false;
    char *saveptr = NULL;
    for (char *line = strtok_r(ancestors, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        char *candidate = line;
        while (*candidate == ' ' || *candidate == '\t') candidate++;
        char *end = candidate + strlen(candidate);
        while (end > candidate && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
        char candidate_real[PATH_MAX];
        if (realpath(candidate, candidate_real) && strcmp(candidate_real, device_path) == 0) {
            matches = true;
            break;
        }
    }
    free(ancestors);
    return matches;
}

static bool read_small_file(const char *path, char *buf, size_t size) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    ssize_t n = read(fd, buf, size - 1);
    close(fd);
    if (n <= 0) return false;
    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' '))
        buf[--n] = '\0';
    return true;
}

static bool is_usb_or_removable(const char *device_path) {
    char resolved[PATH_MAX];
    if (!realpath(device_path, resolved)) return false;
    const char *base = strrchr(resolved, '/');
    if (!base || !base[1]) return false;
    base++;

    char sys_path[PATH_MAX];
    char value[128];
    snprintf(sys_path, sizeof sys_path, "/sys/class/block/%s/removable", base);
    if (read_small_file(sys_path, value, sizeof value) && strcmp(value, "1") == 0)
        return true;

    snprintf(sys_path, sizeof sys_path, "/sys/class/block/%s/device", base);
    char sys_real[PATH_MAX];
    if (realpath(sys_path, sys_real) && strstr(sys_real, "/usb"))
        return true;
    return false;
}

static bool is_child_partition_name(const char *disk_name, const char *entry_name) {
    size_t n = strlen(disk_name);
    if (strncmp(entry_name, disk_name, n) != 0 || entry_name[n] == '\0') return false;
    if (entry_name[n] >= '0' && entry_name[n] <= '9') return true;
    return entry_name[n] == 'p' && entry_name[n + 1] >= '0' && entry_name[n + 1] <= '9';
}

static bool is_partition(const char *device_path) {
    char resolved[PATH_MAX];
    if (!realpath(device_path, resolved)) return true;
    const char *base = strrchr(resolved, '/');
    if (!base || !base[1]) return true;
    char partition_file[PATH_MAX];
    snprintf(partition_file, sizeof partition_file, "/sys/class/block/%s/partition", base + 1);
    return access(partition_file, F_OK) == 0;
}

static bool unmount_target_children(const char *disk_path) {
    char resolved[PATH_MAX];
    if (!realpath(disk_path, resolved)) return false;
    const char *base_ptr = strrchr(resolved, '/');
    if (!base_ptr || !base_ptr[1]) return false;
    char disk_name[NAME_MAX];
    snprintf(disk_name, sizeof disk_name, "%s", base_ptr + 1);

    /* Refuse if any partition on this device backs a critical system mount. */
    FILE *mounts = fopen("/proc/self/mountinfo", "r");
    if (!mounts) {
        fail("Cannot inspect mounted filesystems.");
        return false;
    }
    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, mounts) != -1) {
        unsigned int maj = 0, min = 0;
        char mountpoint[PATH_MAX] = {0};
        if (sscanf(line, "%*s %*s %u:%u %*s %4095s", &maj, &min, mountpoint) != 3)
            continue;
        char decoded[PATH_MAX];
        size_t j = 0;
        for (size_t i = 0; mountpoint[i] && j + 1 < sizeof decoded; i++) {
            if (mountpoint[i] == '\\' && mountpoint[i+1] && mountpoint[i+2] && mountpoint[i+3]) {
                if (!strncmp(mountpoint+i, "\\040", 4)) { decoded[j++] = ' '; i += 3; continue; }
                if (!strncmp(mountpoint+i, "\\011", 4)) { decoded[j++] = '\t'; i += 3; continue; }
                if (!strncmp(mountpoint+i, "\\134", 4)) { decoded[j++] = '\\'; i += 3; continue; }
            }
            decoded[j++] = mountpoint[i];
        }
        decoded[j] = '\0';
        char sysbuf[PATH_MAX], devbuf[128];
        DIR *sysdir = opendir("/sys/class/block");
        if (!sysdir) continue;
        struct dirent *ent;
        while ((ent = readdir(sysdir)) != NULL) {
            if (!is_child_partition_name(disk_name, ent->d_name))
                continue;
            snprintf(sysbuf, sizeof sysbuf, "/sys/class/block/%s/partition", ent->d_name);
            if (access(sysbuf, F_OK) != 0) continue;
            snprintf(sysbuf, sizeof sysbuf, "/sys/class/block/%s/dev", ent->d_name);
            if (!read_small_file(sysbuf, devbuf, sizeof devbuf)) continue;
            unsigned int pmaj = 0, pmin = 0;
            if (sscanf(devbuf, "%u:%u", &pmaj, &pmin) != 2 || pmaj != maj || pmin != min)
                continue;
            if (!strcmp(decoded, "/") || !strcmp(decoded, "/boot") ||
                !strncmp(decoded, "/boot/", 6) || !strcmp(decoded, "/usr") ||
                !strncmp(decoded, "/usr/", 5) || !strcmp(decoded, "/etc") ||
                !strncmp(decoded, "/etc/", 5) || !strcmp(decoded, "/cdrom") ||
                !strncmp(decoded, "/cdrom/", 7) || !strcmp(decoded, "/run/initramfs/live") ||
                !strncmp(decoded, "/run/initramfs/live/", 20) || !strcmp(decoded, "/run/live/medium") ||
                !strncmp(decoded, "/run/live/medium/", 17) || !strcmp(decoded, "/lib/live/mount/medium") ||
                !strncmp(decoded, "/lib/live/mount/medium/", 23)) {
                closedir(sysdir);
                free(line);
                fclose(mounts);
                fail("Refusing to overwrite a device containing a critical system mount.");
                return false;
            }
        }
        closedir(sysdir);
    }
    free(line);
    fclose(mounts);

    /* Unmount any mounted partitions belonging to the selected USB disk. */
    mounts = fopen("/proc/self/mountinfo", "r");
    if (!mounts) return false;
    line = NULL;
    cap = 0;
    while (getline(&line, &cap, mounts) != -1) {
        unsigned int maj = 0, min = 0;
        char mountpoint[PATH_MAX] = {0};
        if (sscanf(line, "%*s %*s %u:%u %*s %4095s", &maj, &min, mountpoint) != 3)
            continue;
        bool belongs = false;
        DIR *sysdir = opendir("/sys/class/block");
        if (!sysdir) continue;
        struct dirent *ent;
        while ((ent = readdir(sysdir)) != NULL) {
            if (!is_child_partition_name(disk_name, ent->d_name)) continue;
            char partfile[PATH_MAX], devfile[PATH_MAX], devbuf[128];
            snprintf(partfile, sizeof partfile, "/sys/class/block/%s/partition", ent->d_name);
            if (access(partfile, F_OK) != 0) continue;
            snprintf(devfile, sizeof devfile, "/sys/class/block/%s/dev", ent->d_name);
            if (!read_small_file(devfile, devbuf, sizeof devbuf)) continue;
            unsigned int pmaj = 0, pmin = 0;
            if (sscanf(devbuf, "%u:%u", &pmaj, &pmin) == 2 && pmaj == maj && pmin == min) {
                belongs = true;
                break;
            }
        }
        closedir(sysdir);
        if (!belongs) continue;
        char decoded[PATH_MAX];
        size_t j = 0;
        for (size_t i = 0; mountpoint[i] && j + 1 < sizeof decoded; i++) {
            if (mountpoint[i] == '\\' && mountpoint[i+1] && mountpoint[i+2] && mountpoint[i+3]) {
                if (!strncmp(mountpoint+i, "\\040", 4)) { decoded[j++] = ' '; i += 3; continue; }
                if (!strncmp(mountpoint+i, "\\011", 4)) { decoded[j++] = '\t'; i += 3; continue; }
                if (!strncmp(mountpoint+i, "\\134", 4)) { decoded[j++] = '\\'; i += 3; continue; }
            }
            decoded[j++] = mountpoint[i];
        }
        decoded[j] = '\0';
        if (umount2(decoded, 0) != 0) {
            char msg[PATH_MAX + 160];
            snprintf(msg, sizeof msg, "Could not unmount %s: %s. Close files on the USB drive and try again.", decoded, strerror(errno));
            fail(msg);
            free(line);
            fclose(mounts);
            return false;
        }
    }
    free(line);
    fclose(mounts);
    return true;
}

static int write_all(int fd, const unsigned char *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, buf + off, n - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (w == 0) return -1;
        off += (size_t)w;
    }
    return 0;
}

static int read_all_at(int fd, unsigned char *buf, size_t n, off_t offset) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = pread(fd, buf + off, n - off, offset + (off_t)off);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return -1;
        off += (size_t)r;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fail("Usage: nebula-flasher-writer IMAGE DEVICE EXPECTED_MAJOR:MINOR");
        return 2;
    }
    char image_real[PATH_MAX], device_real[PATH_MAX];
    if (!realpath(argv[1], image_real) || !realpath(argv[2], device_real)) {
        fail("Image or target device does not exist.");
        return 2;
    }
    unsigned int expected_major = 0, expected_minor = 0;
    char trailing = '\0';
    if (sscanf(argv[3], "%u:%u%c", &expected_major, &expected_minor, &trailing) != 2) {
        fail("Invalid expected device identifier.");
        return 2;
    }
    if (strncmp(device_real, "/dev/", 5) != 0) {
        fail("Target must be a device under /dev.");
        return 2;
    }
    if (is_partition(device_real)) {
        fail("Select the whole USB drive, not an individual partition.");
        return 2;
    }
    if (!is_usb_or_removable(device_real)) {
        fail("Target is not detected as a removable or USB device.");
        return 2;
    }
    if (is_backing_device_for_root(device_real)) {
        fail("Refusing to overwrite the device that contains the running operating system.");
        return 2;
    }

    int in = open(image_real, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (in < 0) {
        fail("Cannot open image file.");
        return 2;
    }
    struct stat image_stat;
    if (fstat(in, &image_stat) != 0 || !S_ISREG(image_stat.st_mode) || image_stat.st_size <= 0) {
        close(in);
        fail("Image must be a non-empty regular file.");
        return 2;
    }
    if (filesystem_path_uses_device(image_real, device_real)) {
        close(in);
        fail("The image file is stored on the selected drive. Copy it to another disk first.");
        return 2;
    }

    int probe = open(device_real, O_RDONLY | O_CLOEXEC);
    if (probe < 0) {
        close(in);
        fail("Cannot inspect the target drive.");
        return 2;
    }
    struct stat dev_stat;
    unsigned long long device_size = 0;
    if (fstat(probe, &dev_stat) != 0 || !S_ISBLK(dev_stat.st_mode) || ioctl(probe, BLKGETSIZE64, &device_size) != 0) {
        close(in); close(probe);
        fail("Target is not a readable block device.");
        return 2;
    }
    if (major(dev_stat.st_rdev) != expected_major || minor(dev_stat.st_rdev) != expected_minor) {
        close(in); close(probe);
        fail("The drive changed after selection. Refresh the device list and try again.");
        return 2;
    }
    close(probe);
    if ((unsigned long long)image_stat.st_size > device_size) {
        close(in);
        fail("Image is larger than the selected drive.");
        return 2;
    }

    if (!unmount_target_children(device_real)) {
        close(in);
        return 2;
    }
    int out = open(device_real, O_RDWR | O_CLOEXEC | O_EXCL);
    if (out < 0) {
        close(in);
        fail("Cannot open target drive exclusively after unmounting its partitions.");
        return 2;
    }

    unsigned char *buffer = malloc(CHUNK_SIZE);
    if (!buffer) {
        close(in); close(out);
        fail("Not enough memory for the write buffer.");
        return 2;
    }
    const unsigned long long total = (unsigned long long)image_stat.st_size;
    unsigned long long done = 0;
    unsigned int last_percent = 101;
    while (done < total) {
        size_t amount = (size_t)((total - done) > CHUNK_SIZE ? CHUNK_SIZE : (total - done));
        if (read_all_at(in, buffer, amount, (off_t)done) != 0 || write_all(out, buffer, amount) != 0) {
            free(buffer); close(in); close(out);
            fail("Writing failed. The drive may have been disconnected or may be faulty.");
            return 2;
        }
        done += amount;
        unsigned int percent = (unsigned int)((done * 100ULL) / total);
        if (percent != last_percent) {
            printf("PROGRESS %u\n", percent);
            fflush(stdout);
            last_percent = percent;
        }
    }
    if (fsync(out) != 0) {
        free(buffer); close(in); close(out);
        fail("Could not flush data to the drive.");
        return 2;
    }
    printf("VERIFY 0\n"); fflush(stdout);

    /* Read back the written image and compare byte-for-byte. */
    unsigned long long verified = 0;
    while (verified < total) {
        size_t amount = (size_t)((total - verified) > CHUNK_SIZE ? CHUNK_SIZE : (total - verified));
        if (read_all_at(in, buffer, amount, (off_t)verified) != 0) {
            free(buffer); close(in); close(out);
            fail("Could not reread the source image for verification.");
            return 2;
        }
        unsigned char *written = malloc(amount);
        if (!written || read_all_at(out, written, amount, (off_t)verified) != 0 || memcmp(buffer, written, amount) != 0) {
            free(written); free(buffer); close(in); close(out);
            fail("Verification failed: data read from the drive does not match the image.");
            return 2;
        }
        free(written);
        verified += amount;
        printf("VERIFY %u\n", (unsigned int)((verified * 100ULL) / total));
        fflush(stdout);
    }

    free(buffer);
    close(in);
    close(out);
    printf("DONE\n");
    fflush(stdout);
    return 0;
}
