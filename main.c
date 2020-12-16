#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <xcb/xcb.h>
#include <libudev.h>
#include <errno.h>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <limits.h>

/* stolen from bsd's time.h */
/* Operations on timespecs */
#define	timespecclear(tvp)	((tvp)->tv_sec = (tvp)->tv_nsec = 0)
#define	timespecisset(tvp)	((tvp)->tv_sec || (tvp)->tv_nsec)
#define	timespeccmp(tvp, uvp, cmp)					\
	(((tvp)->tv_sec == (uvp)->tv_sec) ?				\
	    ((tvp)->tv_nsec cmp (uvp)->tv_nsec) :			\
	    ((tvp)->tv_sec cmp (uvp)->tv_sec))

#define	timespecadd(tsp, usp, vsp)					\
	do {								\
		(vsp)->tv_sec = (tsp)->tv_sec + (usp)->tv_sec;		\
		(vsp)->tv_nsec = (tsp)->tv_nsec + (usp)->tv_nsec;	\
		if ((vsp)->tv_nsec >= 1000000000L) {			\
			(vsp)->tv_sec++;				\
			(vsp)->tv_nsec -= 1000000000L;			\
		}							\
	} while (0)
#define	timespecsub(tsp, usp, vsp)					\
	do {								\
		(vsp)->tv_sec = (tsp)->tv_sec - (usp)->tv_sec;		\
		(vsp)->tv_nsec = (tsp)->tv_nsec - (usp)->tv_nsec;	\
		if ((vsp)->tv_nsec < 0) {				\
			(vsp)->tv_sec--;				\
			(vsp)->tv_nsec += 1000000000L;			\
		}							\
	} while (0)

/* Operations on timevals. */

#define	timevalclear(tvp)		((tvp)->tv_sec = (tvp)->tv_usec = 0)
#define	timevalisset(tvp)		((tvp)->tv_sec || (tvp)->tv_usec)
#define	timevalcmp(tvp, uvp, cmp)					\
	(((tvp)->tv_sec == (uvp)->tv_sec) ?				\
	    ((tvp)->tv_usec cmp (uvp)->tv_usec) :			\
	    ((tvp)->tv_sec cmp (uvp)->tv_sec))


xcb_connection_t *c = NULL;
xcb_screen_t *screen = NULL;
const char* role_atom_name = "WM_WINDOW_ROLE";
xcb_atom_t role_atom;
int rumble_effect_id = -1;
int dev_fd = -1;
int led_fd[4] = { -1, -1, -1, -1 };
int cur_set = 1;

struct key_binding_t
{
    int32_t press_threshold;
    int32_t release_threshold;
    struct timespec first_press;
    struct timespec last_release;
    struct timespec first_activation;
    struct timespec last_activation;
    int32_t hold_threshold_ms;
    int32_t repeat_ms;
    int32_t first_repeat_delay_ms;
    const char* window_class;
    xcb_keycode_t keycode;
    uint16_t keystate;
    bool rumble;
    bool setnext;
    bool setprev;
    int set;
};

#include "config.gen.inc.c"

static xcb_keycode_t get_keycode_for_modifier(int mod)
{
    if (mod == XCB_MOD_MASK_CONTROL) return 0x25;
    if (mod == XCB_MOD_MASK_SHIFT) return 0x32;
    if (mod == XCB_MOD_MASK_1) return 0x40;

    return 0;
}

static void send_keycode_to_window(xcb_window_t win, xcb_keycode_t keycode, uint16_t keystate, bool pressed)
{
    xcb_key_press_event_t event = { 0 };
    event.response_type = pressed ? XCB_KEY_PRESS : XCB_KEY_RELEASE;
    event.root = win;
    event.event = win;
    event.same_screen = 1;

    uint16_t sent_state = pressed ? 0 : keystate;

    if (!pressed)
    {
	event.state = sent_state;
	event.detail = keycode;
	xcb_send_event(c, 0, win, XCB_EVENT_MASK_KEY_RELEASE, (char*)&event);
    }

    for (int i = 1; i <= XCB_MOD_MASK_1; i = i << 1)
    {
	int mod_code = 0;
	if (keystate & i)
	{
	    mod_code = get_keycode_for_modifier(i);
	}

	if (mod_code)
	{
	    event.detail = mod_code;
	    event.state = sent_state;
	    xcb_send_event(c, 0, win, pressed ? XCB_EVENT_MASK_KEY_PRESS : XCB_EVENT_MASK_KEY_RELEASE, (char*)&event);

	    if (pressed)
	    {
		sent_state |= i;
	    }
	    else
	    {
		sent_state &= ~i;
	    }
	}
    }

    if (pressed)
    {
	event.state = sent_state;
	event.detail = keycode;
	xcb_send_event(c, 0, win, XCB_EVENT_MASK_KEY_PRESS, (char*)&event);
    }
}

