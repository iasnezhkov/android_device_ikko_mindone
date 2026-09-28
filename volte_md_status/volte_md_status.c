#include <android/log.h>
#include <fcntl.h>
#include <linux/ioctl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/system_properties.h>
#include <sys/types.h>
#include <unistd.h>

#define CCCI_MDX_STA_DEV "/dev/ccci_mdx_sta"
#define VOLTE_MD_STATUS_PROP "vendor.volte_md_status"
#define VOLTE_MD_STATUS_TAG "VOLTE MD STATUS"

#define CCCI_UTIL_BC_MAGIC 'B'
#define CCCI_IOC_HOLD_RST_LOCK _IO(CCCI_UTIL_BC_MAGIC, 0)
#define CCCI_IOC_FREE_RST_LOCK _IO(CCCI_UTIL_BC_MAGIC, 1)

struct ccci_timespec64 {
	int64_t tv_sec;
	int64_t tv_nsec;
};

struct md_status_event {
	struct ccci_timespec64 time_stamp;
	int32_t md_id;
	int32_t event_type;
	char reason[32];
};

_Static_assert(sizeof(struct md_status_event) == 56,
	       "md_status_event must be 56 bytes, the kernel's native (non-compat) size");

#define MD_STA_EV_COUNT 16

static const char *const kMdStaEventName[MD_STA_EV_COUNT] = {
	[0] = "invalid",
	[1] = "reset",
	[2] = "assert_req",
	[3] = "stop_req",
	[4] = "start_req",
	[5] = "flightmode",
	[6] = "flightmode",
	[7] = "flightmode",
	[8] = "flightmode",
	[9] = "hs1",
	[10] = "ready",
	[11] = "exception",
	[12] = "stop",
	[13] = "",
	[14] = "",
	[15] = "",
};

static int ccci_mdx_sta_open(void)
{
	int fd;

	for (;;) {
		fd = open(CCCI_MDX_STA_DEV, O_RDWR | O_NOCTTY);
		if (fd >= 0)
			return fd;
		sleep(1);
	}
}

int main(void)
{
	int fd = ccci_mdx_sta_open();

	ioctl(fd, CCCI_IOC_HOLD_RST_LOCK, -1);

	for (;;) {
		struct md_status_event ev = {0};
		ssize_t n = read(fd, &ev, sizeof(ev));
		uint32_t idx = (uint32_t)ev.event_type;
		const char *status = idx < MD_STA_EV_COUNT ? kMdStaEventName[idx] : "invalid";
		char prop[PROP_VALUE_MAX];

		__system_property_set(VOLTE_MD_STATUS_PROP, status);
		prop[0] = '\0';
		__system_property_get(VOLTE_MD_STATUS_PROP, prop);
		__android_log_print(ANDROID_LOG_DEBUG, VOLTE_MD_STATUS_TAG,
				     "volte_ims_md_status_read_looper %d %s count = %d property = %s",
				     ev.event_type, status, (int)n, prop);
	}

	ioctl(fd, CCCI_IOC_FREE_RST_LOCK, -1);
	return 0;
}
