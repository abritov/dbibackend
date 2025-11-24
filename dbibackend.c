/*
 * DBI Backend - USB backend for Nintendo Switch DBI installer
 * Rewritten from Python to C
 * Requires: libusb-1.0
 * Compile: gcc -o dbibackend dbibackend.c -lusb-1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <libusb-1.0/libusb.h>
#include <errno.h>

#define BUFFER_SEGMENT_DATA_SIZE 0x100000
#define SWITCH_VID 0x057E
#define SWITCH_PID 0x3000
#define USB_TIMEOUT 0
#define MAX_PATH_LEN 4096
#define MAX_TITLES 1024

/* Command IDs */
typedef enum {
    CMD_EXIT = 0,
    CMD_LIST_DEPRECATED = 1,
    CMD_FILE_RANGE = 2,
    CMD_LIST = 3
} CommandID;

/* Command Types */
typedef enum {
    CMD_TYPE_REQUEST = 0,
    CMD_TYPE_RESPONSE = 1,
    CMD_TYPE_ACK = 2
} CommandType;

/* USB Context */
typedef struct {
    libusb_context *ctx;
    libusb_device_handle *dev_handle;
    uint8_t ep_in;
    uint8_t ep_out;
} UsbContext;

/* Title cache entry */
typedef struct {
    char display_name[256];
    char full_path[MAX_PATH_LEN];
} TitleEntry;

/* Title cache */
typedef struct {
    TitleEntry entries[MAX_TITLES];
    int count;
} TitleCache;

/* Global variables */
static bool debug_mode = false;

/* Logging functions */
#define LOG_INFO(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) if (debug_mode) printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARNING(fmt, ...) fprintf(stderr, "[WARNING] " fmt "\n", ##__VA_ARGS__)

/* USB functions */
int usb_read(UsbContext *ctx, uint8_t *data, int size, int timeout) {
    int transferred;
    int ret = libusb_bulk_transfer(ctx->dev_handle, ctx->ep_in, data, size, &transferred, timeout);
    if (ret < 0) {
        LOG_ERROR("USB read error: %s", libusb_error_name(ret));
        return ret;
    }
    return transferred;
}

int usb_write(UsbContext *ctx, uint8_t *data, int size, int timeout) {
    int transferred;
    int ret = libusb_bulk_transfer(ctx->dev_handle, ctx->ep_out, data, size, &transferred, timeout);
    if (ret < 0) {
        LOG_ERROR("USB write error: %s", libusb_error_name(ret));
        return ret;
    }
    return transferred;
}

UsbContext* usb_init(uint16_t vid, uint16_t pid) {
    UsbContext *ctx = malloc(sizeof(UsbContext));
    if (!ctx) {
        LOG_ERROR("Failed to allocate USB context");
        return NULL;
    }

    int ret = libusb_init(&ctx->ctx);
    if (ret < 0) {
        LOG_ERROR("Failed to initialize libusb: %s", libusb_error_name(ret));
        free(ctx);
        return NULL;
    }

    ctx->dev_handle = libusb_open_device_with_vid_pid(ctx->ctx, vid, pid);
    if (!ctx->dev_handle) {
        LOG_ERROR("Device %04x:%04x not found", vid, pid);
        libusb_exit(ctx->ctx);
        free(ctx);
        return NULL;
    }

    libusb_reset_device(ctx->dev_handle);
    
    if (libusb_kernel_driver_active(ctx->dev_handle, 0) == 1) {
        libusb_detach_kernel_driver(ctx->dev_handle, 0);
    }

    ret = libusb_claim_interface(ctx->dev_handle, 0);
    if (ret < 0) {
        LOG_ERROR("Failed to claim interface: %s", libusb_error_name(ret));
        libusb_close(ctx->dev_handle);
        libusb_exit(ctx->ctx);
        free(ctx);
        return NULL;
    }

    struct libusb_config_descriptor *config;
    libusb_get_active_config_descriptor(libusb_get_device(ctx->dev_handle), &config);
    
    const struct libusb_interface *iface = &config->interface[0];
    const struct libusb_interface_descriptor *iface_desc = &iface->altsetting[0];
    
    ctx->ep_in = 0;
    ctx->ep_out = 0;
    
    for (int i = 0; i < iface_desc->bNumEndpoints; i++) {
        uint8_t ep_addr = iface_desc->endpoint[i].bEndpointAddress;
        if (ep_addr & LIBUSB_ENDPOINT_IN) {
            ctx->ep_in = ep_addr;
        } else {
            ctx->ep_out = ep_addr;
        }
    }
    
    libusb_free_config_descriptor(config);

    if (ctx->ep_in == 0 || ctx->ep_out == 0) {
        LOG_ERROR("Failed to find endpoints");
        libusb_release_interface(ctx->dev_handle, 0);
        libusb_close(ctx->dev_handle);
        libusb_exit(ctx->ctx);
        free(ctx);
        return NULL;
    }

    return ctx;
}