static void send_event_to_window_deep(xcb_window_t win, const char* class, xcb_keycode_t keycode, uint16_t keystate)
{
    const int class_len = strlen(class);
    const char* role = "MainWindow";
    const int role_len = strlen(role);

    xcb_query_tree_cookie_t query_cookie;
    xcb_query_tree_reply_t* query_reply = NULL;
    xcb_get_property_cookie_t class_cookie;
    xcb_get_property_reply_t *class_reply = NULL;
    xcb_get_property_cookie_t role_cookie;
    xcb_get_property_reply_t *role_reply = NULL;

    bool has_role = false;
    bool has_class = false;
    int i;

    class_cookie = xcb_get_property(c, 0, win, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 0, 8);
    role_cookie = xcb_get_property(c, 0, win, role_atom, XCB_ATOM_STRING, 0, 8);
    query_cookie = xcb_query_tree(c, win);

    if ((class_reply = xcb_get_property_reply(c, class_cookie, NULL)))
    {
	has_class = (xcb_get_property_value_length(class_reply) >= class_len
		     && memcmp(class, xcb_get_property_value(class_reply), class_len) == 0);
	free(class_reply);
    }

    if ((role_reply = xcb_get_property_reply(c, role_cookie, NULL)))
    {
	has_role = (xcb_get_property_value_length(role_reply) >= role_len
		    && memcmp(role, xcb_get_property_value(role_reply), role_len) == 0);
	free(role_reply);
    }

    if (has_class && has_role)
    {
	send_keycode_to_window(win, keycode, keystate, true);
	send_keycode_to_window(win, keycode, keystate, false);
    }

    if ((query_reply = xcb_query_tree_reply(c, query_cookie, NULL)))
    {
	xcb_window_t* children = xcb_query_tree_children(query_reply);
	for (i = 0; i < xcb_query_tree_children_length(query_reply); i++)
	{
	    send_event_to_window_deep(children[i], class, keycode, keystate);
	}
	free(query_reply);
    }
}

static void process_binding_event(struct input_event* event, int32_t index)
{
    struct timespec curr_time;
    clock_gettime(CLOCK_MONOTONIC, &curr_time);
    struct key_binding_t *binding = &bindings[index];

    if (event->value > 0 && binding->press_threshold > 0)
    {
	if (event->value >= binding->press_threshold)
	{
	    if (timespeccmp(&binding->first_press, &binding->last_release, <=))
	    {
		binding->first_press = curr_time;
	    }
	    return;
	}
    }
    
    if (event->value < 0 && binding->press_threshold < 0)
    {
	if (event->value <= binding->press_threshold)
	{
	    if (timespeccmp(&binding->first_press, &binding->last_release, <=))
	    {
		binding->first_press = curr_time;
	    }
	    return;
	}
    }
    
    binding->last_release = curr_time;
}

