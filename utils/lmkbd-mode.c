
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <getopt.h>
#include <libudev.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>

static const char *VENDOR = "23fd", *PRODUCT = "2069";
static bool find_lmkbd(char *device)
{
  struct udev *udev;
  struct udev_enumerate *enumerate;
  struct udev_list_entry *devices, *dev_list_entry;

  udev = udev_new();
  if (udev == NULL) {
    fprintf(stderr, "Cannot create udev.\n");
    return false;
  }

  enumerate = udev_enumerate_new(udev);
  udev_enumerate_add_match_subsystem(enumerate, "hidraw");
  udev_enumerate_scan_devices(enumerate);
  devices = udev_enumerate_get_list_entry(enumerate);

  udev_list_entry_foreach(dev_list_entry, devices) {
    const char *syspath, *devpath;
    struct udev_device *hiddev, *usbdev;

    syspath = udev_list_entry_get_name(dev_list_entry);
    hiddev = udev_device_new_from_syspath(udev, syspath);
    devpath = udev_device_get_devnode(hiddev);

    usbdev = udev_device_get_parent_with_subsystem_devtype(hiddev, "usb", "usb_device");
    if (usbdev == NULL) {
      fprintf(stderr, "Cannot find parent USB device.\n");
      return false;
    }

    if (!strcmp(VENDOR, udev_device_get_sysattr_value(usbdev, "idVendor")) &&
        !strcmp(PRODUCT, udev_device_get_sysattr_value(usbdev, "idProduct"))) {
      if (device[0] != '\0') {
        fprintf(stderr, "Found more than one keyboard. Need to specify one.\n");
        return false;
      }
      strncpy(device, devpath, PATH_MAX-1);
    }

    udev_device_unref(hiddev);
  }

  udev_enumerate_unref(enumerate);
  udev_unref(udev);

  if (device[0] == '\0') {
    fprintf(stderr, "Keyboard not found.\n");
    return false;
  }
  return true;
}

static char device[PATH_MAX] = { 0 };
static int swap = 0;
static int set_mode = 0;

static struct option long_options[] = {
  {"device", required_argument, 0, 'd'},
  {"swap", no_argument, &swap, 1},
  {"set", required_argument, 0, 's'},
  {NULL, 0, 0, 0}
};

static const char *models[] = {
  "tk", "space_cadet", "smbx"
};

static const char *modes[] = {
  "illegal", "HUT", "Emacs"
};

#define countof(x) (sizeof(x)/sizeof(x[0]))

int main(int argc, char **argv)
{
  while (true) {
    int option_index = 0;
    int c = getopt_long(argc, argv, "d:s:x",
                        long_options, &option_index);

    if (c < 0) break;

    if (c == 0) {
      if (long_options[option_index].flag != 0) continue;
      c = long_options[option_index].val;
    }

    switch (c) {
    case 'd':
      if (optarg[0] == '/') {
        strncpy(device, optarg, sizeof(device)-1);
      }
      else {
        snprintf(device, sizeof(device)-1, "/dev/hidraw%s", optarg);
      }
      break;

    case 's':
      set_mode = strtoul(optarg, NULL, 10);
      break;

    case 'x':
      swap = 1;
      break;

    case '?':
    default:
      printf("Usage: %s [--device num] [--swap] [--set mode]\n", argv[0]);
      return 1;
    }
  }

  if (device[0] == '\0') {
    if (!find_lmkbd(device)) return 1;
  }

  int fd, rc;
  unsigned char buf[4];
  fd = open(device, O_RDWR|O_NONBLOCK);
  if (fd < 0) {
    perror("Unable to open device");
    return 1;
  }
  
  buf[0] = 0;
  rc = ioctl(fd, HIDIOCGFEATURE(4), buf);
  if (rc < 0) {
    perror("Error getting feature report");
    return 1;
  }
  if (rc != 4) {
    fprintf(stderr, "Incorrect feature report: %d", rc);
    return 1;
  }

  printf("Model = %d (%s)\n", buf[1], (buf[1] < countof(models)) ? models[buf[1]] : "unknown");

  do {
    if (set_mode) {
      buf[2] = set_mode;
    }
    else if (swap) {
      unsigned char tmp;
      tmp = buf[2];
      buf[2] = buf[3];
      buf[3] = tmp;
    }
    else {
      break;
    }

    rc = ioctl(fd, HIDIOCSFEATURE(4), buf);
    if (rc < 0) {
      perror("Error setting feature report");
      return 1;
    }
  } while(false);
  
  printf("Normal mode = %d (%s)\n", buf[2], (buf[2] < countof(modes)) ? modes[buf[2]] : "unknown");
  printf("Mode lock mode = %d (%s)\n", buf[3], (buf[3] < countof(modes)) ? modes[buf[3]] : "unknown");

  return 0;
}