void usb_cleanup(UsbContext *ctx) {
    if (ctx) {
        if (ctx->dev_handle) {
            libusb_release_interface(ctx->dev_handle, 0);
            libusb_close(ctx->dev_handle);
        }
        if (ctx->ctx) {
            libusb_exit(ctx->ctx);
        }
        free(ctx);
    }
}

/* Helper function to check if file has valid extension */
bool has_valid_extension(const char *filename) {
    size_t len = strlen(filename);
    if (len < 4) return false;
    
    const char *ext = filename + len - 4;
    return (strcasecmp(ext, ".nsp") == 0 || 
            strcasecmp(ext, ".xci") == 0 ||
            strcasecmp(ext, ".nsz") == 0);
}

/* Recursively scan directory for titles */
void scan_directory(const char *path, TitleCache *cache) {
    DIR *dir = opendir(path);
    if (!dir) {
        LOG_ERROR("Failed to open directory: %s", path);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && cache->count < MAX_TITLES) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                LOG_DEBUG("Found directory: %s", full_path);
                scan_directory(full_path, cache);
            } else if (S_ISREG(st.st_mode) && has_valid_extension(entry->d_name)) {
                LOG_DEBUG("\t%s", entry->d_name);
                strncpy(cache->entries[cache->count].display_name, entry->d_name, 255);
                strncpy(cache->entries[cache->count].full_path, full_path, MAX_PATH_LEN - 1);
                cache->count++;
            }
        }
    }

    closedir(dir);
}

/* Find title in cache by display name */
const char* find_title_path(TitleCache *cache, const char *display_name) {
    for (int i = 0; i < cache->count; i++) {
        if (strcmp(cache->entries[i].display_name, display_name) == 0) {
            return cache->entries[i].full_path;
        }
    }
    return display_name;
}

/* Process EXIT command */
void process_exit_command(UsbContext *ctx) {
    LOG_INFO("Exit");
    uint8_t response[16];
    memcpy(response, "DBI0", 4);
    *(uint32_t*)(response + 4) = CMD_TYPE_RESPONSE;
    *(uint32_t*)(response + 8) = CMD_EXIT;
    *(uint32_t*)(response + 12) = 0;
    usb_write(ctx, response, 16, USB_TIMEOUT);
}

/* Process LIST command */
void process_list_command(UsbContext *ctx, const char *work_dir, TitleCache *cache) {
    LOG_INFO("Get list");

    cache->count = 0;
    scan_directory(work_dir, cache);

    char *nsp_list = malloc(MAX_TITLES * 256);
    if (!nsp_list) {
        LOG_ERROR("Failed to allocate memory for title list");
        return;
    }

    nsp_list[0] = '\0';
    for (int i = 0; i < cache->count; i++) {
        strcat(nsp_list, cache->entries[i].display_name);
        strcat(nsp_list, "\n");
    }

    uint32_t list_len = strlen(nsp_list);

    uint8_t response[16];
    memcpy(response, "DBI0", 4);
    *(uint32_t*)(response + 4) = CMD_TYPE_RESPONSE;
    *(uint32_t*)(response + 8) = CMD_LIST;
    *(uint32_t*)(response + 12) = list_len;
    usb_write(ctx, response, 16, USB_TIMEOUT);

    uint8_t ack[16];
    usb_read(ctx, ack, 16, USB_TIMEOUT);
    uint32_t cmd_type = *(uint32_t*)(ack + 4);
    uint32_t cmd_id = *(uint32_t*)(ack + 8);
    uint32_t data_size = *(uint32_t*)(ack + 12);
    LOG_DEBUG("Cmd Type: %u, Command id: %u, Data size: %u", cmd_type, cmd_id, data_size);
    LOG_DEBUG("Ack");

    usb_write(ctx, (uint8_t*)nsp_list, list_len, USB_TIMEOUT);
    free(nsp_list);
}