static bool is_binding_pending(struct key_binding_t* binding, bool* first)
{
    struct timespec curr_time;
    struct timespec first_press_diff;
    struct timespec last_activation_diff;
    struct timespec first_activation_diff;
    int32_t diff_ms;

    *first = false;

    if (timespeccmp(&binding->first_press, &binding->last_release, >))
    {
	clock_gettime(CLOCK_MONOTONIC, &curr_time);

	if (timespeccmp(&binding->first_press, &binding->last_activation, >))
	{
	    if (binding->hold_threshold_ms > 0)
	    {
		timespecsub(&curr_time, &binding->first_press, &first_press_diff);
		diff_ms = (first_press_diff.tv_sec * 1000) + (first_press_diff.tv_nsec / 1000000);

		if (diff_ms < binding->hold_threshold_ms)
		{
		    return false;
		}
	    }

	    *first = true;
	    return true;
	}

	timespecsub(&curr_time, &binding->first_activation, &first_activation_diff);

	diff_ms = (first_activation_diff.tv_sec * 1000) + (first_activation_diff.tv_nsec / 1000000);

	if (diff_ms < binding->first_repeat_delay_ms)
	{
	    return false;
	}

	timespecsub(&curr_time, &binding->last_activation, &last_activation_diff);
	diff_ms = (last_activation_diff.tv_sec * 1000) + (last_activation_diff.tv_nsec / 1000000);

	if (diff_ms >= binding->repeat_ms)
	{
	    return true;
	}
    }

    return false;
}

static void play_rumble()
{
    if (dev_fd == -1)
    {
	return;
    }
    
    struct input_event rumble_event = { 0 };
    rumble_event.type = EV_FF;
    rumble_event.code = rumble_effect_id;
    rumble_event.value = 1;
    write(dev_fd, &rumble_event, sizeof rumble_event);
    rumble_event.value = 0;
    write(dev_fd, &rumble_event, sizeof rumble_event);
}

static void set_led_state()
{
    for (int i = 0; i < 4; i++)
    {
	if (led_fd[i] != -1)
	{
	    write(led_fd[i], cur_set == i + 1 ? "1" : "0", 1);
	}
    }
}

static void fire_binding(struct key_binding_t* binding, bool first)
{
    struct timespec curr_time;
    clock_gettime(CLOCK_MONOTONIC, &curr_time);

    binding->last_activation = curr_time;

    if (first)
    {
	binding->first_activation = curr_time;
    }

    if (binding->rumble)
    {
	play_rumble();
    }

    if (binding->setnext)
    {
	if (cur_set < 4)
	{
	    cur_set++;
	    set_led_state();
	}
    }
    else if (binding->setprev)
    {
	if (cur_set > 1)
	{
	    cur_set--;
	    set_led_state();
	}
    }
    else
    {
	if (binding->set == cur_set || binding->set == 0)
	{
	    send_event_to_window_deep(screen->root, binding->window_class, binding->keycode, binding->keystate);
	}
    }
}

static void fire_pending_bindings()
{
    int num_bindings = sizeof bindings / sizeof bindings[0];
    int i;

    for (i = 0; i < num_bindings; i++)
    {
	bool first = false;
	if (is_binding_pending(&bindings[i], &first))
	{
	    fire_binding(&bindings[i], first);
	}
    }
}

