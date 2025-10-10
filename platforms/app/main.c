int get_fd(const char* file_name, int open_style);
int fd_advisexrzETKrvvN(int fd);
int fd_advise(int fd, off_t offset, off_t len, int advice);
int snapshot(int myfd);

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

// Function declarations
int get_fd(const char* file_name, int open_style);
int fd_advisexrzETKrvvN(int fd);
int fd_advise(int fd, off_t offset, off_t len, int advice);
int snapshot(int myfd);

int main() {
    const char* file_name = "Data/hello.txt";
    int open_style = O_RDONLY;
    int fd = get_fd(file_name, open_style);
    if (fd < 0) {
        return 1;
    }

    fd_advisexrzETKrvvN(fd);
    snapshot(fd);
    return 0;
}

int get_fd(const char* file_name, int open_style) {
    int fd = open(file_name, open_style);
    if (fd == -1) {
        perror("Failed to open the file");
        return -1;
    }
    return fd;
}

int fd_advisexrzETKrvvN(int fd) {
    off_t offset = 0;
    off_t len = 1024; // Prevent zero-length buffer
    int advice = POSIX_FADV_NORMAL;
    return fd_advise(fd, offset, len, advice);
}

int fd_advise(int fd, off_t offset, off_t len, int advice) {
    printf("Enter fd_advise\n");

    int result = posix_fadvise(fd, offset, len, advice);
    if (result != 0) {
        errno = result;
        perror("posix_fadvise failed");
        close(fd);
        return -1;
    }

    char buffer[len+1];
    ssize_t bytes_read = read(fd, buffer, len);
    if (bytes_read == -1) {
        perror("Failed to read the file");
        close(fd);
        return -1;
    }

    buffer[bytes_read] = '\0'; // Null-terminate
    printf("Read %zd bytes: %s\n", bytes_read, buffer);
    printf("Leave fd_advise\n");
    return fd;
}

int snapshot(int myfd) {
    printf("Enter snapshot\n");

    struct stat file_info;
    if (fstat(myfd, &file_info) == -1) {
        perror("Error getting file attributes");
        close(myfd);
        return -1;
    }

    printf("File Size: %lld bytes\n", (long long)file_info.st_size);
    printf("File Permissions: %o\n", file_info.st_mode & ~S_IFMT);
    printf("File Owner UID: %d\n", file_info.st_uid);
    printf("File Group GID: %d\n", file_info.st_gid);

    off_t cur_offset = lseek(myfd, 0, SEEK_CUR);
    if (cur_offset == -1) {
        perror("Error getting current offset");
    }
    printf("Current offset: %lld\n", (long long)cur_offset);

    if (close(myfd) == -1) {
        perror("Error closing file");
        return -1;
    }

    printf("Leave snapshot\n");
    return 0;
}