/* Process FILE_RANGE command */
void process_file_range_command(UsbContext *ctx, uint32_t data_size, TitleCache *cache) {
    LOG_INFO("File range");

    uint8_t ack_header[16];
    memcpy(ack_header, "DBI0", 4);
    *(uint32_t*)(ack_header + 4) = CMD_TYPE_ACK;
    *(uint32_t*)(ack_header + 8) = CMD_FILE_RANGE;
    *(uint32_t*)(ack_header + 12) = data_size;
    usb_write(ctx, ack_header, 16, USB_TIMEOUT);

    uint8_t *file_range_header = malloc(data_size);
    if (!file_range_header) {
        LOG_ERROR("Failed to allocate memory for file range header");
        return;
    }

    usb_read(ctx, file_range_header, data_size, USB_TIMEOUT);

    uint32_t range_size = *(uint32_t*)(file_range_header);
    uint64_t range_offset = *(uint64_t*)(file_range_header + 4);
    uint32_t nsp_name_len = *(uint32_t*)(file_range_header + 12);
    char nsp_name[MAX_PATH_LEN];
    strncpy(nsp_name, (char*)(file_range_header + 16), MAX_PATH_LEN - 1);
    nsp_name[MAX_PATH_LEN - 1] = '\0';

    free(file_range_header);

    const char *actual_path = find_title_path(cache, nsp_name);
    LOG_INFO("Range Size: %u, Range Offset: %lu, Name len: %u, Name: %s", 
             range_size, range_offset, nsp_name_len, actual_path);

    uint8_t response[16];
    memcpy(response, "DBI0", 4);
    *(uint32_t*)(response + 4) = CMD_TYPE_RESPONSE;
    *(uint32_t*)(response + 8) = CMD_FILE_RANGE;
    *(uint32_t*)(response + 12) = range_size;
    usb_write(ctx, response, 16, USB_TIMEOUT);

    uint8_t ack[16];
    usb_read(ctx, ack, 16, USB_TIMEOUT);
    uint32_t cmd_type = *(uint32_t*)(ack + 4);
    uint32_t cmd_id = *(uint32_t*)(ack + 8);
    uint32_t ack_data_size = *(uint32_t*)(ack + 12);
    LOG_DEBUG("Cmd Type: %u, Command id: %u, Data size: %u", cmd_type, cmd_id, ack_data_size);
    LOG_DEBUG("Ack");

    FILE *f = fopen(actual_path, "rb");
    if (!f) {
        LOG_ERROR("Failed to open file: %s", actual_path);
        return;
    }

    fseek(f, range_offset, SEEK_SET);

    uint8_t *buffer = malloc(BUFFER_SEGMENT_DATA_SIZE);
    if (!buffer) {
        LOG_ERROR("Failed to allocate transfer buffer");
        fclose(f);
        return;
    }

    uint64_t curr_off = 0;
    uint64_t end_off = range_size;
    uint32_t read_size = BUFFER_SEGMENT_DATA_SIZE;

    while (curr_off < end_off) {
        if (curr_off + read_size >= end_off) {
            read_size = end_off - curr_off;
        }

        size_t bytes_read = fread(buffer, 1, read_size, f);
        if (bytes_read != read_size) {
            LOG_ERROR("Failed to read from file");
            break;
        }

        usb_write(ctx, buffer, read_size, USB_TIMEOUT);
        curr_off += read_size;
    }

    free(buffer);
    fclose(f);
}

/* Main command polling loop */
void poll_commands(UsbContext *ctx, const char *work_dir) {
    LOG_INFO("Entering command loop");

    TitleCache cache = {0};

    while (true) {
        uint8_t cmd_header[16];
        int ret = usb_read(ctx, cmd_header, 16, USB_TIMEOUT);
        if (ret < 16) {
            continue;
        }

        if (memcmp(cmd_header, "DBI0", 4) != 0) {
            continue;
        }

        uint32_t cmd_type = *(uint32_t*)(cmd_header + 4);
        uint32_t cmd_id = *(uint32_t*)(cmd_header + 8);
        uint32_t data_size = *(uint32_t*)(cmd_header + 12);

        LOG_DEBUG("Cmd Type: %u, Command id: %u, Data size: %u", cmd_type, cmd_id, data_size);

        switch (cmd_id) {
            case CMD_EXIT:
                process_exit_command(ctx);
                return;
            case CMD_LIST:
                process_list_command(ctx, work_dir, &cache);
                break;
            case CMD_FILE_RANGE:
                process_file_range_command(ctx, data_size, &cache);
                break;
            default:
                LOG_WARNING("Unknown command id: %u", cmd_id);
                process_exit_command(ctx);
                return;
        }
    }
}

/* Connect to Nintendo Switch */
UsbContext* connect_to_switch(void) {
    UsbContext *ctx;
    while (true) {
        ctx = usb_init(SWITCH_VID, SWITCH_PID);
        if (ctx) {
            return ctx;
        }
        LOG_INFO("Waiting for switch");
        sleep(1);
    }
}

/* Print usage */
void print_usage(const char *prog_name) {
    printf("Usage: %s [OPTIONS] <titles_directory>\n", prog_name);
    printf("\nInstall local titles into Nintendo Switch via USB\n");
    printf("\nOptions:\n");
    printf("  --debug    Enable debug output\n");
    printf("  --help     Show this help message\n");
}

/* Main function */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *titles_dir = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug_mode = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            titles_dir = argv[i];
        }
    }

    if (!titles_dir) {
        LOG_ERROR("No titles directory specified");
        print_usage(argv[0]);
        return 1;
    }

    struct stat st;
    if (stat(titles_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        LOG_ERROR("Specified path must be a directory: %s", titles_dir);
        return 1;
    }

    UsbContext *ctx = connect_to_switch();
    if (!ctx) {
        LOG_ERROR("Failed to connect to Switch");
        return 1;
    }

    poll_commands(ctx, titles_dir);

    usb_cleanup(ctx);
    return 0;
}