int main(int argc, char **argv)
{
    int screen_num = 0;
    const xcb_setup_t *setup;
    xcb_screen_iterator_t iter;
    xcb_intern_atom_cookie_t atom_cookie;
    xcb_intern_atom_reply_t* atom_reply;

    struct udev* udev = NULL;
    struct udev_device* device = NULL;
    const char* device_name;
    const char* device_path;
    struct input_event event;
    int imu_fd = -1;
    int epoll_fd = -1;

    int i = 0;
    ssize_t read_result = 0;
    int epoll_result = 0;

    if (argc != 2)
    {
	fprintf(stderr, "Invalid number of arguments. Need exactly one.\n");
	exit(1);
    }

    printf("%s\n", argv[1]);

    udev = udev_new();

    if (!udev)
    {
	fprintf(stderr, "Unable to create udev context.\n");
	exit(1);
    }

    device = udev_device_new_from_syspath(udev, argv[1]);

    if (!device)
    {
	fprintf(stderr, "Unable to open device from syspath: %d.\n", errno);
	exit(1);
    }

    device_name = udev_device_get_property_value(udev_device_get_parent(device), "NAME");
    device_path = udev_device_get_devnode(device);

    if (device_name && device_path)
    {
	printf("Starting up to monitor %s on %s\n", device_name, device_path);
    }
    else
    {
	fprintf(stderr, "Could not get name of device.\n");
	exit(1);
    }

    if ((dev_fd = open(device_path, O_RDWR)) == -1)
    {
	fprintf(stderr, "Could not open device node: %d\n", errno);
	exit(1);
    }

    struct ff_effect effect = { 0 };
    effect.id = -1;
    effect.type = FF_RUMBLE;
    effect.u.rumble.strong_magnitude = USHRT_MAX / 2;
    printf("Trying to add rumble effect...\n");
    if (ioctl(dev_fd, EVIOCSFF, &effect) == -1)
    {
	fprintf(stderr, "Could not add rumble effect! %d %s\n", errno, strerror(errno));
	exit (1);
    }

    rumble_effect_id = effect.id;
    printf("Added rumble effect %d\n", rumble_effect_id);

    {
	struct udev_enumerate* monitor;
	struct udev_list_entry* dev_list;
	printf("We are imu. Trying to find main device so we can use the rumble...\n");
	monitor = udev_enumerate_new(udev);
	if (monitor == NULL)
	{
	    fprintf(stderr, "Could not open udev monitor.\n");
	    exit (1);
	}

	if (udev_enumerate_add_match_parent(monitor, udev_device_get_parent(udev_device_get_parent(device))) < 0)
	{
	    fprintf(stderr, "Could not add udev match for parent's parent.\n");
	    exit (1);
	}

	if (udev_enumerate_add_match_subsystem(monitor, "input") < 0)
	{
	    fprintf(stderr, "Could not add udev match for input subsystem.\n");
	    exit (1);
	}

	if (udev_enumerate_add_match_sysname(monitor, "event*") < 0)
	{
	    fprintf(stderr, "Could not add udev match for event* sysname\n");
	    exit (1);
	}

	if (udev_enumerate_scan_devices(monitor) < 0)
	{
	    fprintf(stderr, "Device scan failed.\n");
	    exit (1);
	}

	dev_list = udev_enumerate_get_list_entry(monitor);

	if (dev_list == NULL)
	{
	    fprintf(stderr, "Device scan returned no devices.\n");
	    exit (1);
	}

	const char* imu_sys_path = NULL;
	while (dev_list)
	{
	    imu_sys_path = udev_list_entry_get_name(dev_list);
	    if (strcmp(imu_sys_path, argv[1]) == 0)
	    {
		printf("Found ourselves!\n");
		imu_sys_path = NULL;
	    }
	    else
	    {
		printf("Found the IMU: %s\n", imu_sys_path);
		break;
	    }
	    dev_list = udev_list_entry_get_next(dev_list);
	}

	if (imu_sys_path == NULL)
	{
	    fprintf(stderr, "Could not find IMU.\n");
	    exit (1);
	}

	struct udev_device* imu_device = udev_device_new_from_syspath(udev, imu_sys_path);

	if (imu_device == NULL)
	{
	    fprintf(stderr, "Unable to open IMU from syspath. %s\n", strerror(errno));
	    exit (1);
	}

	const char* imu_dev_path = udev_device_get_devnode(imu_device);

	imu_fd = open(imu_dev_path, O_RDONLY);

	if (imu_fd == -1)
	{
	    fprintf(stderr, "Could not open the IMU dev. %s\n", strerror(errno));
	    exit (1);
	}

	udev_enumerate_unref(monitor);
	udev_device_unref(imu_device);
    }
    {
	struct udev_enumerate* monitor;
	struct udev_list_entry* dev_list;

	monitor = udev_enumerate_new(udev);

	printf("Trying to open leds...\n");
	monitor = udev_enumerate_new(udev);
	if (monitor == NULL)
	{
	    fprintf(stderr, "Could not open udev monitor.\n");
	    exit (1);
	}

	if (udev_enumerate_add_match_parent(monitor, udev_device_get_parent(udev_device_get_parent(device))) < 0)
	{
	    fprintf(stderr, "Could not add udev match for parent's parent.\n");
	    exit (1);
	}

	if (udev_enumerate_add_match_subsystem(monitor, "leds") < 0)
	{
	    fprintf(stderr, "Could not add udev match for leds subsystem.\n");
	    exit (1);
	}

	if (udev_enumerate_scan_devices(monitor) < 0)
	{
	    fprintf(stderr, "Device scan failed.\n");
	    exit (1);
	}

	dev_list = udev_enumerate_get_list_entry(monitor);

	if (dev_list == NULL)
	{
	    fprintf(stderr, "Device scan returned no devices.\n");
	    exit (1);
	}

	while (dev_list)
	{
	    const char* led_path = udev_list_entry_get_name(dev_list);

	    for (i = 0; i < 4; i++)
	    {
		char name[] = "playerx";
		name[6] = '0' + i + 1;

		if (strstr(led_path, name))
		{
		    char brightness_path[PATH_MAX];
		    snprintf(brightness_path, sizeof brightness_path, "%s/%s", led_path, "brightness");
		    printf("opening: %s\n", brightness_path);

		    led_fd[i] = open(brightness_path, O_WRONLY);
		    if (led_fd[i] == -1)
		    {
			fprintf(stderr, "Could not open led. %d\n", errno);
			exit (1);
		    }

		    //write(led_fd[i], i == 0 ? "1" : "0", 1);
		    write(led_fd[i], "1", 1);
		}
	    }

	    dev_list = udev_list_entry_get_next(dev_list);
	}

	udev_enumerate_unref(monitor);
    }

    set_led_state();

    if ((epoll_fd = epoll_create1(0)) == -1)
    {
	fprintf(stderr, "Could not initialize epoll context: %d\n", errno);
	exit(1);
    }

    struct epoll_event epoll_event = { 0 };
    epoll_event.events = EPOLLIN;
    epoll_event.data.fd = dev_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, dev_fd, &epoll_event) == -1)
    {
	fprintf(stderr, "Could not add device fd to epoll context: %d\n", errno);
	exit(1);
    }

    epoll_event.data.fd = imu_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, imu_fd, &epoll_event) == -1)
    {
	fprintf(stderr, "Could not add imu fd to epoll context: %d\n", errno);
	exit(1);
    }

    c = xcb_connect(NULL, &screen_num);

    if (c == NULL)
    {
	fprintf(stderr, "Unable to open XCB display.\n");
	exit(1);
    }

    setup = xcb_get_setup(c);

    iter = xcb_setup_roots_iterator(setup);

    for (i = 0; i < screen_num; i++)
    {
	xcb_screen_next(&iter);
    }

    screen = iter.data;
    atom_cookie = xcb_intern_atom(c, 0, strlen(role_atom_name), role_atom_name);
    if ((atom_reply = xcb_intern_atom_reply(c, atom_cookie, NULL)))
    {
	role_atom = atom_reply->atom;
	free(atom_reply);
	
    }
    else
    {
	fprintf(stderr, "Could not intern window role atom.\n");
	exit (1);
    }

    for(;;)
    {
	int32_t binding_indices[10];
	int bindings_found;

	fire_pending_bindings();

	epoll_result = epoll_wait(epoll_fd, &epoll_event, 1, 25);

	if (epoll_result == -1)
	{
	    fprintf(stderr, "Error waiting with epoll. %d\n", errno);
	    break;
	}

	if (epoll_result == 0)
	{
	    // epoll timed out
	    continue;
	}

	if (!(epoll_event.events & EPOLLIN))
	{
	    fprintf(stderr, "Device fd is not readable. \n");
	    break;
	}

	read_result = read(epoll_event.data.fd, &event, sizeof event);

	if (read_result == -1)
	{
	    fprintf(stderr, "Error reading from device. %s\n", strerror(errno));
	    break;
	}

	bindings_found = find_matching_bindings(epoll_event.data.fd == imu_fd, event.type, event.code, binding_indices, sizeof binding_indices / sizeof binding_indices[0]);

	for (i = 0; i < bindings_found; i++)
	{
	    process_binding_event(&event, binding_indices[i]);
	}
    }


    xcb_disconnect(c);

    for (i = 0; i < 4; i++)
    {
	if (led_fd[i] != -1)
	{
	    close(led_fd[i]);
	}
    }

    close(epoll_fd);
    close(dev_fd);
    udev_device_unref(device);
    udev_unref(udev);
    return 0;
}
